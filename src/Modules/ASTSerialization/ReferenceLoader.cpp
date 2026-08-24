// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements the AST Loader related classes.
 */

#include "ASTLoaderImpl.h"

#include "ASTSerializeUtils.h"

#include "flatbuffers/ModuleFormat_generated.h"

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/CheckUtils.h"
#include "flatbuffers/NodeFormat_generated.h"

using namespace Cangjie;
using namespace AST;

namespace {
void SetFuncParentReference(const FuncDecl& fd, Decl& parentDecl)
{
    CJC_ASSERT(fd.funcBody);
    if (parentDecl.IsClassLikeDecl()) {
        fd.funcBody->parentClassLike = RawStaticCast<ClassLikeDecl*>(&parentDecl);
    } else if (parentDecl.astKind == ASTKind::STRUCT_DECL) {
        fd.funcBody->parentStruct = RawStaticCast<StructDecl*>(&parentDecl);
    } else if (parentDecl.astKind == ASTKind::ENUM_DECL) {
        fd.funcBody->parentEnum = RawStaticCast<EnumDecl*>(&parentDecl);
    }
}

void SetMemberDeclReference(Decl& member, Decl& parentDecl)
{
    auto parentTypeDecl = &parentDecl;
    if (parentDecl.astKind == ASTKind::EXTEND_DECL) {
        if (auto extendedDecl = Ty::GetDeclPtrOfTy(parentDecl.GetTy())) {
            parentTypeDecl = extendedDecl;
        }
    }
    if (auto propDecl = DynamicCast<PropDecl*>(&member)) {
        for (auto& get : propDecl->getters) {
            SetFuncParentReference(*get, *parentTypeDecl);
        }
        for (auto& set : propDecl->setters) {
            SetFuncParentReference(*set, *parentTypeDecl);
        }
    } else if (auto func = DynamicCast<FuncDecl*>(&member)) {
        SetFuncParentReference(*func, *parentTypeDecl);
        if (func->funcBody->paramLists.empty()) {
            return;
        }
        for (auto& param : func->funcBody->paramLists[0]->params) {
            if (param->desugarDecl) {
                SetFuncParentReference(*param->desugarDecl, *parentTypeDecl);
            }
        }
    }
}

OwnedPtr<Type> WrapType(ModalTy ty);

ASTMode LocalModalToAST(Mode modal)
{
    switch (modal) {
        case Mode::NOT:
            return ASTMode::NOT;
        case Mode::HALF:
            return ASTMode::HALF;
        case Mode::FULL:
            return ASTMode::FULL;
        default:
            CJC_ABORT_WITH_MSG("Unhandled local modal.");
            return {};
    }
}

/// Wrap the modal info to the type node.
void WrapTyMode(Type& typeNode, ModalTy ty)
{
    if (ty.IsDataType()) {
        return;
    }
    typeNode.modal.SetLocal(LocalModalToAST(ty.Mode().local), DEFAULT_POSITION);
}

OwnedPtr<RefType> Wrap2RefType(ModalTy ty)
{
    const std::unordered_set<TypeKind> CAN_BE_REF = {
        TypeKind::TYPE_ENUM,
        TypeKind::TYPE_STRUCT,
        TypeKind::TYPE_ARRAY,
        TypeKind::TYPE_VARRAY,
        TypeKind::TYPE_POINTER,
        TypeKind::TYPE_CSTRING,
        TypeKind::TYPE_CLASS,
        TypeKind::TYPE_INTERFACE,
        TypeKind::TYPE,
        TypeKind::TYPE_GENERICS,
        TypeKind::TYPE_NOTHING,
        // Invalid type can also be wrapped to RefType to avoid null pointer.
        TypeKind::TYPE_INVALID,
    };
    if (!Utils::In(ty->kind, CAN_BE_REF)) {
        return nullptr;
    }
    bool hasTypeAlias = ty->kind == TypeKind::TYPE;
    auto refType = MakeOwned<RefType>();
    for (auto typeArg : ty->typeArgs) {
        refType->typeArguments.emplace_back(WrapType(typeArg));
        hasTypeAlias = hasTypeAlias || !Ty::IsInitialTy(refType->typeArguments.back()->aliasTy);
    }
    // The refType's ty of TypeAlias reference will be updated after loading all type aliases.
    refType->SetTy(ty);
    refType->ref.identifier = ty->name;
    if (auto decl = Ty::GetDeclPtrOfTy(ty)) {
        refType->ref.target = decl;
        refType->ref.identifier =
            Is<ClassThisTy>(*refType->DataTy()) ? "This" : decl->identifier.Val();
    }
    if (hasTypeAlias) {
        refType->aliasTy = ty.Ty();
    }
    WrapTyMode(*refType, ty);
    refType->EnableAttr(Attribute::IMPORTED, Attribute::COMPILER_ADD, Attribute::IS_CHECK_VISITED);
    return refType;
}

/**
 * Wrap a semantic type to an AST type node. TypeAliasTy will create a alias type reference.
 * @param ty The semantic type.
 * @return The wrapped AST type node.
 */
OwnedPtr<Type> WrapType(ModalTy ty)
{
    if (ty == nullptr) {
        return nullptr;
    }
    bool hasTypeAlias = false;
    if (ty->kind <= TypeKind::TYPE_BOOLEAN && ty->kind != TypeKind::TYPE_NOTHING) {
        auto pt = MakeOwned<PrimitiveType>();
        pt->str = ty->String();
        pt->SetTy(ty);
        WrapTyMode(*pt, ty);
        pt->EnableAttr(Attribute::IMPORTED, Attribute::COMPILER_ADD, Attribute::IS_CHECK_VISITED);
        return pt;
    } else if (ty->kind == TypeKind::TYPE_FUNC) {
        auto funcType = MakeOwned<FuncType>();
        auto& funcTy = StaticCast<FuncTy&>(*ty.Ty());
        for (auto param : funcTy.paramTys) {
            funcType->paramTypes.emplace_back(WrapType(param));
            hasTypeAlias = hasTypeAlias || !Ty::IsInitialTy(funcType->paramTypes.back()->aliasTy);
        }
        funcType->SetTy(ty);
        funcType->retType = WrapType(funcTy.retTy);
        hasTypeAlias = hasTypeAlias || !Ty::IsInitialTy(funcType->retType->aliasTy);
        WrapTyMode(*funcType, ty);
        funcType->EnableAttr(Attribute::IMPORTED, Attribute::COMPILER_ADD, Attribute::IS_CHECK_VISITED);
        if (hasTypeAlias) {
            funcType->aliasTy = ty.Ty();
        }
        return funcType;
    } else if (ty->kind == TypeKind::TYPE_TUPLE) {
        auto tupleType = MakeOwned<TupleType>();
        auto& tupleTy = StaticCast<TupleTy&>(*ty.Ty());
        for (auto typeArg : tupleTy.typeArgs) {
            tupleType->fieldTypes.emplace_back(WrapType(typeArg));
            hasTypeAlias = hasTypeAlias || !Ty::IsInitialTy(tupleType->fieldTypes.back()->aliasTy);
        }
        tupleType->SetTy(ty);
        WrapTyMode(*tupleType, ty);
        tupleType->EnableAttr(Attribute::IMPORTED, Attribute::COMPILER_ADD, Attribute::IS_CHECK_VISITED);
        if (hasTypeAlias) {
            tupleType->aliasTy = ty.Ty();
        }
        return tupleType;
    }
    return Wrap2RefType(ty);
}

void UpdateType(OwnedPtr<Type>& astType, OwnedPtr<Type> loadedType)
{
    CJC_NULLPTR_CHECK(loadedType);
    if (astType == nullptr) {
        astType = std::move(loadedType);
    } else if (loadedType->GetTy().IsCorrect()) {
        // If type is invalid, it means the type decl is need to be recompiled, do not load type cache further.
        astType->SetTy(loadedType->GetTy());
        astType->EnableAttr(Attribute::IS_CHECK_VISITED);
        if (auto rt = DynamicCast<RefType*>(astType.get())) {
            rt->ref.target = loadedType->GetTarget();
        } else if (auto qt = DynamicCast<QualifiedType*>(astType.get())) {
            qt->target = loadedType->GetTarget();
        }
    }
}

void PostLoadReference(Expr& expr)
{
    switch (expr.astKind) {
        case ASTKind::CALL_EXPR: {
            auto& ce = StaticCast<CallExpr&>(expr);
            // Only copy expr's ty to arg's ty for 'ArrayExpr' & 'PointerExpr',
            // Type of 'CallExpr' 's argTy may be different from argExprTy when 'withInout' is true,
            // it is loaded during 'LoadSubNodeRefs'.
            CJC_NULLPTR_CHECK(ce.baseFunc);
            ce.resolvedFunction = DynamicCast<FuncDecl*>(ce.baseFunc->GetTarget());
            break;
        }
        case ASTKind::ARRAY_EXPR:
            for (auto& arg : StaticCast<ArrayExpr&>(expr).args) {
                CJC_NULLPTR_CHECK(arg->expr);
                arg->SetTy(arg->expr->GetTy());
            }
            break;
        case ASTKind::POINTER_EXPR: {
            auto& arg = StaticCast<PointerExpr&>(expr).arg;
            if (arg) {
                CJC_NULLPTR_CHECK(arg->expr);
                arg->SetTy(arg->expr->GetTy());
            }
            break;
        }
        default:
            break;
    }
}
} // namespace

DataTy ASTLoader::LoadType(FormattedIndex type) const
{
    CJC_NULLPTR_CHECK(pImpl);
    return pImpl->LoadType(type);
}

void ASTLoader::SetIsChirNow(bool isChirNow)
{
    CJC_NULLPTR_CHECK(pImpl);
    pImpl->isChirNow = isChirNow;
}

void ASTLoader::ASTLoaderImpl::InitializeTypeLoader()
{
    tyLoaderMap = {
        {PackageFormat::TypeKind_CPointer, &ASTLoaderImpl::SetTypeTy<PointerTy>},
        {PackageFormat::TypeKind_Array, &ASTLoaderImpl::SetTypeTy<ArrayTy>},
        {PackageFormat::TypeKind_VArray, &ASTLoaderImpl::SetTypeTy<VArrayTy>},
        {PackageFormat::TypeKind_CString, &ASTLoaderImpl::SetTypeTy<CStringTy>},
        {PackageFormat::TypeKind_Struct, &ASTLoaderImpl::SetTypeTy<StructTy, StructDecl>},
        {PackageFormat::TypeKind_Enum, &ASTLoaderImpl::SetTypeTy<EnumTy, EnumDecl>},
        {PackageFormat::TypeKind_Interface, &ASTLoaderImpl::SetTypeTy<InterfaceTy, InterfaceDecl>},
        {PackageFormat::TypeKind_Class, &ASTLoaderImpl::SetTypeTy<ClassTy, ClassDecl>},
        {PackageFormat::TypeKind_Type, &ASTLoaderImpl::SetTypeTy<TypeAliasTy, TypeAliasDecl>},
        {PackageFormat::TypeKind_Tuple, &ASTLoaderImpl::SetTypeTy<TupleTy>},
        {PackageFormat::TypeKind_Func, &ASTLoaderImpl::SetTypeTy<FuncTy>},
        {PackageFormat::TypeKind_Generic, &ASTLoaderImpl::SetGenericTy},
    };
}

void ASTLoader::LoadRefs() const
{
    CJC_NULLPTR_CHECK(pImpl);
    pImpl->LoadRefs();
}

void ASTLoader::ASTLoaderImpl::LoadRefs()
{
    for (auto [index, decl] : allLoadedDecls) {
        auto declObj = GetFormatDeclByIndex(static_cast<FormattedIndex>(index));
        CJC_NULLPTR_CHECK(declObj);
        CJC_NULLPTR_CHECK(decl);
        LoadDeclRefs(*declObj, *decl);
        LoadDeclDependencies(*declObj, *decl);
    }
    for (auto [index, expr] : allLoadedExprs) {
        auto exprObj = GetFormatExprByIndex(static_cast<FormattedIndex>(index));
        CJC_NULLPTR_CHECK(exprObj);
        CJC_NULLPTR_CHECK(expr);
        LoadExprRefs(*exprObj, *expr);
    }
    // Set type for created dummy expressions.
    for (auto dummyExpr : allDummyExprs) {
        CJC_ASSERT(dummyExpr->desugarExpr);
        dummyExpr->SetTy(dummyExpr->desugarExpr->GetTy());
    }
    // NOTE: funcArg expr's type reference is loaded after funcArg itself,
    // so we need to load for arg's ty after finish loading all reference for expression.
    // Also callExpr is loading before call base expr, loading possibly 'resolvedFunction' here.
    for (auto [_, expr] : allLoadedExprs) {
        PostLoadReference(*expr);
    }
}

// Load shape-only ty from SemaTy table; modal is loaded separately from parallel Mode fields.
DataTy ASTLoader::ASTLoaderImpl::LoadType(FormattedIndex type)
{
    if (type == INVALID_FORMAT_INDEX) {
        return TypeManager::GetInvalidTy();
    }
    // NOTE: serialized index is real table offset plus 1.
    auto index = type - 1;
    if (auto ty = allTypes[index]; ty) {
        return ty;
    }
    auto typeObj = package->allTypes()->Get(static_cast<uoffset_t>(index));
    if (GetPrimitiveTy(index, typeObj)) {
        return allTypes[index];
    }
    auto tyHandler = tyLoaderMap.find(typeObj->kind());
    if (tyHandler != tyLoaderMap.end()) {
        tyHandler->second(this, index, *typeObj);
    } else {
        allTypes[index] = TypeManager::GetInvalidTy();
    }
    if (!allTypes[index]) {
        allTypes[index] = TypeManager::GetInvalidTy();
    }
    return allTypes[index];
}

std::vector<ModalTy> ASTLoader::ASTLoaderImpl::LoadFuncSemaTyParamTypes(const PackageFormat::SemaTy& typeObj)
{
    auto info = typeObj.info_as_FuncTyInfo();
    CJC_NULLPTR_CHECK(info);
    CJC_NULLPTR_CHECK(typeObj.typeArgs());
    const auto* modesVec = info->typeArgsMode();
    std::vector<ModalTy> params;
    const auto len = static_cast<uoffset_t>(typeObj.typeArgs()->size());
    for (uoffset_t i = 0; i < len; ++i) {
        const PackageFormat::Mode* modePtr =
            (modesVec != nullptr && i < modesVec->size()) ? modesVec->Get(i) : nullptr;
        params.emplace_back(ApplyLoadedModal(
            LoadType(static_cast<FormattedIndex>(typeObj.typeArgs()->Get(i))), modePtr));
    }
    return params;
}

ModalTy ASTLoader::ASTLoaderImpl::LoadFuncSemaTyRetType(const PackageFormat::SemaTy& typeObj)
{
    auto info = typeObj.info_as_FuncTyInfo();
    CJC_NULLPTR_CHECK(info);
    return ApplyLoadedModal(LoadType(static_cast<FormattedIndex>(info->retType())), info->retTyMode());
}

bool ASTLoader::ASTLoaderImpl::GetPrimitiveTy(FormattedIndex type, const PackageFormat::SemaTy* typeObj)
{
    if (typeObj && typeObj->kind() >= PackageFormat::TypeKind_Unit &&
        typeObj->kind() <= PackageFormat::TypeKind_Bool) {
        auto astKind = GetASTTypeKind(typeObj->kind());
        CJC_ASSERT(astKind != TypeKind::TYPE_INVALID);
        allTypes[type] = TypeManager::GetPrimitiveTy(astKind);
        return true;
    }
    return false;
}

void ASTLoader::ASTLoaderImpl::SetGenericTy(FormattedIndex type, const PackageFormat::SemaTy& typeObj)
{
    auto info = typeObj.info_as_GenericTyInfo();
    CJC_NULLPTR_CHECK(info);
    auto decl = GetDeclFromIndex(info->declPtr());
    if (decl == nullptr) {
        allTypes[type] = TypeManager::GetInvalidTy();
        return; // For invalid indirect importing, this may be true.
    }
    auto gpd = StaticCast<GenericParamDecl*>(decl);
    auto t = typeManager.GetGenericsTy(*gpd);
    allTypes[type] = t;

    if (isLoadCache) {
        return; // Do not load upper bounds cache for generic type.
    }
    if (auto gTy = DynamicCast<GenericsTy*>(t)) {
        CJC_NULLPTR_CHECK(info->upperBounds());
        auto length = info->upperBounds()->size();
        for (uoffset_t i = 0; i < length; i++) {
            gTy->upperBounds.emplace(LoadType(info->upperBounds()->Get(i)));
        }
        CJC_NULLPTR_CHECK(gpd->outerDecl);
        auto generic = gpd->outerDecl->GetGeneric();
        CJC_NULLPTR_CHECK(generic);
        std::set<ModalTy> upperModal;
        for (const auto& ub : gTy->upperBounds) {
            upperModal.insert(ModalTy{ub});
        }
        generic->assumptionCollection.emplace(gTy, upperModal);
    }
}

std::vector<DataTy> ASTLoader::ASTLoaderImpl::LoadTypeArgs(const PackageFormat::SemaTy& typeObj)
{
    std::vector<DataTy> typeArgs;
    CJC_NULLPTR_CHECK(typeObj.typeArgs());
    auto length = static_cast<uoffset_t>(typeObj.typeArgs()->size());
    for (uoffset_t i = 0; i < length; i++) {
        typeArgs.emplace_back(LoadType(typeObj.typeArgs()->Get(i)));
    }
    return typeArgs;
}

template <typename TypeT, typename TypeDecl>
void ASTLoader::ASTLoaderImpl::SetTypeTy(FormattedIndex type, const PackageFormat::SemaTy& typeObj)
{
    DataTy ty = TypeManager::GetInvalidTy();
    if constexpr (std::is_same_v<TypeT, CStringTy>) {
        ty = TypeManager::GetCStringTy();
    } else if constexpr (std::is_same_v<TypeT, PointerTy>) {
        CJC_ASSERT(typeObj.typeArgs() && typeObj.typeArgs()->size() == 1u);
        ty = typeManager.GetPointerTy(LoadType(typeObj.typeArgs()->Get(0)));
    } else if constexpr (std::is_same_v<TypeT, ArrayTy>) {
        auto info = typeObj.info_as_ArrayTyInfo();
        CJC_NULLPTR_CHECK(info);
        unsigned int dims = static_cast<unsigned int>(info->dimsOrSize());
        CJC_ASSERT(typeObj.typeArgs() && typeObj.typeArgs()->size() == 1u);
        ty = typeManager.GetArrayTy(LoadType(typeObj.typeArgs()->Get(0)), dims);
    } else if constexpr (std::is_same_v<TypeT, VArrayTy>) {
        auto info = typeObj.info_as_ArrayTyInfo();
        CJC_NULLPTR_CHECK(info);
        CJC_ASSERT(typeObj.typeArgs() && typeObj.typeArgs()->size() == 1u);
        ty = typeManager.GetVArrayTy(*LoadType(typeObj.typeArgs()->Get(0)), info->dimsOrSize());
    } else if constexpr (std::is_same_v<TypeT, TupleTy>) {
        ty = typeManager.GetTupleTy(LoadTypeArgs(typeObj), false);
    } else if constexpr (std::is_same_v<TypeT, FuncTy>) {
        auto params = LoadFuncSemaTyParamTypes(typeObj);
        auto retTy = LoadFuncSemaTyRetType(typeObj);
        auto finfo = typeObj.info_as_FuncTyInfo();
        CJC_NULLPTR_CHECK(finfo);
        ty = typeManager.GetFunctionTy(
            std::move(params), std::move(retTy), {finfo->isC(), false, finfo->hasVariableLenArg()});
    } else {
        auto info = typeObj.info_as_CompositeTyInfo();
        CJC_NULLPTR_CHECK(info);
        auto typeDecl = DynamicCast<TypeDecl*>(GetDeclFromIndex(info->declPtr()));
        if (typeDecl == nullptr) {
            allTypes[type] = ty;
            return;
        }
        auto typeArgs = LoadTypeArgs(typeObj);
        if constexpr (std::is_same_v<TypeT, TypeAliasTy>) {
            ty = typeManager.GetTypeAliasTy(*typeDecl, typeArgs);
        } else if constexpr (std::is_same_v<TypeT, ClassTy>) {
            ty = info->isThisTy() ? typeManager.GetClassThisTy(*typeDecl, typeArgs)
                                  : typeManager.GetClassTy(*typeDecl, typeArgs);
        } else if constexpr (std::is_same_v<TypeT, InterfaceTy>) {
            ty = typeManager.GetInterfaceTy(*typeDecl, typeArgs);
        } else if constexpr (std::is_same_v<TypeT, StructTy>) {
            ty = typeManager.GetStructTy(*typeDecl, typeArgs);
        } else if constexpr (std::is_same_v<TypeT, EnumTy>) {
            ty = typeManager.GetEnumTy(*typeDecl, typeArgs);
        } else {
            static_assert(std::is_same_v<TypeT, Ty>);
        }
    }
    allTypes[type] = ty;
}

void ASTLoader::ASTLoaderImpl::LoadInheritedTypes(const PackageFormat::Decl& decl, InheritableDecl& id)
{
    // Super class type or super interface types.
    auto inheritedTypes = id.astKind == ASTKind::CLASS_DECL ? decl.info_as_ClassInfo()->inheritedTypes()
        : id.astKind == ASTKind::INTERFACE_DECL             ? decl.info_as_InterfaceInfo()->inheritedTypes()
        : id.astKind == ASTKind::ENUM_DECL                  ? decl.info_as_EnumInfo()->inheritedTypes()
        : id.astKind == ASTKind::STRUCT_DECL                ? decl.info_as_StructInfo()->inheritedTypes()
        : id.astKind == ASTKind::EXTEND_DECL                ? decl.info_as_ExtendInfo()->inheritedTypes()
                                                            : nullptr;
    CJC_NULLPTR_CHECK(inheritedTypes);
    auto length = static_cast<uoffset_t>(inheritedTypes->size());
    std::vector<OwnedPtr<Type>> loadedTypes;
    for (uoffset_t i = 0; i < length; i++) {
        auto index = inheritedTypes->Get(i);
        if (index != INVALID_FORMAT_INDEX) {
            auto type = WrapType(ModalTy{LoadType(index)});
            if (!type || !type->GetTy().IsCorrect()) {
                if (isLoadCache) {
                    return; // If any invalid type existed, do not load type cache for current 'id'.
                }
                continue;
            }
            if (auto classLikeTy = DynamicCast<ClassLikeTy>(type->DataTy())) {
                (void)classLikeTy->directSubtypes.emplace(id.DataTy());
            }
            loadedTypes.emplace_back(std::move(type));
        }
    }
    id.inheritedTypes.resize(loadedTypes.size());
    for (size_t i = 0; i < loadedTypes.size(); ++i) {
        UpdateType(id.inheritedTypes[i], std::move(loadedTypes[i]));
    }
}

void ASTLoader::ASTLoaderImpl::LoadGenericConstraintsRef(
    const PackageFormat::Generic* genericRef, Ptr<Generic> generic)
{
    // Do not load constraints if the generic is incremental compiled node.
    if (generic == nullptr || genericRef == nullptr || generic->TestAttr(Attribute::INCRE_COMPILE)) {
        return;
    }
    uoffset_t length = genericRef->constraints()->size();
    for (uoffset_t i = 0; i < length; i++) {
        auto vConstraint = genericRef->constraints()->Get(i);
        auto constraint = CreateAndLoadBasicInfo<GenericConstraint>(*vConstraint, INVALID_FORMAT_INDEX);
        constraint->type = Wrap2RefType(ModalTy{LoadType(vConstraint->type())});
        if (vConstraint->uppers()) {
            auto upperSize = vConstraint->uppers()->size();
            for (uoffset_t j = 0; j < upperSize; j++) {
                constraint->upperBounds.emplace_back(
                    WrapType(ModalTy{LoadType(vConstraint->uppers()->Get(j))}));
            }
        }
        constraint->isImplicitlyIntroduced = vConstraint->isImplicitlyIntroduced();
        generic->genericConstraints.emplace_back(std::move(constraint));
    }
}

void ASTLoader::ASTLoaderImpl::LoadAnnotationBaseExpr(const PackageFormat::Anno& rawAnno, AST::Annotation& anno)
{
    if (anno.kind != AST::AnnotationKind::CUSTOM) {
        return;
    }
    auto targetCtr = GetDeclFromIndex(rawAnno.target());
    if (targetCtr) {
        auto re = MakeOwned<RefExpr>();
        re->ref.target = targetCtr;
        re->ref.identifier = targetCtr->identifier;
        re->SetTy(targetCtr->GetTy());
        anno.baseExpr = std::move(re);
    }
}

template <typename DeclT>
void ASTLoader::ASTLoaderImpl::LoadNominalDeclRef(const PackageFormat::Decl& decl, DeclT& astDecl)
{
    if constexpr (std::is_base_of<InheritableDecl, DeclT>::value) {
        for (auto& member : astDecl.GetMemberDeclPtrs()) {
            SetMemberDeclReference(*member, astDecl);
        }
        // For incremental compilation, when 'id' is changed, the 'inheritedTypes' may be changed and cannot be loaded.
        // But type for original decl and reference relation can still be loaded.
        if (!astDecl.toBeCompiled) {
            LoadInheritedTypes(decl, astDecl);
        }
    } else if constexpr (std::is_same_v<DeclT, FuncDecl>) {
        // Do not load 'retType' node for constructor which is not inside generic type decl.
        // Generic type decl's constructor will have funcBody content.
        // NOTE: for cjlint check -- 'DataflowRuleGFIO01Check::IsFileInit' requires 'retType' not exist.
        bool nonGenericCtor = astDecl.TestAttr(Attribute::CONSTRUCTOR) && !astDecl.funcBody->body;
        if (!nonGenericCtor) {
            auto info = decl.info_as_FuncInfo();
            CJC_NULLPTR_CHECK(info);
            auto fb = info->funcBody();
            CJC_NULLPTR_CHECK(fb);
            auto retTy = ApplyLoadedModal(LoadType(static_cast<FormattedIndex>(fb->retType())), fb->retTyMode());
            auto type = WrapType(retTy);
            UpdateType(astDecl.funcBody->retType, std::move(type));
        }
        astDecl.funcBody->SetTy(astDecl.GetTy());
    }
    if constexpr (std::is_same_v<DeclT, ExtendDecl>) {
        UpdateType(astDecl.extendedType, WrapType(astDecl.GetTy()));
    }
    if (astDecl.TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        astDecl.genericDecl = GetDeclFromIndex(decl.genericDecl());
    }
}

void ASTLoader::ASTLoaderImpl::LoadTypeAliasDeclRef(const PackageFormat::Decl& decl, TypeAliasDecl& tad)
{
    auto info = decl.info_as_AliasInfo();
    CJC_NULLPTR_CHECK(info);
    tad.type = WrapType(ModalTy{LoadType(info->aliasedTy())});
}

void ASTLoader::ASTLoaderImpl::LoadDeclDependencies(const PackageFormat::Decl& decl, Decl& astDecl)
{
    auto rawDeps = decl.dependencies();
    if (deserializingCommon && rawDeps) {
        auto length = static_cast<uoffset_t>(rawDeps->size());
        for (uoffset_t i = 0; i < length; i++) {
            auto index = rawDeps->Get(i);
            auto dependency = GetDeclFromIndex(index);
            astDecl.dependencies.emplace_back(dependency);
        }
    }
}

void ASTLoader::ASTLoaderImpl::LoadDeclRefs(const PackageFormat::Decl& declObj, Decl& decl)
{
    // Load ty for all decls and try to load generic constraints.
    decl.SetTy(ApplyLoadedModal(LoadType(static_cast<FormattedIndex>(declObj.type())), declObj.tyMode()));
    if (!Ty::IsTyCorrect(decl.GetTy()) && isLoadCache) {
        return; // Skip following steps if ty is not correct during loading cache.
    }
    LoadGenericConstraintsRef(declObj.generic(), decl.GetGeneric());
    for (uoffset_t i = 0; i < declObj.annotations()->size(); i++) {
        CJC_NULLPTR_CHECK(declObj.annotations()->Get(i));
        CJC_NULLPTR_CHECK(decl.annotations[i]);
        LoadAnnotationBaseExpr(*declObj.annotations()->Get(i), *decl.annotations[i]);
    }
    switch (declObj.kind()) {
        case PackageFormat::DeclKind_FuncDecl: {
            LoadNominalDeclRef(declObj, StaticCast<FuncDecl&>(decl));
            break;
        }
        case PackageFormat::DeclKind_StructDecl: {
            LoadNominalDeclRef(declObj, StaticCast<StructDecl&>(decl));
            break;
        }
        case PackageFormat::DeclKind_EnumDecl: {
            LoadNominalDeclRef(declObj, StaticCast<EnumDecl&>(decl));
            break;
        }
        case PackageFormat::DeclKind_InterfaceDecl: {
            LoadNominalDeclRef(declObj, StaticCast<InterfaceDecl&>(decl));
            break;
        }
        case PackageFormat::DeclKind_ClassDecl: {
            LoadNominalDeclRef(declObj, StaticCast<ClassDecl&>(decl));
            break;
        }
        case PackageFormat::DeclKind_ExtendDecl: {
            LoadNominalDeclRef(declObj, StaticCast<ExtendDecl&>(decl));
            break;
        }
        case PackageFormat::DeclKind_TypeAliasDecl: {
            LoadTypeAliasDeclRef(declObj, StaticCast<TypeAliasDecl&>(decl));
            break;
        }
        case PackageFormat::DeclKind_VarWithPatternDecl: {
            auto info = declObj.info_as_VarWithPatternInfo();
            CJC_NULLPTR_CHECK(info);
            LoadPatternRefs(*info->irrefutablePattern(), *StaticCast<VarWithPatternDecl&>(decl).irrefutablePattern);
            break;
        }
        case PackageFormat::DeclKind_FuncParam:
        case PackageFormat::DeclKind_VarDecl:
        case PackageFormat::DeclKind_PropDecl: {
            auto vda = StaticCast<VarDeclAbstract>(&decl);
            // In the incremental compilation scenario, the original type nodes in the source code package cannot be
            // overwritten.
            // To optimize performance, only types with aliases will generate TypeNode here.
            if (!vda->type && (decl.GetTy()->HasAliasTy() || decl.GetTy()->IsFunc())) {
                vda->type = WrapType(decl.GetTy());
            }
            break;
        }
        case PackageFormat::DeclKind_ThisParam: {
            auto tp = StaticCast<ThisParam*>(&decl);
            // Modal is serialized on Decl.tyMode while SemaTy is shape-only; recover if ApplySerializedTyMode missed.
            ModalInfo tyModal = tp->TyMode();
            if (tyModal == ModalInfo{} && declObj.tyMode() != nullptr) {
                tp->SetTy(ApplyLoadedModal(tp->DataTy(), declObj.tyMode()));
                tyModal = tp->TyMode();
            }
            if (tyModal == ModalInfo{}) {
                CJC_ABORT_WITH_MSG("do not save modal for @~local this param");
            }
            tp->modal.SetLocal(LocalModalToAST(tyModal.local), DEFAULT_POSITION);
            break;
        }
        default:
            break;
    }
}

void ASTLoader::ASTLoaderImpl::LoadExprRefs(const PackageFormat::Expr& exprObj, Expr& expr)
{
    expr.SetTy(ApplyLoadedModal(LoadType(static_cast<FormattedIndex>(exprObj.type())), exprObj.tyMode()));
    expr.mapExpr = GetExprByIndex(exprObj.mapExpr());
    // Only following 4 kind of expression has referenced target decl.
    switch (exprObj.kind()) {
        case PackageFormat::ExprKind_MemberAccess: {
            auto info = exprObj.info_as_ReferenceInfo();
            CJC_ASSERT(info && info->instTys());
            StaticCast<MemberAccess&>(expr).target = GetDeclFromIndex(info->target());
            for (uoffset_t i = 0; i < info->instTys()->size(); ++i) {
                StaticCast<MemberAccess>(expr).instTys.emplace_back(LoadType(info->instTys()->Get(i)));
            }
            if (info->matchedParentTy() != INVALID_FORMAT_INDEX) {
                StaticCast<MemberAccess>(expr).matchedParentTy = ApplyLoadedModal(
                    LoadType(static_cast<FormattedIndex>(info->matchedParentTy())), info->matchedParentTyMode());
            }
            break;
        }
        case PackageFormat::ExprKind_RefExpr: {
            auto info = exprObj.info_as_ReferenceInfo();
            CJC_ASSERT(info && info->instTys());
            StaticCast<RefExpr&>(expr).ref.target = GetDeclFromIndex(info->target());
            for (uoffset_t i = 0; i < info->instTys()->size(); ++i) {
                StaticCast<RefExpr&>(expr).instTys.emplace_back(LoadType(info->instTys()->Get(i)));
            }
            if (info->matchedParentTy() != INVALID_FORMAT_INDEX) {
                StaticCast<RefExpr&>(expr).matchedParentTy = ApplyLoadedModal(
                    LoadType(static_cast<FormattedIndex>(info->matchedParentTy())), info->matchedParentTyMode());
            }
            break;
        }
        case PackageFormat::ExprKind_ArrayLit: {
            if (expr.TyKind() == TypeKind::TYPE_VARRAY) {
                break;
            }
            auto info = exprObj.info_as_ArrayInfo();
            CJC_NULLPTR_CHECK(info);
            StaticCast<ArrayLit&>(expr).initFunc = DynamicCast<FuncDecl*>(GetDeclFromIndex(info->initFunc()));
            CJC_NULLPTR_CHECK(StaticCast<ArrayLit&>(expr).initFunc);
            break;
        }
        case PackageFormat::ExprKind_ArrayExpr: {
            if (expr.TyKind() == TypeKind::TYPE_VARRAY) {
                break;
            }
            auto info = exprObj.info_as_ArrayInfo();
            CJC_NULLPTR_CHECK(info);
            // ArrayExpr's 'initFunc' is optional.
            StaticCast<ArrayExpr&>(expr).initFunc = DynamicCast<FuncDecl*>(GetDeclFromIndex(info->initFunc()));
            break;
        }
        case PackageFormat::ExprKind_LambdaExpr:
            StaticCast<LambdaExpr&>(expr).funcBody->SetTy(expr.GetTy());
            break;
        case PackageFormat::ExprKind_LitConstExpr:
            if (expr.GetTy()->IsString()) {
                StaticCast<LitConstExpr&>(expr).ref = Wrap2RefType(expr.GetTy());
            }
            InitializeLitConstValue(StaticCast<LitConstExpr&>(expr));
            break;
        case PackageFormat::ExprKind_JumpExpr: {
            // 'JumpExpr' will not have mapExpr, the 'mapExpr' field is reused for 'refLoop'.
            auto& je = StaticCast<JumpExpr&>(expr);
            je.refLoop = std::move(je.mapExpr);
            break;
        }
        default:
            break;
    }
    LoadSubNodeRefs(exprObj, expr);
}

/**
 * NOTE: sub nodes which are not subtype of 'Expr' were not stored in 'allLoadedExprs',
 * so we need to load their reference separately. eg: FuncArg, MatchCase, MatchCaseOther, Pattern and it subtypes.
 */
void ASTLoader::ASTLoaderImpl::LoadSubNodeRefs(const PackageFormat::Expr& exprObj, Expr& expr)
{
    switch (exprObj.kind()) {
        case PackageFormat::ExprKind_CallExpr: {
            auto& ce = StaticCast<CallExpr&>(expr);
            CJC_ASSERT(exprObj.operands() && (exprObj.operands()->size() - 1) == ce.args.size());
            for (uoffset_t i = 1; i < exprObj.operands()->size(); i++) {
                auto index = exprObj.operands()->Get(i);
                CJC_ASSERT(index != INVALID_FORMAT_INDEX);
                auto argObj = GetFormatExprByIndex(index);
                auto arg = ce.args[i - 1].get();
                arg->SetTy(ApplyLoadedModal(LoadType(static_cast<FormattedIndex>(argObj->type())), argObj->tyMode()));
                if (arg->TestAttr(Attribute::HAS_INITIAL)) {
                    // Default param's arg does not loaded expr, which should retrieve type from arg's type.
                    arg->expr->SetTy(arg->GetTy());
                }
            }
            break;
        }
        case PackageFormat::ExprKind_TryExpr: {
            auto info = exprObj.info_as_TryInfo();
            CJC_ASSERT(info && info->patterns() &&
                info->patterns()->size() == StaticCast<TryExpr&>(expr).catchPatterns.size());
            for (uoffset_t i = 0; i < info->patterns()->size(); i++) {
                auto patternObj = info->patterns()->Get(i);
                LoadPatternRefs(*patternObj, *StaticCast<TryExpr>(expr).catchPatterns[i]);
            }
            break;
        }
        case PackageFormat::ExprKind_MatchExpr: {
            auto& me = StaticCast<MatchExpr&>(expr);
            CJC_NULLPTR_CHECK(exprObj.operands());
            if (me.matchMode) {
                CJC_ASSERT((exprObj.operands()->size() - 1) == me.matchCases.size());
                // For matchExpr with selector, the start index of matchCases is 1.
                for (uoffset_t i = 1; i < exprObj.operands()->size(); i++) {
                    LoadMatchCaseRef(exprObj.operands()->Get(i), *me.matchCases[i - 1], *me.selector);
                }
            } else {
                for (uoffset_t i = 0; i < exprObj.operands()->size(); i++) {
                    auto mcObj = GetFormatExprByIndex(exprObj.operands()->Get(i));
                    me.matchCaseOthers[i]->SetTy(
                        ApplyLoadedModal(LoadType(static_cast<FormattedIndex>(mcObj->type())), mcObj->tyMode()));
                }
            }
            break;
        }
        case PackageFormat::ExprKind_ForInExpr: {
            auto info = exprObj.info_as_ForInInfo();
            CJC_NULLPTR_CHECK(info);
            LoadPatternRefs(*info->pattern(), *StaticCast<ForInExpr>(expr).pattern);
            break;
        }
        default:
            break;
    }
    LoadSubNodeRefs2(exprObj, expr);
}

void ASTLoader::ASTLoaderImpl::LoadSubNodeRefs2(const PackageFormat::Expr& exprObj, AST::Expr& expr)
{
    switch (exprObj.kind()) {
        case PackageFormat::ExprKind_LetPatternDestructor: {
            auto& let = StaticCast<LetPatternDestructor>(expr);
            auto info = exprObj.info_as_LetPatternDestructorInfo();
            CJC_NULLPTR_CHECK(info);
            for (uoffset_t i{0}; i < info->patterns()->size(); ++i) {
                LoadPatternRefs(*info->patterns()->Get(i), *let.patterns[i]);
            }
            {
                auto initObj = GetFormatExprByIndex(exprObj.operands()->Get(0));
                let.initializer->SetTy(
                    ApplyLoadedModal(LoadType(static_cast<FormattedIndex>(initObj->type())), initObj->tyMode()));
            }
            break;
        }
        default:
            break;
    }
}

void ASTLoader::ASTLoaderImpl::LoadMatchCaseRef(FormattedIndex index, MatchCase& mc, Expr& selector)
{
    auto mcObj = GetFormatExprByIndex(index);
    mc.SetTy(ApplyLoadedModal(LoadType(static_cast<FormattedIndex>(mcObj->type())), mcObj->tyMode()));
    auto info = mcObj->info_as_MatchCaseInfo();
    CJC_NULLPTR_CHECK(info);
    CJC_ASSERT(info->patterns()->size() == mc.patterns.size());
    for (uoffset_t i = 0; i < info->patterns()->size(); i++) {
        LoadPatternRefs(*info->patterns()->Get(i), *mc.patterns[i]);
        mc.patterns[i]->ctxExpr = &selector;
    }
}

void ASTLoader::ASTLoaderImpl::LoadPatternRefs(const PackageFormat::Pattern& pObj, Pattern& pattern)
{
    CJC_ASSERT(pObj.types()->size() >= 1);
    pattern.SetTy(ApplyLoadedModal(LoadType(static_cast<FormattedIndex>(pObj.types()->Get(0))),
        pObj.tyModes() != nullptr ? pObj.tyModes()->Get(0) : nullptr));
    // Sub patterns is guaranteed and created during 'LoadPattern'.
    switch (pObj.kind()) {
        case PackageFormat::PatternKind_TuplePattern:
            for (uoffset_t i = 0; i < pObj.patterns()->size(); i++) {
                LoadPatternRefs(*pObj.patterns()->Get(i), *StaticCast<TuplePattern&>(pattern).patterns[i]);
            }
            break;
        case PackageFormat::PatternKind_TypePattern:
            LoadPatternRefs(*pObj.patterns()->Get(0), *StaticCast<TypePattern&>(pattern).pattern);
            StaticCast<TypePattern&>(pattern).type = WrapType(pattern.GetTy());
            break;
        case PackageFormat::PatternKind_EnumPattern:
            for (uoffset_t i = 0; i < pObj.patterns()->size(); i++) {
                LoadPatternRefs(*pObj.patterns()->Get(i), *StaticCast<EnumPattern&>(pattern).patterns[i]);
            }
            break;
        case PackageFormat::PatternKind_ExceptTypePattern: {
            auto& etp = StaticCast<ExceptTypePattern&>(pattern);
            LoadPatternRefs(*pObj.patterns()->Get(0), *etp.pattern);
            CJC_ASSERT(pObj.types()->size() >= 1);
            etp.types.resize(pObj.types()->size() - 1);
            for (uoffset_t i = 1; i < pObj.types()->size(); i++) {
                etp.types[i - 1] = WrapType(ApplyLoadedModal(
                    LoadType(static_cast<FormattedIndex>(pObj.types()->Get(i))),
                    pObj.tyModes() != nullptr ? pObj.tyModes()->Get(i) : nullptr));
            }
            break;
        }
        case PackageFormat::PatternKind_CommandTypePattern: {
            auto& ctp = StaticCast<CommandTypePattern&>(pattern);
            LoadPatternRefs(*pObj.patterns()->Get(0), *ctp.pattern);
            CJC_ASSERT(pObj.types()->size() >= 1);
            ctp.types.resize(pObj.types()->size() - 1);
            for (uoffset_t i = 1; i < pObj.types()->size(); i++) {
                ctp.types[i - 1] = WrapType(ApplyLoadedModal(
                    LoadType(static_cast<FormattedIndex>(pObj.types()->Get(i))),
                    pObj.tyModes() != nullptr ? pObj.tyModes()->Get(i) : nullptr));
            }
            break;
        }
        default:
            break;
    }
}
