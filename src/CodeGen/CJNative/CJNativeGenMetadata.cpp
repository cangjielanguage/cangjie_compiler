// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Implements metadata generator.
 */

#include "CJNative/CJNativeMetadata.h"

#include "Base/CGTypes/CGEnumType.h"
#include "IRBuilder.h"
#include "Utils/CGUtils.h"

using namespace Cangjie;
using namespace CodeGen;
using ChirTypeKind = Cangjie::CHIR::Type::TypeKind;
using CGEnumKind = Cangjie::CodeGen::CGEnumType::CGEnumTypeKind;

namespace {
inline bool IsClassForBoxType(const std::string& className)
{
    return className.find(BOX_DECL_PREFIX) != std::string::npos;
}

std::string GetEnumKindName(CGModule& cgMod, const CHIR::EnumDef& ed)
{
    auto cgType = StaticCast<const CGEnumType*>(CGType::GetOrCreate(cgMod, ed.GetType()));
    switch (cgType->GetCGEnumTypeKind()) {
        case CGEnumKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumKind::EXHAUSTIVE_ZERO_SIZE:
        case CGEnumKind::EXHAUSTIVE_UNASSOCIATED:
            return "enumKind0";
        case CGEnumKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumKind::EXHAUSTIVE_OTHER:
            return "enumKind1";
        case CGEnumKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF:
        case CGEnumKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF:
        case CGEnumKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T:
            return "enumKind2";
        case CGEnumKind::EXHAUSTIVE_ASSOCIATED_NONREF:
            return "enumKind3";
        default:
            CJC_ASSERT(false && "should not reach here");
            return "UNKNOWN";
    }
}

uint8_t GetSRetMode(CHIR::Type& retTy, const CodeGen::CGFunction& cgFunc)
{
    if (!cgFunc.IsSRet()) {
        return SRetMode::NO_SRET;
    }
    if (!retTy.IsGenericRelated()) {
        return SRetMode::SRET_NON_GENERIC;
    }
    if (retTy.IsGeneric()) {
        return SRetMode::SRET_GENERIC;
    }
    auto sretTy = cgFunc.GetRawFunction()->getArg(0)->getType();
    if (sretTy->isPointerTy()) {
        if (sretTy->getPointerAddressSpace() == 0) {
            return SRetMode::SRET_KNOWN_GENERIC_CUSTOM;
        }
        return SRetMode::SRET_UNKNOWN_GENERIC_CUSTOM;
    }
    CJC_ASSERT(false && "should not reach here");
    return SRetMode::SRET_NON_GENERIC;
}
} // namespace

llvm::MDTuple* MetadataInfo::GenerateParametersMetadata(
    const std::vector<CHIR::Parameter*>& argsInfo, bool hasThis) const
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    MetadataVector res(llvmCtx);
    for (auto arg : argsInfo) {
        auto argId = arg->GetSrcCodeIdentifier();
        // If this is non-static member function, the first parameter is "this" added by the compiler.
        // And we don't generate metadata for "this" since it is known by runtime.
        if (arg == argsInfo.front() && hasThis) {
            continue;
        }
        // {!param name, !type name, !attributes}
        res.AddSubItem(MetadataVector(llvmCtx)
                           .Concat(argId)
                           .Concat(GetTiName(*arg->GetType()))
                           .Concat(GenerateAttrsMetadata(CHIR::AttributeInfo(), {}, arg->GetAnnoInfo().mangledName)));
    }
    return res.CreateMDTuple();
}

llvm::MDTuple* MetadataInfo::GenerateParametersMetadata(
    const std::vector<CHIR::AbstractMethodParam>& paramInfos, bool hasThis) const
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    MetadataVector res(llvmCtx);
    for (auto& param : paramInfos) {
        auto paramId = param.paramName;
        // If this is non-static member function, the first parameter is "this" added by the compiler.
        // And we don't generate metadata for "this" since it is known by runtime.
        if (&param == &paramInfos.front() && hasThis) {
            continue;
        }
        // {!param name, !type name, !attributes}
        res.AddSubItem(MetadataVector(llvmCtx)
                           .Concat(paramId)
                           .Concat(GetTiName(*param.type))
                           .Concat(GenerateAttrsMetadata(CHIR::AttributeInfo(), {}, param.annoInfo.mangledName)));
    }
    return res.CreateMDTuple();
}

llvm::MDTuple* MetadataInfo::GenerateParametersMetadata(const std::vector<CHIR::GenericType*>& genericParamInfos) const
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    MetadataVector res(llvmCtx);
    for (auto arg : genericParamInfos) {
        // !{!genericParamType name}
        res.Concat(GetTiName(*arg));
    }
    return res.CreateMDTuple();
}

llvm::MDTuple* MetadataInfo::GenerateAttrsMetadata(const CHIR::AttributeInfo& attrs, ExtraAttribute extraAttr,
    const std::string& gettingAnnotationMethod, uint8_t hasSRetMode, const std::string& enumKind) const
{
    static const std::map<CHIR::Attribute, std::string> TEST_ATTRS{
        {CHIR::Attribute::PUBLIC, "public"},
        {CHIR::Attribute::PROTECTED, "protected"},
        {CHIR::Attribute::PRIVATE, "private"},
        {CHIR::Attribute::VIRTUAL, "open"},
        {CHIR::Attribute::OVERRIDE, "override"},
        {CHIR::Attribute::REDEF, "redef"},
        {CHIR::Attribute::MUT, "mut"},
        {CHIR::Attribute::STATIC, "static"},
        {CHIR::Attribute::ABSTRACT, "abstract"},
        {CHIR::Attribute::SEALED, "sealed"},
    };

    std::set<std::string> attrsStr;
    switch (extraAttr) {
        case ExtraAttribute::METHOD_FROM_INTERFACE:
            attrsStr.emplace("abstract");
            break;
        case ExtraAttribute::IMMUTABLE_FIELD: // let variable
            attrsStr.emplace(ATTR_IMMUTABLE);
            break;
        case ExtraAttribute::INTERFACE:
            attrsStr.emplace(METADATA_ATTR_OPEN);
            break;
        case ExtraAttribute::ENUM:
            attrsStr.emplace(enumKind);
            break;
        case ExtraAttribute::BOX_CLASS:
            attrsStr.emplace("box");
            break;
        default:
            break;
    }

    if (hasSRetMode != SRetMode::NO_SRET) {
        attrsStr.emplace("hasSRet" + std::to_string(hasSRetMode));
    }

    for (auto& attr : TEST_ATTRS) {
        if (attrs.TestAttr(attr.first)) {
            attrsStr.emplace(attr.second);
        }
    }

    MetadataVector ops(module.GetLLVMContext());
    for (auto& str : attrsStr) {
        (void)ops.Concat(str);
    }

    (void)ops.Concat(gettingAnnotationMethod);
    if (gettingAnnotationMethod != "none") {
        auto chirFunc = module.GetCGContext().GetCGPkgContext().FindCHIRGlobalValue(gettingAnnotationMethod);
        (void)module.GetOrInsertCGFunction(chirFunc);
    }

    return ops.CreateMDTuple();
}

std::string MetadataInfo::GetTiName(const CHIR::Type& ty) const
{
    if (ty.IsRef()) {
        auto refTy = StaticCast<const CHIR::RefType&>(ty).GetBaseType();
        return GetTiName(*refTy);
    }
    auto cgType = CGType::GetOrCreate(module, &ty);
    if (ty.IsGenericRelated()) {
        auto ti = cgType->GetOrCreateTypeInfo();
        return ti->getName().str();
    }
    if (ty.IsAutoEnvBase()) {
        if (ty.IsAutoEnvInstBase()) {
            auto superClassTy = const_cast<CHIR::ClassType&>(static_cast<const CHIR::ClassType&>(ty))
                                    .GetSuperClassTy(&module.GetCGContext().GetCHIRBuilder());
            return GetTiName(static_cast<const CHIR::Type&>(*superClassTy));
        }
    }
    auto tiName = CGType::GetNameOfTypeInfoGV(ty);
    if (!module.GetLLVMModule()->getNamedGlobal(tiName)) {
        auto ti = cgType->GetOrCreateTypeInfo();
        if (cgType->IsStaticGI()) {
            module.GetCGContext().RegisterReflectGeneratedStaticGIName(ti->getName().str());
        }
        return ti->getName().str();
    }
    return tiName;
}

llvm::MDTuple* MetadataInfo::GenerateInstanceFieldMetadata(const CHIR::MemberVarInfo& field, size_t idx)
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    return MetadataVector(llvmCtx)
        .Concat(idx)
        .Concat(field.name)
        .Concat(GenerateAttrsMetadata(field.attributeInfo,
            field.TestAttr(CHIR::Attribute::READONLY) ? ExtraAttribute::IMMUTABLE_FIELD
                                                       : ExtraAttribute::MUTABLE_FIELD,
            field.annoInfo.mangledName))
        .CreateMDTuple();
}

llvm::MDTuple* MetadataInfo::GenerateStaticFieldMetadata(const CHIR::GlobalVarBase& staticField)
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    return MetadataVector(llvmCtx)
        .Concat(staticField.GetSrcCodeIdentifier())
        .Concat(GetTiName(*staticField.GetType()))
        .Concat(staticField.GetIdentifierWithoutPrefix())
        .Concat(GenerateAttrsMetadata(staticField.GetAttributeInfo(),
            staticField.TestAttr(CHIR::Attribute::READONLY) ? ExtraAttribute::IMMUTABLE_FIELD
                                                             : ExtraAttribute::MUTABLE_FIELD,
            staticField.GetAnnoInfo().mangledName))
        .CreateMDTuple();
}

llvm::MDTuple* MetadataInfo::GenerateMethodMetadata(const CHIR::FuncBase& method, bool isFromInterface)
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    CJC_ASSERT(method.GetType()->IsFunc());
    auto methodName = llvm::MDString::get(llvmCtx, method.GetSrcCodeIdentifier());
    auto methodLinkageName = llvm::MDString::get(llvmCtx, method.GetIdentifierWithoutPrefix());
    auto funcType = StaticCast<CHIR::FuncType*>(method.GetType());
    auto retTypeInfo = GetTiName(*funcType->GetReturnType());
    MetadataVector methodMD(llvmCtx);
    methodMD.Concat(methodName).Concat(retTypeInfo).Concat(methodLinkageName);
    auto extraAttr = isFromInterface ? ExtraAttribute::METHOD_FROM_INTERFACE : ExtraAttribute::METHOD;
    if (method.IsFuncWithBody()) {
        auto methodValue = StaticCast<CHIR::Func*>(&method);
        CJC_NULLPTR_CHECK(methodValue);
        uint8_t hasSRetMode = GetSRetMode(*funcType->GetReturnType(), *module.GetOrInsertCGFunction(methodValue));
        methodMD
            .Concat(method.TestAttr(CHIR::Attribute::STATIC)
                    ? GenerateParametersMetadata(methodValue->GetParams())
                    : GenerateParametersMetadata(methodValue->GetParams(), true))
            .Concat(GenerateParametersMetadata(methodValue->GetGenericTypeParams()))
            .Concat(GenerateAttrsMetadata(
                method.GetAttributeInfo(), extraAttr, methodValue->GetAnnoInfo().mangledName, hasSRetMode));
        (void)module.GetOrInsertCGFunction(methodValue);
    } else {
        auto importedMethodValue = StaticCast<CHIR::ImportedFunc*>(&method);
        CJC_NULLPTR_CHECK(importedMethodValue);
        uint8_t hasSRetMode =
            GetSRetMode(*funcType->GetReturnType(), *module.GetOrInsertCGFunction(importedMethodValue));
        methodMD
            .Concat(method.TestAttr(CHIR::Attribute::STATIC)
                    ? GenerateParametersMetadata(importedMethodValue->GetParamInfo())
                    : GenerateParametersMetadata(importedMethodValue->GetParamInfo(), true))
            .Concat(GenerateParametersMetadata(importedMethodValue->GetGenericTypeParams()))
            .Concat(GenerateAttrsMetadata(
                method.GetAttributeInfo(), extraAttr, importedMethodValue->GetAnnoInfo().mangledName, hasSRetMode));
        (void)module.GetOrInsertCGFunction(importedMethodValue);
    }
    return methodMD.CreateMDTuple();
}

void PkgMetadataInfo::GeneratePkgMetadata() const
{
    auto llvmMod = module.GetLLVMModule();
    auto& llvmCtx = module.GetLLVMContext();
    llvm::NamedMDNode* pkgInfo = llvmMod->getOrInsertNamedMetadata(METADATA_PKG);
    auto names = Utils::SplitQualifiedName(module.GetCGContext().GetCurrentPkgName());
    auto moduleName = names.front();
    auto pkgName = module.GetCGContext().GetCurrentPkgName();
    auto currentIdx = subCHIRPkg.subCHIRPackageIdx;
    std::string version = ""; // for dynamic loading
    pkgInfo->addOperand(
        MetadataVector(llvmCtx)
            .Concat(static_cast<uint32_t>(reflectionMode == GenReflectMode::FULL_REFLECT))
            .Concat(version)
            .Concat(moduleName)
            .Concat(pkgName)
            .Concat(subCHIRPkg.splitNum == 1 ? moduleName + "_" + pkgName
                                             : moduleName + "_" + pkgName + "_" + std::to_string(currentIdx))
            .Concat(currentIdx < subCHIRPkg.splitNum - 1
                    ? moduleName + "_" + pkgName + "_" + std::to_string(currentIdx + 1)
                    : "")
            .CreateMDTuple());
    if (pkgName == CORE_PACKAGE_NAME && currentIdx == 0) {
        AddPrimitiveTypeInfoToCorePkgInfo();
    }
}

void PkgMetadataInfo::AddPrimitiveTypeInfoToCorePkgInfo() const
{
    auto primitiveTIsMD = module.GetLLVMModule()->getOrInsertNamedMetadata(METADATA_PRIMITIVE_TYPES);
    auto primitiveTTsMD = module.GetLLVMModule()->getOrInsertNamedMetadata(METADATA_PRIMITIVE_TYPETEMPLATES);
    auto& llvmCtx = module.GetLLVMContext();
    auto typeTemplateType = CGType::GetOrCreateTypeTemplateType(llvmCtx);
    auto typeInfoType = CGType::GetOrCreateTypeInfoType(llvmCtx);
    for (auto& tiName : PRIMITIVE_TIS) {
        auto ti = llvm::cast<llvm::GlobalVariable>(module.GetLLVMModule()->getOrInsertGlobal(tiName, typeInfoType));
        ti->addAttribute(GC_KLASS_ATTR);
        primitiveTIsMD->addOperand(MetadataVector(llvmCtx).Concat(tiName).CreateMDTuple());
    }
    for (auto& ttName : PRIMITIVE_TTS) {
        module.GetLLVMModule()->getOrInsertGlobal(ttName, typeTemplateType);
        primitiveTTsMD->addOperand(MetadataVector(llvmCtx).Concat(ttName).CreateMDTuple());
    }
}

void StructMetadataInfo::GenerateAllStructsMetadata()
{
    std::set<std::string> doneSet;
    for (auto typeDef : subCHIRPkg.chirCustomDefs) {
        if (typeDef->TestAttr(CHIR::Attribute::GENERIC_INSTANTIATED) ||
            typeDef->TestAttr(CHIR::Attribute::IMPORTED) || typeDef->TestAttr(CHIR::Attribute::NON_RECOMPILE) ||
            typeDef->TestAttr(CHIR::Attribute::NO_REFLECT_INFO) || !typeDef->IsStruct()) {
            continue;
        }
        GenerateStructMetadata(StaticCast<const CHIR::StructDef&>(*typeDef), doneSet);
    }
    for (auto& typeName : doneSet) {
        module.GetCGContext().AddGeneratedStructType(typeName);
    }
}

void StructMetadataInfo::GenerateStructMetadata(const CHIR::StructDef& sd, std::set<std::string>& doneSet)
{
    auto typeName = sd.GetIdentifierWithoutPrefix();
    auto structTypeName = STRUCT_TYPE_PREFIX + typeName;
    if (doneSet.count(structTypeName) > 0) {
        return;
    }

    auto typesMD = sd.TestAttr(CHIR::Attribute::GENERIC)
        ? module.GetLLVMModule()->getOrInsertNamedMetadata(METADATA_TYPETEMPLATES)
        : module.GetLLVMModule()->getOrInsertNamedMetadata(METADATA_TYPES);
    std::string tiOrTTName = sd.TestAttr(CHIR::Attribute::GENERIC) ? CGType::GetNameOfTypeTemplateGV(*sd.GetType())
                                                                    : CGType::GetNameOfTypeInfoGV(*sd.GetType());
    llvm::GlobalVariable* reflectTIOrTT = module.GetLLVMModule()->getGlobalVariable(tiOrTTName, true);

    if (!reflectTIOrTT) {
        return;
    }

    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    if (reflectionMode != GenReflectMode::FULL_REFLECT) {
        auto mdTuple =
            MetadataVector(llvmCtx).Concat(tiOrTTName).Concat(GenerateStructFieldMetadata(sd)).CreateMDTuple();
        typesMD->addOperand(mdTuple);
        reflectTIOrTT->addMetadata("Reflection", *mdTuple);
        doneSet.emplace(structTypeName);
        return;
    }
    auto declaredGenericTi = sd.TestAttr(CHIR::Attribute::GENERIC) ? GetTiName(*sd.GetType()) : "";

    std::vector<llvm::Metadata*> methodsVec{};
    std::vector<llvm::Metadata*> staticMethodsVec{};
    GenerateStructMethodMetadata(sd, methodsVec, staticMethodsVec);

    MetadataTypeItem item(llvm::MDString::get(llvmCtx, tiOrTTName), llvm::MDString::get(llvmCtx, declaredGenericTi),
        GenerateStructFieldMetadata(sd), GenerateStructStaticFieldMetadata(sd), llvm::MDTuple::get(llvmCtx, methodsVec),
        llvm::MDTuple::get(llvmCtx, staticMethodsVec),
        GenerateAttrsMetadata(sd.GetAttributeInfo(), ExtraAttribute::STRUCT, sd.GetAnnoInfo().mangledName));

    auto mdTuple = item.CreateMDTuple(llvmCtx);
    typesMD->addOperand(mdTuple);
    reflectTIOrTT->addMetadata("Reflection", *mdTuple);
    doneSet.emplace(structTypeName);
}

llvm::MDTuple* StructMetadataInfo::GenerateStructFieldMetadata(const CHIR::StructDef& sd)
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    MetadataVector fieldsVec(llvmCtx);
    int idx = 0;
    for (auto& field : sd.GetAllInstanceVars()) {
        fieldsVec.Concat(GenerateInstanceFieldMetadata(field, idx));
        idx++;
    }
    return fieldsVec.CreateMDTuple();
}

llvm::MDTuple* StructMetadataInfo::GenerateStructStaticFieldMetadata(const CHIR::StructDef& sd)
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    MetadataVector staticFieldsVec(llvmCtx);
    for (auto staticField : sd.GetStaticMemberVars()) {
        staticFieldsVec.Concat(GenerateStaticFieldMetadata(*staticField));
        (void)module.GetOrInsertGlobalVariable(staticField);
    }
    return staticFieldsVec.CreateMDTuple();
}

void StructMetadataInfo::GenerateStructMethodMetadata(const CHIR::StructDef& sd,
    std::vector<llvm::Metadata*>& methodsVec, std::vector<llvm::Metadata*>& staticMethodsVec)
{
    for (auto method : sd.GetMethods()) {
        if (method->TestAttr(CHIR::Attribute::NO_REFLECT_INFO) || method->TestAttr(CHIR::Attribute::IMPORTED)) {
            continue;
        }
        auto methodMD = GenerateMethodMetadata(*method);
        (void)module.GetOrInsertCGFunction(method);
        if (method->TestAttr(CHIR::Attribute::STATIC)) {
            staticMethodsVec.push_back(methodMD);
        } else {
            methodsVec.push_back(methodMD);
        }
    }
}

void ClassMetadataInfo::GenerateAllClassesMetadata()
{
    for (const auto typeDef : subCHIRPkg.chirCustomDefs) {
        if (typeDef->TestAttr(CHIR::Attribute::GENERIC_INSTANTIATED) ||
            typeDef->TestAttr(CHIR::Attribute::NON_RECOMPILE) || typeDef->TestAttr(CHIR::Attribute::IMPORTED) ||
            !typeDef->IsClassLike()) {
            continue;
        }
        GenerateClassLikeMetadata(StaticCast<const CHIR::ClassDef&>(*typeDef));
    }
}

void ClassMetadataInfo::GenerateClassLikeMetadata(const CHIR::ClassDef& cd)
{
    if (cd.TestAttr(CHIR::Attribute::NO_REFLECT_INFO) && !IsClassForBoxType(cd.GetSrcCodeIdentifier())) {
        return;
    }

    auto typesMD = cd.TestAttr(CHIR::Attribute::GENERIC)
        ? module.GetLLVMModule()->getOrInsertNamedMetadata(METADATA_TYPETEMPLATES)
        : module.GetLLVMModule()->getOrInsertNamedMetadata(METADATA_TYPES);
    std::string tiOrTTName = cd.TestAttr(CHIR::Attribute::GENERIC) ? CGType::GetNameOfTypeTemplateGV(*cd.GetType())
                                                                    : CGType::GetNameOfTypeInfoGV(*cd.GetType());
    auto reflectTIOrTT = module.GetLLVMModule()->getGlobalVariable(tiOrTTName, true);
    if (!reflectTIOrTT) {
        return;
    }
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    if (reflectionMode != GenReflectMode::FULL_REFLECT) {
        auto mdTuple =
            MetadataVector(llvmCtx).Concat(tiOrTTName).Concat(GenerateClassLikeFieldMetadata(cd)).CreateMDTuple();
        typesMD->addOperand(mdTuple);
        reflectTIOrTT->addMetadata("Reflection", *mdTuple);
        return;
    }
    auto declaredGenericTi = cd.TestAttr(CHIR::Attribute::GENERIC) ? GetTiName(*cd.GetType()) : "";

    std::vector<llvm::Metadata*> methodsVec{};
    std::vector<llvm::Metadata*> staticMethodsVec{};
    GenerateClassLikeMethodMetadata(cd, methodsVec, staticMethodsVec);

    MetadataTypeItem item(llvm::MDString::get(llvmCtx, tiOrTTName), llvm::MDString::get(llvmCtx, declaredGenericTi),
        GenerateClassLikeFieldMetadata(cd), GenerateClassLikeStaticFieldMetadata(cd),
        llvm::MDTuple::get(llvmCtx, methodsVec), llvm::MDTuple::get(llvmCtx, staticMethodsVec),
        GenerateClassLikeTypeAttrsMetadata(cd));
    auto mdTuple = item.CreateMDTuple(llvmCtx);
    typesMD->addOperand(mdTuple);
    reflectTIOrTT->addMetadata("Reflection", *mdTuple);
}

void ClassMetadataInfo::GenerateClassLikeMethodMetadata(
    const CHIR::ClassDef& cd, std::vector<llvm::Metadata*>& methodsVec, std::vector<llvm::Metadata*>& staticMethodsVec)
{
    if (IsClassForBoxType(cd.GetSrcCodeIdentifier())) {
        return;
    }
    for (auto method : cd.GetMethods()) {
        if (method->TestAttr(CHIR::Attribute::NO_REFLECT_INFO)) {
            continue;
        }
        auto methodMD = GenerateMethodMetadata(*method, cd.IsInterface());
        if (method->TestAttr(CHIR::Attribute::STATIC)) {
            staticMethodsVec.push_back(methodMD);
        } else {
            methodsVec.push_back(methodMD);
        }
    }
    for (auto& absMethod : cd.GetAbstractMethods()) {
        if (absMethod.TestAttr(CHIR::Attribute::NO_REFLECT_INFO) || absMethod.hasBody) {
            continue;
        }
        auto methodMD = GenerateClassAbsMethodMetadata(absMethod);
        if (absMethod.TestAttr(CHIR::Attribute::STATIC)) {
            staticMethodsVec.push_back(methodMD);
        } else {
            methodsVec.push_back(methodMD);
        }
    }
}

llvm::MDNode* ClassMetadataInfo::GenerateClassAbsMethodMetadata(const CHIR::AbstractMethodInfo& absMethod)
{
    auto& llvmCtx = module.GetLLVMContext();
    auto methodType = absMethod.methodTy;
    CJC_ASSERT(methodType->IsFunc());
    auto funcTy = StaticCast<CHIR::FuncType*>(methodType);
    // Since the metadata corresponding to the type is collected in the process
    // of `GetCodeGenType`, and the abstract method does not need to generate
    // the corresponding ir, naturally, `GetCodeGenType` won't be invoked for the
    // formal parameter. To collect the metadata corresponding to the parameter
    // type, we need to explicitly call `GetCodeGenType`.
    // Handle params.
    std::vector<CHIR::AbstractMethodParam> params = absMethod.paramInfos;
    if (!absMethod.TestAttr(CHIR::Attribute::STATIC) && !params.empty()) {
        // The first parameter name of a non-static function should be `this`.
        params[0].paramName = "this";
    }
    auto attr = absMethod.attributeInfo;
    if (!absMethod.TestAttr(CHIR::Attribute::STATIC)) {
        attr.SetAttr(CHIR::Attribute::VIRTUAL, true);
    }
    auto retTypeInfo = GetTiName(*funcTy->GetReturnType());
    auto mdTuple = MetadataVector(llvmCtx)
                       .Concat(absMethod.methodName)
                       .Concat(retTypeInfo)
                       .Concat("")
                       .Concat(GenerateParametersMetadata(params, !absMethod.TestAttr(CHIR::Attribute::STATIC)))
                       .Concat(GenerateParametersMetadata(absMethod.methodGenericTypeParams))
                       .Concat(GenerateAttrsMetadata(attr, ExtraAttribute::METHOD, absMethod.annoInfo.mangledName))
                       .CreateMDTuple();
    return mdTuple;
}

llvm::MDTuple* ClassMetadataInfo::GenerateClassLikeStaticFieldMetadata(const CHIR::ClassDef& cd)
{
    auto& llvmCtx = module.GetCGContext().GetLLVMContext();
    if (cd.IsInterface()) {
        return llvm::MDTuple::get(llvmCtx, {});
    }
    MetadataVector fieldsVec(llvmCtx);
    for (auto value : cd.GetStaticMemberVars()) {
        fieldsVec.Concat(GenerateStaticFieldMetadata(*value));
        (void)module.GetOrInsertGlobalVariable(value);
    }
    return fieldsVec.CreateMDTuple();
}

llvm::MDTuple* ClassMetadataInfo::GenerateClassLikeFieldMetadata(const CHIR::ClassDef& cd)
{
    auto& llvmCtx = module.GetCGContext().GetLLVMContext();
    if (cd.IsInterface()) {
        return llvm::MDTuple::get(llvmCtx, {});
    }
    MetadataVector fieldsVec(llvmCtx);
    size_t idx = cd.GetAllInstanceVarNum() - cd.GetDirectInstanceVarNum();
    for (auto& field : cd.GetDirectInstanceVars()) {
        fieldsVec.Concat(GenerateInstanceFieldMetadata(field, idx));
        idx++;
    }
    return fieldsVec.CreateMDTuple();
}

llvm::MDTuple* ClassMetadataInfo::GenerateClassLikeTypeAttrsMetadata(const CHIR::ClassDef& cd) const
{
    auto metadataType = ExtraAttribute::CLASS;
    if (cd.IsInterface()) {
        metadataType = ExtraAttribute::INTERFACE;
    }
    if (IsClassForBoxType(cd.GetSrcCodeIdentifier())) {
        metadataType = ExtraAttribute::BOX_CLASS;
    }
    return GenerateAttrsMetadata(cd.GetAttributeInfo(), metadataType, cd.GetAnnoInfo().mangledName);
}

void EnumMetadataInfo::GenerateAllEnumsMetadata()
{
    for (const auto typeDef : subCHIRPkg.chirCustomDefs) {
        if (typeDef->TestAttr(CHIR::Attribute::GENERIC_INSTANTIATED) ||
            typeDef->TestAttr(CHIR::Attribute::NO_REFLECT_INFO) ||
            typeDef->TestAttr(CHIR::Attribute::NON_RECOMPILE) || typeDef->TestAttr(CHIR::Attribute::IMPORTED) ||
            !typeDef->IsEnum()) {
            continue;
        }
        GenerateEnumMetadata(StaticCast<const CHIR::EnumDef&>(*typeDef));
    }
}

void EnumMetadataInfo::GenerateEnumMetadata(const CHIR::EnumDef& ed)
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();

    auto typesMD = ed.TestAttr(CHIR::Attribute::GENERIC)
        ? module.GetLLVMModule()->getOrInsertNamedMetadata(METADATA_TYPETEMPLATES)
        : module.GetLLVMModule()->getOrInsertNamedMetadata(METADATA_TYPES);
    std::string tiOrTTName = ed.TestAttr(CHIR::Attribute::GENERIC) ? CGType::GetNameOfTypeTemplateGV(*ed.GetType())
                                                                    : CGType::GetNameOfTypeInfoGV(*ed.GetType());
    auto reflectTIOrTT = module.GetLLVMModule()->getGlobalVariable(tiOrTTName, true);
    if (!reflectTIOrTT) {
        return;
    }

    if (reflectionMode != GenReflectMode::FULL_REFLECT) {
        auto mdTuple = MetadataVector(llvmCtx)
                           .Concat(tiOrTTName)
                           .Concat(GenerateEnumConstructorMetadata(ed))
                           .Concat(GenerateAttrsMetadata(ed.GetAttributeInfo(), ExtraAttribute::ENUM,
                               ed.GetAnnoInfo().mangledName, SRetMode::NO_SRET, GetEnumKindName(module, ed)))
                           .CreateMDTuple();
        typesMD->addOperand(mdTuple);
        reflectTIOrTT->addMetadata("Reflection", *mdTuple);
        return;
    }

    std::vector<llvm::Metadata*> methodsVec{};
    std::vector<llvm::Metadata*> staticMethodsVec{};
    GenerateEnumMethodMetadata(ed, methodsVec, staticMethodsVec);

    MetadataTypeItem item(llvm::MDString::get(llvmCtx, tiOrTTName), llvm::MDString::get(llvmCtx, ""),
        GenerateEnumConstructorMetadata(ed), llvm::MDTuple::get(llvmCtx, {}), llvm::MDTuple::get(llvmCtx, methodsVec),
        llvm::MDTuple::get(llvmCtx, staticMethodsVec),
        GenerateAttrsMetadata(ed.GetAttributeInfo(), ExtraAttribute::ENUM, ed.GetAnnoInfo().mangledName,
            SRetMode::NO_SRET, GetEnumKindName(module, ed)));
    auto mdTuple = item.CreateMDTuple(llvmCtx, true);
    typesMD->addOperand(mdTuple);
    reflectTIOrTT->addMetadata("Reflection", *mdTuple);
}

llvm::MDTuple* EnumMetadataInfo::GenerateEnumConstructorMetadata(const CHIR::EnumDef& ed)
{
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    MetadataVector fieldsVec(llvmCtx);
    size_t index = 0;
    for (auto& ctor : ed.GetCtors()) {
        auto ctorName = ctor.name;
        auto currentCgType = StaticCast<const CGEnumType*>(CGType::GetOrCreate(module, ed.GetType()));
        if (currentCgType->IsOptionLike()) {
            size_t nonArgIndex = currentCgType->IsAntiOptionLike() ? 0 : 1;
            ctorName = nonArgIndex == index && !IsCoreOption(ed) ? "N$_" + ctor.name : ctor.name;
        }
        if (currentCgType->IsTrivial() || currentCgType->IsZeroSizeEnum()) {
            fieldsVec.AddSubItem(MetadataVector(llvmCtx).Concat(ctorName).Concat(""));
        } else {
            std::string ti = GenerateCtorFn(ed, index++, GetTypeQualifiedName(*ed.GetType()));
            fieldsVec.AddSubItem(MetadataVector(llvmCtx).Concat(ctorName).Concat(ti));
        }
    }
    return fieldsVec.CreateMDTuple();
}

std::string EnumMetadataInfo::GenerateCtorFn(
    const CHIR::EnumDef& enumDef, size_t index, const std::string& qualifiedName)
{
    auto funcName = qualifiedName + ".Ctor_" + std::to_string(index) + ".Fn";
    auto getTiFn = module.GetLLVMModule()->getFunction(funcName);
    if (!getTiFn) {
        std::vector<llvm::Type*> argTypes;
        auto typeInfoType = CGType::GetOrCreateTypeInfoType(module.GetLLVMContext());
        argTypes.emplace_back(llvm::Type::getInt32Ty(module.GetLLVMContext()));
        argTypes.emplace_back(typeInfoType->getPointerTo()->getPointerTo());
        llvm::FunctionType* ctorFn =
            llvm::FunctionType::get(llvm::Type::getInt8PtrTy(module.GetLLVMContext()), argTypes, false);
        getTiFn = llvm::Function::Create(ctorFn, llvm::Function::PrivateLinkage, funcName, module.GetLLVMModule());
        CodeGen::IRBuilder2 irBuilder(module);
        auto entryBB = irBuilder.CreateEntryBasicBlock(getTiFn, "entry");
        irBuilder.SetInsertPoint(entryBB);
        llvm::Value* ti{nullptr};
        auto chirEnumType = StaticCast<CHIR::EnumType*>(enumDef.GetType());
        if (CGType::GetOrCreate(module, chirEnumType)->IsDynamicGI()) {
            auto tt = module.GetOrCreateEnumCtorTIOrTT(*chirEnumType, index);
            ti = irBuilder.CallIntrinsicGetTypeInfo({tt, getTiFn->getArg(0), getTiFn->getArg(1)});
        } else {
            ti = module.GetOrCreateEnumCtorTIOrTT(*chirEnumType, index);
        }
        llvm::Value* bitcastedValue = irBuilder.CreateBitCast(ti, llvm::Type::getInt8PtrTy(module.GetLLVMContext()));
        irBuilder.CreateRet(bitcastedValue);
    }
    return funcName;
}

void EnumMetadataInfo::GenerateEnumMethodMetadata(
    const CHIR::EnumDef& ed, std::vector<llvm::Metadata*>& methodsVec, std::vector<llvm::Metadata*>& staticMethodsVec)
{
    for (auto method : ed.GetMethods()) {
        if (method->TestAttr(CHIR::Attribute::NO_REFLECT_INFO) || method->TestAttr(CHIR::Attribute::IMPORTED)) {
            continue;
        }
        auto methodMD = GenerateMethodMetadata(*method);
        if (method->TestAttr(CHIR::Attribute::STATIC)) {
            staticMethodsVec.push_back(methodMD);
        } else {
            methodsVec.push_back(methodMD);
        }
    }
}

void GFMetadataInfo::GenerateAllFunctionsMetadata()
{
    if (reflectionMode != GenReflectMode::FULL_REFLECT) {
        return;
    }
    auto& llvmCtx = module.GetLLVMContext();
    auto llvmMod = module.GetLLVMModule();
    /// Generate "functions" metadata for functions without "@C"\"@Java"\"foreign".
    llvm::NamedMDNode* functionsMdNode = llvmMod->getOrInsertNamedMetadata(METADATA_FUNCTIONS);
    for (auto gf : subCHIRPkg.chirFuncs) {
        // Member functions also have the Global attribute, which cannot be determined by `TestAttr(Attribute::GLOBAL)`.
        if (gf->GetParentCustomTypeDef()) {
            continue;
        }
        if (gf->TestAttr(CHIR::Attribute::NO_REFLECT_INFO) || gf->TestAttr(CHIR::Attribute::IMPORTED) ||
            gf->GetSrcCodeIdentifier().empty() || gf->GetFuncType()->IsCFunc() ||
            gf->TestAttr(CHIR::Attribute::GENERIC_INSTANTIATED)) {
            continue;
        }
        auto retTypeInfo = GetTiName(*gf->GetReturnType());
        uint8_t hasSRetMode = GetSRetMode(*gf->GetReturnType(), *module.GetOrInsertCGFunction(gf));
        if (auto func = llvmMod->getFunction(gf->GetIdentifierWithoutPrefix()); func) {
            auto funcMD =
                MetadataVector(llvmCtx)
                    .Concat(gf->GetSrcCodeIdentifier())
                    .Concat(retTypeInfo)
                    .Concat(gf->GetIdentifierWithoutPrefix())
                    .Concat(GenerateParametersMetadata(gf->GetParams()))
                    .Concat(GenerateParametersMetadata(gf->GetGenericTypeParams()))
                    .Concat(GenerateAttrsMetadata(gf->GetAttributeInfo(), ExtraAttribute::METHOD,
                        gf->GetAnnoInfo().mangledName, hasSRetMode))
                    .CreateMDTuple();
            functionsMdNode->addOperand(funcMD);
            func->addMetadata("ReflectionFunc", *funcMD);
        }
    }
}

void GVMetadataInfo::GenerateGlobalVariablesMetadata()
{
    if (reflectionMode != GenReflectMode::FULL_REFLECT) {
        return;
    }
    auto genGlobalVarsMetadata = [this](const std::vector<CHIR::GlobalVar*>& variables) {
        auto varsMdNode = module.GetLLVMModule()->getOrInsertNamedMetadata(METADATA_GLOBAL_VAR);
        for (const auto it : variables) {
            if (it->TestAttr(CHIR::Attribute::STATIC) || it->TestAttr(CHIR::Attribute::NO_REFLECT_INFO) ||
                it->TestAttr(CHIR::Attribute::IMPORTED)) {
                continue;
            }
            if (const auto variableMetadata = GenerateVariableMetadata(*it); variableMetadata) {
                varsMdNode->addOperand(variableMetadata);
                module.GetLLVMModule()
                    ->getNamedGlobal(it->GetIdentifierWithoutPrefix())
                    ->addMetadata("ReflectionGV", *variableMetadata);
            }
        }
    };

    /// "global_variables" contains the variables which are global but not static.
    genGlobalVarsMetadata(std::vector<CHIR::GlobalVar*>(subCHIRPkg.chirGVs.begin(), subCHIRPkg.chirGVs.end()));
}

llvm::MDNode* GVMetadataInfo::GenerateVariableMetadata(const CHIR::GlobalVar& variable)
{
    auto llvmMod = module.GetLLVMModule();
    auto variableMangleName = variable.GetIdentifierWithoutPrefix();

    /// If a CHIR::Variable node is not generated as a global variable in the current module, or the node is imported
    /// to the current module, we don't need to generate metadata for it. Just return directly.
    const auto globalVariable = llvmMod->getGlobalVariable(variableMangleName, true);
    bool isStringGV = variable.GetInitializer() != nullptr && variable.GetInitializer()->GetType()->IsString();
    if (!globalVariable || (!globalVariable->hasInitializer() && !isStringGV)) {
        return nullptr;
    }
    llvm::LLVMContext& llvmCtx = module.GetLLVMContext();
    return MetadataVector(llvmCtx)
        .Concat(variable.GetSrcCodeIdentifier())
        .Concat(GetTiName(*variable.GetType()))
        .Concat(variableMangleName)
        .Concat(GenerateAttrsMetadata(variable.GetAttributeInfo(),
            variable.TestAttr(CHIR::Attribute::READONLY) ? ExtraAttribute::IMMUTABLE_FIELD
                                                          : ExtraAttribute::MUTABLE_FIELD,
            variable.GetAnnoInfo().mangledName))
        .CreateMDTuple();
}
