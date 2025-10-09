// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/AST2CHIRChecker.h"

#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Value.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

namespace {
void Errorln(const std::string& info)
{
    std::cerr << "ast2chir checker error: " << info << std::endl;
}

bool CheckPrimitiveType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    const std::map<Type::TypeKind, Cangjie::AST::TypeKind> chir2astTy = {
        {Type::TypeKind::TYPE_INT8, Cangjie::AST::TypeKind::TYPE_INT8},
        {Type::TypeKind::TYPE_INT16, Cangjie::AST::TypeKind::TYPE_INT16},
        {Type::TypeKind::TYPE_INT32, Cangjie::AST::TypeKind::TYPE_INT32},
        {Type::TypeKind::TYPE_INT64, Cangjie::AST::TypeKind::TYPE_INT64},
        {Type::TypeKind::TYPE_INT_NATIVE, Cangjie::AST::TypeKind::TYPE_INT_NATIVE},
        {Type::TypeKind::TYPE_UINT8, Cangjie::AST::TypeKind::TYPE_UINT8},
        {Type::TypeKind::TYPE_UINT16, Cangjie::AST::TypeKind::TYPE_UINT16},
        {Type::TypeKind::TYPE_UINT32, Cangjie::AST::TypeKind::TYPE_UINT32},
        {Type::TypeKind::TYPE_UINT64, Cangjie::AST::TypeKind::TYPE_UINT64},
        {Type::TypeKind::TYPE_UINT_NATIVE, Cangjie::AST::TypeKind::TYPE_UINT_NATIVE},
        {Type::TypeKind::TYPE_FLOAT16, Cangjie::AST::TypeKind::TYPE_FLOAT16},
        {Type::TypeKind::TYPE_FLOAT32, Cangjie::AST::TypeKind::TYPE_FLOAT32},
        {Type::TypeKind::TYPE_FLOAT64, Cangjie::AST::TypeKind::TYPE_FLOAT64},
        {Type::TypeKind::TYPE_RUNE, Cangjie::AST::TypeKind::TYPE_RUNE},
        {Type::TypeKind::TYPE_BOOLEAN, Cangjie::AST::TypeKind::TYPE_BOOLEAN},
        {Type::TypeKind::TYPE_UNIT, Cangjie::AST::TypeKind::TYPE_UNIT},
        {Type::TypeKind::TYPE_NOTHING, Cangjie::AST::TypeKind::TYPE_NOTHING}};
    if (chir2astTy.count(chirTy.GetTypeKind()) == 0) {
        Cangjie::InternalError("unsupported type kind");
    }
    return chir2astTy.at(chirTy.GetTypeKind()) == astTy.kind;
}

bool CheckType(const Cangjie::AST::Ty& astTy, const Type& chirTy);

bool CheckTypeArgs(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    auto astTyArgs = astTy.typeArgs;
    auto chirTyArgs = chirTy.GetTypeArgs();
    if (astTyArgs.size() != chirTyArgs.size()) {
        return false;
    }
    for (size_t i = 0; i < astTyArgs.size(); ++i) {
        if (!CheckType(*astTyArgs[i], *chirTyArgs[i])) {
            return false;
        }
    }
    return true;
}

bool CheckTupleType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    auto astTyArgs = astTy.typeArgs;
    auto chirTyArgs = chirTy.GetTypeArgs();
    if (astTyArgs.size() != chirTyArgs.size()) {
        return false;
    }
    for (size_t loop = 0; loop < astTyArgs.size(); loop++) {
        if (!CheckType(*astTyArgs[loop], *chirTyArgs[loop])) {
            return false;
        }
    }
    return true;
}

bool CheckFuncType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    auto astTyArgs = astTy.typeArgs;
    auto chirTyArgs = chirTy.GetTypeArgs();
    if (astTyArgs.size() != chirTyArgs.size()) {
        return false;
    }
    for (size_t loop = 0; loop < astTyArgs.size(); loop++) {
        if (!CheckType(*astTyArgs[loop], *chirTyArgs[loop])) {
            return false;
        }
    }
    return true;
}

bool CheckMethodType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    auto astTyArgs = astTy.typeArgs;
    auto chirTyArgs = chirTy.GetTypeArgs();
    if (astTyArgs.size() + 1 != chirTyArgs.size()) {
        return false;
    }
    for (size_t loop = 0; loop < astTyArgs.size(); loop++) {
        if (!CheckType(*astTyArgs[loop], *chirTyArgs[loop + 1])) {
            return false;
        }
    }
    return true;
}

bool CheckStructType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    if (!astTy.IsStruct()) {
        return false;
    }
    auto astStruct = Cangjie::StaticCast<const Cangjie::AST::StructTy&>(astTy).declPtr;
    auto chirStruct = Cangjie::StaticCast<const StructType&>(chirTy).GetStructDef();
    if (astStruct->mangledName != chirStruct->GetIdentifierWithoutPrefix()) {
        return false;
    }
    return CheckTypeArgs(astTy, chirTy);
}

bool CheckClassType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    if (!astTy.IsClass()) {
        return false;
    }
    auto astClass = Cangjie::StaticCast<const Cangjie::AST::ClassTy&>(astTy).declPtr;
    auto chirClass = Cangjie::StaticCast<const ClassType&>(chirTy).GetClassDef();
    if (astClass->mangledName != chirClass->GetIdentifierWithoutPrefix()) {
        return false;
    }
    return CheckTypeArgs(astTy, chirTy);
}

bool CheckInterfaceType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    if (!astTy.IsInterface()) {
        return false;
    }
    auto astClass = Cangjie::StaticCast<const Cangjie::AST::InterfaceTy&>(astTy).declPtr;
    auto chirClass = Cangjie::StaticCast<const ClassType&>(chirTy).GetClassDef();
    if (astClass->mangledName != chirClass->GetIdentifierWithoutPrefix()) {
        return false;
    }
    return CheckTypeArgs(astTy, chirTy);
}

bool CheckEnumType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    if (!astTy.IsEnum()) {
        return false;
    }
    auto astEnum = Cangjie::StaticCast<const Cangjie::AST::EnumTy&>(astTy).declPtr;
    auto chirEnum = Cangjie::StaticCast<const EnumType&>(chirTy).GetEnumDef();
    if (astEnum->mangledName != chirEnum->GetIdentifierWithoutPrefix()) {
        return false;
    }
    return CheckTypeArgs(astTy, chirTy);
}

bool CheckRawArrayType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    if (!astTy.IsArray()) {
        return false;
    }
    auto astArray = Cangjie::StaticCast<const Cangjie::AST::ArrayTy&>(astTy);
    auto& chirArray = Cangjie::StaticCast<const RawArrayType&>(chirTy);
    return astArray.dims == chirArray.GetDims() && CheckType(*astTy.typeArgs[0], *chirTy.GetTypeArgs()[0]);
}

bool CheckVArrayType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    if (astTy.kind != Cangjie::AST::TypeKind::TYPE_VARRAY) {
        return false;
    }
    auto astArray = Cangjie::StaticCast<const Cangjie::AST::VArrayTy&>(astTy);
    auto& chirArray = Cangjie::StaticCast<const VArrayType&>(chirTy);
    return astArray.size == chirArray.GetSize() && CheckType(*astTy.typeArgs[0], *chirTy.GetTypeArgs()[0]);
}

bool CheckCPointerType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    if (!astTy.IsPointer()) {
        return false;
    }
    return CheckType(*astTy.typeArgs[0], *chirTy.GetTypeArgs()[0]);
}

bool CheckType(const Cangjie::AST::Ty& astTy, const Type& chirTy)
{
    if (chirTy.IsPrimitive()) {
        return CheckPrimitiveType(astTy, chirTy);
    } else if (chirTy.IsTuple()) {
        return CheckTupleType(astTy, chirTy);
    } else if (chirTy.IsFunc()) {
        return CheckFuncType(astTy, chirTy);
    } else if (chirTy.IsStruct()) {
        return CheckStructType(astTy, chirTy);
    } else if (chirTy.IsClass()) {
        if (StaticCast<const ClassType&>(chirTy).GetClassDef()->IsClass()) {
            return CheckClassType(astTy, chirTy);
        } else {
            return CheckInterfaceType(astTy, chirTy);
        }
    } else if (chirTy.IsEnum()) {
        return CheckEnumType(astTy, chirTy);
    } else if (chirTy.IsRawArray()) {
        return CheckRawArrayType(astTy, chirTy);
    } else if (chirTy.IsVArray()) {
        return CheckVArrayType(astTy, chirTy);
    } else if (chirTy.IsCPointer()) {
        return CheckCPointerType(astTy, chirTy);
    } else if (chirTy.IsCString()) {
        return astTy.IsCString();
    } else if (chirTy.IsRef()) {
        return CheckType(astTy, *chirTy.GetTypeArgs()[0]);
    }
    return true;
}

bool CheckClass(const Cangjie::AST::ClassDecl& decl, const ClassDef& classDef)
{
    if (!classDef.IsClass()) {
        Errorln(classDef.GetIdentifier() + " is expected to be a classDef.");
        return false;
    }
    AST::ClassTy* astSupClsTy = nullptr;
    for (auto& super : decl.inheritedTypes) {
        if (super->ty->kind == AST::TypeKind::TYPE_CLASS) {
            astSupClsTy = StaticCast<AST::ClassTy*>(super->ty);
        }
    }
    auto chirSupClsTy = classDef.GetSuperClassTy();
    // check super class type
    if (astSupClsTy != nullptr && chirSupClsTy != nullptr) {
        if (!CheckType(*astSupClsTy, *chirSupClsTy)) {
            Errorln(classDef.GetIdentifier() + " set wrong super class.");
            return false;
        }
    } else if (astSupClsTy != nullptr) {
        Errorln(classDef.GetIdentifier() + " not set super class.");
        return false;
    } else if (chirSupClsTy != nullptr) {
        Errorln(classDef.GetIdentifier() + " set redundant super class.");
        return false;
    }
    return true;
}

bool CheckInterface(const ClassDef& chirNode)
{
    if (!chirNode.IsInterface()) {
        Errorln(chirNode.GetIdentifier() + " is expected to be a interfaceDef.");
        return false;
    }
    return true;
}

const CustomTypeDef* GetParentCustomTypeDef(const Value& value)
{
    if (auto func = DynamicCast<FuncBase>(&value)) {
        return func->GetParentCustomTypeDef();
    } else if (auto var = DynamicCast<GlobalVarBase>(&value)) {
        return var->GetParentCustomTypeDef();
    }
    return nullptr;
}

bool CheckInheritDeclGlobalMember(
    const Cangjie::AST::Decl& decl, const CustomTypeDef& chirNode, const AST2CHIRNodeMap<Value>& globalCache)
{
    auto chirCache = globalCache.TryGet(decl);
    if (chirCache == nullptr && decl.TestAttr(Cangjie::AST::Attribute::COMMON)) {
        return true;
    }
    if (chirCache == nullptr) {
        Errorln("not find " + decl.mangledName + " translator in " + chirNode.GetIdentifier() + ".");
        return false;
    }
    // skip checking temporarily
    if (decl.IsConst() && decl.TestAttr(AST::Attribute::IMPORTED)) {
        return true;
    }
    // check chir node have a right declaredParent
    auto funcDecl = DynamicCast<const Cangjie::AST::FuncDecl*>(&decl);
    if (funcDecl == nullptr || !IsStaticInit(*funcDecl)) {
        if (auto def = GetParentCustomTypeDef(*chirCache); def != &chirNode) {
            Errorln("not find " + chirCache->GetIdentifier() + " in " + chirNode.GetIdentifier() + ".");
            return false;
        }
    }
    // member func
    if (decl.astKind == Cangjie::AST::ASTKind::FUNC_DECL) {
        if (!decl.TestAttr(Cangjie::AST::Attribute::STATIC) && !CheckMethodType(*decl.ty, *chirCache->GetType())) {
            Errorln(chirCache->GetIdentifier() + " is expected to be promoted " + Cangjie::AST::Ty::ToString(decl.ty) +
                ".");
            return false;
        }
        if (decl.TestAttr(Cangjie::AST::Attribute::STATIC) && !CheckFuncType(*decl.ty, *chirCache->GetType())) {
            Errorln(chirCache->GetIdentifier() + " is expected to be promoted " + Cangjie::AST::Ty::ToString(decl.ty) +
                ".");
            return false;
        }
    }
    return true;
}

bool CheckAbstractMethod(const Cangjie::AST::Decl& decl, const CustomTypeDef& chirNode)
{
    if (chirNode.GetCustomKind() != CustomDefKind::TYPE_CLASS) {
        return true;
    }
    if (decl.astKind == Cangjie::AST::ASTKind::PROP_DECL) {
        auto ret = true;
        auto& propDecl = Cangjie::StaticCast<Cangjie::AST::PropDecl&>(decl);
        for (auto& itp : propDecl.getters) {
            ret = CheckAbstractMethod(*itp, chirNode) && ret;
        }
        for (auto& itp : propDecl.setters) {
            ret = CheckAbstractMethod(*itp, chirNode) && ret;
        }
        return ret;
    }
    auto& classDef = Cangjie::StaticCast<const ClassDef&>(chirNode);
    for (auto& it : classDef.GetAbstractMethods()) {
        if (it.GetMangledName() != decl.mangledName + ".0") {
            continue;
        }
        auto res = true;
        if (it.TestAttr(Attribute::STATIC)) {
            res = CheckType(*decl.ty, *it.methodTy);
        } else {
            auto astTyArgs = decl.ty->typeArgs;
            auto chirTyArgs = it.methodTy->GetTypeArgs();
            if (astTyArgs.size() + 1 != chirTyArgs.size()) {
                res = false;
            } else {
                for (size_t i = 0; i < astTyArgs.size(); ++i) {
                    res = CheckType(*astTyArgs[i], *chirTyArgs[i + 1]) && res;
                }
            }
        }
        if (!res) {
            Errorln(it.GetMangledName() + " is expected to be " + Cangjie::AST::Ty::ToString(decl.ty) + " in " +
                chirNode.GetIdentifier() + ".");
            return false;
        }
        return true;
    }
    if (decl.platformImplementation && decl.platformImplementation->TestAttr(AST::Attribute::OPEN)) {
        // ABSTRACT COMMON was replaced with OPEN PLATFORM
        return true;
    }
    Errorln("not find abstract method " + decl.mangledName + " in " + chirNode.GetIdentifier() + ".");
    return false;
}

bool CheckLocalVar(const Cangjie::AST::Decl& decl, const CustomTypeDef& chirNode)
{
    auto localVars = chirNode.GetAllInstanceVars();
    if (chirNode.GetCustomKind() == CustomDefKind::TYPE_CLASS) {
        auto& classDef = static_cast<const ClassDef&>(chirNode);
        localVars = classDef.GetDirectInstanceVars();
    }
    for (auto& it : localVars) {
        if (it.name != decl.identifier.Val()) {
            continue;
        }
        if (!DynamicCast<AST::RefEnumTy*>(decl.ty) && !CheckType(*decl.ty, *it.type)) {
            Errorln(it.name + " is expected to be " + Cangjie::AST::Ty::ToString(decl.ty) + " in " +
                chirNode.GetIdentifier() + ".");
            return false;
        }
        return true;
    }
    Errorln("not find local var " + decl.mangledName + " in " + chirNode.GetIdentifier() + ".");
    return false;
}

bool CheckInheritDeclMembers(
    const Cangjie::AST::InheritableDecl& decl, const CustomTypeDef& chirNode, const AST2CHIRNodeMap<Value>& globalCache)
{
    auto ret = true;
    for (auto& it : decl.GetMemberDecls()) {
        // All of call to JArray constructors will be desugared, so we can skip the useless constructor member directly.
        if (it->TestAttr(Cangjie::AST::Attribute::GENERIC)) {
            continue;
        }
        // decl in Interface is abstract method.
        if (decl.astKind == Cangjie::AST::ASTKind::INTERFACE_DECL) {
            if (it->astKind != AST::ASTKind::VAR_DECL) {
                ret = CheckAbstractMethod(*it, chirNode) && ret;
            }
            continue;
        }
        // abstract method not have a real node
        if (it->TestAttr(Cangjie::AST::Attribute::ABSTRACT)) {
            ret = CheckAbstractMethod(*it, chirNode) && ret;
            continue;
        }
        if (decl.astKind == Cangjie::AST::ASTKind::INTERFACE_DECL && !it->TestAttr(Cangjie::AST::Attribute::STATIC) &&
            (it->astKind == Cangjie::AST::ASTKind::FUNC_DECL || it->astKind == Cangjie::AST::ASTKind::PROP_DECL)) {
            ret = CheckAbstractMethod(*it, chirNode) && ret;
            continue;
        }
        // local member var not have a real node
        if (it->astKind == Cangjie::AST::ASTKind::VAR_DECL && !it->TestAttr(Cangjie::AST::Attribute::STATIC)) {
            ret = CheckLocalVar(*it, chirNode) && ret;
            continue;
        }
        // primary ctor not have a cache
        if (it->astKind == Cangjie::AST::ASTKind::PRIMARY_CTOR_DECL) {
            continue;
        }
        if (it->astKind == Cangjie::AST::ASTKind::PROP_DECL) {
            auto& propDecl = Cangjie::StaticCast<Cangjie::AST::PropDecl&>(*it);
            for (auto& itp : propDecl.getters) {
                ret = CheckInheritDeclGlobalMember(*itp, chirNode, globalCache) && ret;
            }
            for (auto& itp : propDecl.setters) {
                ret = CheckInheritDeclGlobalMember(*itp, chirNode, globalCache) && ret;
            }
            continue;
        }
        // other all need a real node, contain method and static var.
        ret = CheckInheritDeclGlobalMember(*it, chirNode, globalCache) && ret;
    }
    return ret;
}

bool CheckClassLike(
    const Cangjie::AST::ClassLikeDecl& decl, const CustomTypeDef& chirNode, const AST2CHIRNodeMap<Value>& globalCache)
{
    if (chirNode.GetCustomKind() != CustomDefKind::TYPE_CLASS) {
        Errorln(chirNode.GetIdentifier() + " is expected to be a classDef/interfaceDef.");
        return false;
    }
    auto ret = true;
    auto& classDef = static_cast<const ClassDef&>(chirNode);
    // check super interface same
    auto astSupInter = decl.GetSuperInterfaceTys();
    auto chirSupClsInter = classDef.GetImplementedInterfaceDefs();
    if (astSupInter.size() != chirSupClsInter.size()) {
        Errorln(chirNode.GetIdentifier() + " set wrong super interfaces.");
        ret = false;
    }

    if (decl.astKind == Cangjie::AST::ASTKind::CLASS_DECL) {
        ret = CheckClass(static_cast<const Cangjie::AST::ClassDecl&>(decl), classDef) && ret;
    } else {
        ret = CheckInterface(classDef) && ret;
    }
    return CheckInheritDeclMembers(decl, chirNode, globalCache) && ret;
}

bool CheckEnum(
    const Cangjie::AST::EnumDecl& decl, const CustomTypeDef& chirNode, const AST2CHIRNodeMap<Value>& globalCache)
{
    if (chirNode.GetCustomKind() != CustomDefKind::TYPE_ENUM) {
        Errorln(chirNode.GetIdentifier() + " is expected to be a enumDef.");
        return false;
    }
    return CheckInheritDeclMembers(decl, chirNode, globalCache);
}

bool CheckStruct(
    const Cangjie::AST::StructDecl& decl, const CustomTypeDef& chirNode, const AST2CHIRNodeMap<Value>& globalCache)
{
    if (chirNode.GetCustomKind() != CustomDefKind::TYPE_STRUCT) {
        Errorln(chirNode.GetIdentifier() + " is expected to be a structDef.");
        return false;
    }
    return CheckInheritDeclMembers(decl, chirNode, globalCache);
}

bool CheckFunc(const Cangjie::AST::FuncDecl& decl, const Value& chirNode)
{
    if (!Is<FuncBase>(chirNode)) {
        Errorln(chirNode.GetIdentifier() + " is expected to be a func.");
        return false;
    }
    // not check member method, it will check in customDef
    if (decl.outerDecl != nullptr) {
        return true;
    }
    auto astTy = decl.ty;
    auto chirTy = chirNode.GetType();
    if (!CheckType(*astTy, *chirTy)) {
        Errorln(chirNode.GetIdentifier() + " is expected to be " + Cangjie::AST::Ty::ToString(astTy) + ".");
        return false;
    }
    return true;
}

bool CheckVar(const Cangjie::AST::VarDecl& decl, const Value& chirNode)
{
    if (!Is<GlobalVarBase>(chirNode)) {
        Errorln(chirNode.GetIdentifier() + " is expected to be a globalVar.");
        return false;
    }
    auto astTy = decl.ty;
    auto chirTy = chirNode.GetType();
    if (!CheckType(*astTy, *chirTy)) {
        Errorln(chirNode.GetIdentifier() + " is expected to be " + Cangjie::AST::Ty::ToString(astTy) + ".");
        return false;
    }
    return true;
}

/// Search for vtable method duplication.
//  O(n^2) is okay because of average table size
inline FuncBase* CheckVtableCopy(const std::vector<VirtualFuncInfo>& tableMethods)
{
    auto size = tableMethods.size();

    for (size_t i = 0; i < size; i++) {
        auto instance1 = tableMethods[i].instance;

        for (size_t j = i + 1; j < size; j++) {
            auto instance2 = tableMethods[j].instance;

            if (instance1 == instance2 && tableMethods[i].srcCodeIdentifier == tableMethods[j].srcCodeIdentifier) {
                return instance1;
            }
        }
    }

    return nullptr;
}

bool CheckVtableHasNoDuplicates(const CustomTypeDef& customTypeDef)
{
    for (auto& [srcTy, tableMethods] : customTypeDef.GetVTable()) {
        const auto duplicate = CheckVtableCopy(tableMethods);
        if (duplicate) {
            Errorln(customTypeDef.GetIdentifier() + " has method duplication in vtable: '"
                + duplicate->GetSrcCodeIdentifier() + "'.");

            return false;
        }
    }

    return true;
}
} // namespace

namespace Cangjie::CHIR {
bool AST2CHIRCheckCustomTypeDef(
    const AST::Node& astNode, const CustomTypeDef& chirNode, const AST2CHIRNodeMap<Value>& globalCache)
{
    if (!astNode.IsNominalDecl()) {
        InternalError("unsupported decl");
        return false;
    }
    auto ret = true;
    auto& decl = static_cast<const AST::Decl&>(astNode);
    if (decl.identifier != chirNode.GetSrcCodeIdentifier()) {
        Errorln(chirNode.GetIdentifier() + "'srcIdentifier is expected to be " + decl.identifier + ".");
        ret = false;
    }
    if (decl.mangledName != chirNode.GetIdentifierWithoutPrefix()) {
        Errorln(chirNode.GetIdentifier() + "'identifier is expected to be " + decl.mangledName + ".");
        ret = false;
    }

    // NOTE: measurements are accumulated
    Utils::ProfileRecorder::Start("AST to CHIR Translation", "Checking VTable duplication");
    CheckVtableHasNoDuplicates(chirNode);
    Utils::ProfileRecorder::Stop("AST to CHIR Translation", "Checking VTable duplication");
    if (astNode.astKind == AST::ASTKind::CLASS_DECL || astNode.astKind == AST::ASTKind::INTERFACE_DECL) {
        return CheckClassLike(static_cast<const AST::ClassLikeDecl&>(decl), chirNode, globalCache) && ret;
    } else if (astNode.astKind == AST::ASTKind::ENUM_DECL) {
        return CheckEnum(static_cast<const AST::EnumDecl&>(decl), chirNode, globalCache) && ret;
    } else if (astNode.astKind == AST::ASTKind::STRUCT_DECL) {
        return CheckStruct(static_cast<const AST::StructDecl&>(decl), chirNode, globalCache) && ret;
    }
    return ret;
}

bool AST2CHIRCheckValue(const AST::Node& astNode, const Value& chirNode)
{
    if (!astNode.IsDecl()) {
        return true;
    }
    auto& decl = static_cast<const AST::Decl&>(astNode);
    if (astNode.astKind == AST::ASTKind::VAR_DECL) {
        return CheckVar(static_cast<const AST::VarDecl&>(decl), chirNode);
    } else if (astNode.astKind == AST::ASTKind::FUNC_DECL) {
        return CheckFunc(static_cast<const AST::FuncDecl&>(decl), chirNode);
    }
    return true;
}
} // namespace Cangjie::CHIR
