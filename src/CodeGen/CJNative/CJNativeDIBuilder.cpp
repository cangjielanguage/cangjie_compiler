// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "DIBuilder.h"

namespace Cangjie {
namespace CodeGen {

llvm::DIType* DIBuilder::CreateRefType()
{
    if (!refDIType) {
        // 64-bits is size of int64 pointer type.
        llvm::DIType* basicType = createBasicType("Int64", 64u, static_cast<unsigned>(llvm::dwarf::DW_ATE_signed));
        refDIType = CreatePointerType(basicType);
    }
    return refDIType;
}

llvm::DIType* DIBuilder::GetOrCreateObjectClassType()
{
    if (!objDIType) {
        auto tag = static_cast<unsigned>(llvm::dwarf::DW_TAG_class_type);
        llvm::DICompositeType* fwdDecl = createReplaceableCompositeType(tag, OBJECT_NAME, diCompileUnit, defaultFile, 0,
            0, 64u, 0u, llvm::DINode::FlagTypePassByReference, OBJECT_NAME);
        objDIType = llvm::MDNode::replaceWithDistinct(llvm::TempDICompositeType(fwdDecl));
    }
    return objDIType;
}

llvm::DIType* DIBuilder::CreateClosureType(const CHIR::Type& ty)
{
    auto classTy = StaticCast<const CHIR::ClassType*>(&ty);
    auto identifier = ty.ToString();
    auto name = GenerateTypeName(*classTy);
    auto& position = classTy->GetClassDef()->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto tag = static_cast<unsigned>(llvm::dwarf::DW_TAG_class_type);
    llvm::DICompositeType* fwdDecl = createReplaceableCompositeType(tag, name, diNamespace, diFile,
        position.GetBeginPos().line, 0, 128u, 0u, llvm::DINode::FlagTypePassByReference, identifier);
    fwdDecl = llvm::MDNode::replaceWithDistinct(llvm::TempDICompositeType(fwdDecl));

    // SmallVector Size: 8.
    llvm::SmallVector<llvm::Metadata*, 8> eltTys;
    // Function return type.
    auto [paramTypes, retType] = GetFuncTypeWithoutThisPtrFromAutoEnvBaseType(*classTy);
    eltTys.push_back(GetOrCreateType(*retType));
    for (auto paramType : paramTypes) {
        llvm::DIType* cgType = GetOrCreateType(*paramType);
        eltTys.push_back(cgType);
    }
    llvm::DISubroutineType* subroutineType = createSubroutineType(getOrCreateTypeArray(eltTys));
    auto funcType = CreatePointerType(subroutineType, 64);

    auto tiType = createMemberType(
        fwdDecl, "$ti", diFile, 0u, CreateRefType()->getSizeInBits(), 0u, 0u, llvm::DINode::FlagZero, CreateRefType());
    auto classPointerType = createMemberType(
        fwdDecl, "ptr", diFile, 0u, CreateRefType()->getSizeInBits(), 0u, 64u, llvm::DINode::FlagZero, funcType);
    replaceArrays(fwdDecl, getOrCreateArray({tiType, classPointerType}));
    return fwdDecl;
}

uint64_t DIBuilder::GetClassSize(llvm::Type* classLayout) const
{
    // For cjnative backend, just get the size information from classLayout.
    return GetTypeSize(classLayout);
}

llvm::DIType* DIBuilder::CreateInterfaceType(const CHIR::ClassType& interfaceTy)
{
    auto interfaceDef = interfaceTy.GetClassDef();
    auto mangledName = interfaceTy.ToString();

    auto& position = interfaceDef->GetDebugLocation();
    auto name = "Interface$" + GenerateCustomTypeName(interfaceTy, false);
    auto diFile = GetOrCreateFile(position);
    auto classSize = 64u;
    auto defPackage = createNameSpace(diCompileUnit, interfaceDef->GetPackageName(), false);
    auto tag = static_cast<unsigned>(llvm::dwarf::DW_TAG_class_type);
    llvm::DICompositeType* fwdDecl = createReplaceableCompositeType(tag, name, defPackage, diFile,
        position.GetBeginPos().line, 0, classSize, 0u, llvm::DINode::FlagTypePassByReference, mangledName);
    fwdDecl = llvm::MDNode::replaceWithDistinct(llvm::TempDICompositeType(fwdDecl));
    typeCache[&interfaceTy] = llvm::TrackingMDRef(fwdDecl);
    CodeGenDIVector16 elements;
    CreateInheritedInterface(elements, fwdDecl, interfaceTy);
    for (auto funcInfo : interfaceTy.GetInstAbstractMethodTypes(cgMod.GetCGContext().GetCHIRBuilder())) {
        auto funcIdentifier = GenerateGenericFuncName(funcInfo.methodName, funcInfo.methodGenericTypeParams);
        auto funcName = funcInfo.GetASTMangledName();
        bool hasThis = funcInfo.TestAttr(CHIR::Attribute::STATIC) ? false : true;
        auto funcType = CreateFuncType(StaticCast<CHIR::FuncType*>(funcInfo.methodTy), false, hasThis);
        llvm::DINode::DIFlags flags = llvm::DINode::FlagPrototyped;
        llvm::DISubprogram::DISPFlags spFlags =
            funcInfo.hasBody ? llvm::DISubprogram::SPFlagZero : llvm::DISubprogram::SPFlagPureVirtual;
        auto funcElement = createFunction(fwdDecl, funcIdentifier, funcName, diFile, 0u, funcType, 0u, flags, spFlags);
        elements.emplace_back(funcElement);
        finalizeSubprogram(funcElement);
    }
    CreateGetGenericFunc(interfaceTy, fwdDecl, elements);
    replaceArrays(fwdDecl, getOrCreateArray(elements));
    return createTypedef(fwdDecl, GenerateCustomTypeName(interfaceTy, false), diFile, 0, diCompileUnit);
}

void DIBuilder::CreateClassMemberType(const CHIR::ClassType& classTy, llvm::Type* classLayout,
    CodeGenDIVector16& elements, llvm::DICompositeType* fwdDecl)
{
    auto classDef = classTy.GetClassDef();
    auto& position = classDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto layout = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(llvm::cast<llvm::StructType>(classLayout));
    // the new objLayout of Class does not have object.
    CHIR::CHIRBuilder& chirBuilder = cgMod.GetCGContext().GetCHIRBuilder();
    auto& nonConstClassType = const_cast<CHIR::ClassType&>(classTy);
    CJC_ASSERT(nonConstClassType.GetSuperClassTy(&chirBuilder));
    uint32_t i = nonConstClassType.GetSuperClassTy(&chirBuilder)->GetInstantiatedMemberTys(chirBuilder).size();
    CJC_ASSERT(nonConstClassType.GetSuperClassTy(&chirBuilder)->GetInstantiatedMemberTys(chirBuilder).size() +
            classDef->GetDirectInstanceVarNum() ==
        nonConstClassType.GetInstantiatedMemberTys(chirBuilder).size());
    for (auto& it : classDef->GetDirectInstanceVars()) {
        if (it.TestAttr(CHIR::Attribute::NO_DEBUG_INFO) &&
            classDef->GetSrcCodeIdentifier().rfind(CC_DEF_PREFIX, 0) == 0) {
            ++i;
            continue;
        }
        uint32_t elementIndex = IsClosureConversionEnvClass(*classDef) ? i + 2U : i;
        auto elementTy = nonConstClassType.GetInstantiatedMemberTys(chirBuilder)[i];
        auto elemType = GetOrCreateType(*elementTy);
        auto cgElemType = classLayout->getStructElementType(elementIndex);
        if (IsReferenceType(*elementTy, cgMod) && !IsOption(*elementTy)) {
            elemType = CreatePointerType(elemType, CreateRefType()->getSizeInBits());
        }
        auto bitInSize = GetTypeSize(cgElemType);
        auto offset = layout->getElementOffsetInBits(elementIndex) + 64;
        auto align = layout->getAlignment().value();
        auto memberType = createMemberType(fwdDecl, it.name, diFile, it.loc.GetBeginPos().line, bitInSize,
            static_cast<uint32_t>(align), offset, llvm::DINode::FlagZero, elemType);
        elements.push_back(memberType);
        ++i;
    }
}

// Array.Type has 3 elements.
void DIBuilder::CreateArrayMemberType(const CHIR::RawArrayType& arrTy, llvm::DICompositeType* fwdDecl,
    CodeGenDIVector3& elements, llvm::Type* arrayLayout)
{
    auto layout = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(llvm::cast<llvm::StructType>(arrayLayout));
    auto pointerSize = CreateRefType()->getSizeInBits();
    auto sizeType = createBasicType("Int64", 64u,
        static_cast<unsigned>(llvm::dwarf::DW_ATE_unsigned)); // int64 type : size.
    CJC_NULLPTR_CHECK(sizeType);
    auto memberType = createMemberType(
        fwdDecl, "$ti", defaultFile, 0u, pointerSize, 0u, 0u, llvm::DINode::FlagArtificial, CreateRefType());
    elements.push_back(memberType);
    memberType = createMemberType(
        fwdDecl, "size", defaultFile, 0u, sizeType->getSizeInBits(), 0u, pointerSize, llvm::DINode::FlagZero, sizeType);
    // Offset for element is size of ArrayBase for chir2llvm.
    elements.push_back(memberType);
    auto elemTy = arrTy.GetElementType();
    auto elemType = GetOrCreateType(*elemTy);
    // Multi-dimension array or class/enum is pointer element.
    if (arrTy.GetDims() > 1 || IsReferenceType(*elemTy, cgMod)) {
        elemType = CreatePointerType(elemType, CreateRefType()->getSizeInBits());
    }
    auto bitInSize = GetSizeInBits(elemType);
    // layout is %ArrayBase, which is behind an object.
    auto offset = layout->getElementOffsetInBits(1) + 64u;
    auto align = layout->getAlignment().value();
    memberType = createMemberType(fwdDecl, "elements", defaultFile, 0u, bitInSize, static_cast<uint32_t>(align), offset,
        llvm::DINode::FlagZero, elemType);
    elements.push_back(memberType);
    replaceArrays(fwdDecl, getOrCreateArray(elements));
}

llvm::DICompositeType* DIBuilder::CreateEnumWithArgsType(
    const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers, const CHIR::Type* boxTy)
{
    /**
     *  The Enum is stored as
     *  64-bits //  TypeInfo* of ctor
     *  32-bits //  constructor
     *  ...     //  arg
     */
    auto enumDef = enumTy.GetEnumDef();
    auto defPackage = createNameSpace(diCompileUnit, enumDef->GetPackageName(), false);
    auto& position = enumDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto name = GenerateCustomTypeName(enumTy, false);
    uint64_t maxSize = 0;
    for (size_t ctorIndex = 0; ctorIndex < enumTy.GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder()).size();
        ctorIndex++) {
        auto enumCtorlayoutType = CGType::GetOrCreateEnumCtorLayoutType(cgMod, enumTy, ctorIndex,
            enumTy.GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder())[ctorIndex].funcType->GetParamTypes());
        maxSize = std::max(maxSize, GetTypeSize(enumCtorlayoutType));
    }
    // add size of typeInfo
    maxSize += 64u;
    auto fwdDecl = createStructType(defPackage, "E1$" + name, diFile, position.GetBeginPos().line, maxSize, 0u,
        llvm::DINode::FlagVirtual, nullptr, {}, 0u, nullptr, "$" + enumTy.ToString());
    if (boxTy) {
        typeCache[boxTy] = llvm::TrackingMDRef(fwdDecl);
    } else {
        typeCache[&enumTy] = llvm::TrackingMDRef(fwdDecl);
    }
    // Generate all constructors as the subclass for the enum.
    std::size_t ctorIndex = 0;
    for (auto& ctor : enumTy.GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder())) {
        llvm::DIType* enumClassType = CreateEnumClassType(enumTy, ctor, fwdDecl, ctorIndex);
        auto inheritType = createInheritance(fwdDecl, enumClassType, 0u, 0u, llvm::DINode::FlagZero);
        enumMembers.push_back(inheritType);
        ++ctorIndex;
    }
    return fwdDecl;
}

llvm::DICompositeType* DIBuilder::CreateEnumClassType(const CHIR::EnumType& enumTy,
    const CHIR::EnumCtorInfo& constructor, llvm::DICompositeType* subEnumType, std::size_t ctorIndex)
{
    CodeGenDIVector4 enumClassElements{};
    auto enumCtorlayoutType =
        CGType::GetOrCreateEnumCtorLayoutType(cgMod, enumTy, ctorIndex, constructor.funcType->GetParamTypes());
    CJC_ASSERT(!enumCtorlayoutType->isOpaque() && "The body is not set for enumCtorlayoutType.");
    uint64_t sizeofPointer = 64u;
    auto totalSize = GetTypeSize(enumCtorlayoutType) + sizeofPointer;
    auto enumDef = enumTy.GetEnumDef();
    auto& position = enumDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto layout = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(enumCtorlayoutType);
    auto name = GenerateCustomTypeName(enumTy, false);
    std::string ctorName = name + "_ctor_" + std::to_string(ctorIndex);
    auto enumClassType =
        createStructType(subEnumType, ctorName, diFile, 0u, totalSize, 0u, llvm::DINode::FlagZero, nullptr, {});

    auto constructorType = GetOrCreateEnumCtorType(enumTy);
    auto ctorType = createMemberType(
        enumClassType, "constructor", diFile, 0u, 32u, 0u, 64u, llvm::DINode::FlagZero, constructorType);
    enumClassElements.push_back(ctorType);

    // in ObjLayout, the first element is constructor.
    size_t argIndex = 1;
    for (auto argTy : constructor.funcType->GetParamTypes()) {
        auto argType = GetOrCreateType(*argTy);
        if (IsReferenceType(*argTy, cgMod) && !IsOption(*argTy)) {
            argType = CreatePointerType(argType, CreateRefType()->getSizeInBits());
        }
        auto offset = layout->getElementOffsetInBits(argIndex) + sizeofPointer;
        auto align = layout->getAlignment().value();
        auto argMember = createMemberType(enumClassType, "arg_" + std::to_string(argIndex), diFile, 0u,
            GetSizeInBits(argType), static_cast<uint32_t>(align), offset, llvm::DINode::FlagZero, argType);
        enumClassElements.push_back(argMember);
        ++argIndex;
    }
    replaceArrays(enumClassType, getOrCreateArray(enumClassElements));
    return enumClassType;
}
} // namespace CodeGen
} // namespace Cangjie
