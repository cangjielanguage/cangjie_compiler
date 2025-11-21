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

#include "flatbuffers/ModuleFormat_generated.h"

#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/AST/ASTCasting.h"

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
        if (auto extendedDecl = Ty::GetDeclPtrOfTy(parentDecl.ty)) {
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

// Wrap ty using RefType without identifier.
OwnedPtr<RefType> WrapTypeInNode(Ptr<Ty> type)
{
    auto refType = MakeOwned<RefType>();
    refType->ty = type;
    if (auto decl = Ty::GetDeclPtrOfTy(type)) {
        refType->ref.target = decl;
        refType->ref.identifier = Is<ClassThisTy>(refType->ty) ? "This" : decl->identifier.Val();
    }
    refType->EnableAttr(Attribute::IMPORTED);
    refType->EnableAttr(Attribute::COMPILER_ADD);
    refType->EnableAttr(Attribute::IS_CHECK_VISITED);
    return refType;
}

void UpdateType(OwnedPtr<Type>& astType, OwnedPtr<Type> loadedType)
{
    CJC_NULLPTR_CHECK(loadedType);
    if (astType == nullptr) {
        astType = std::move(loadedType);
    } else if (Ty::IsTyCorrect(loadedType->ty)) {
        // If type is invalid, it means the type decl is need to be recompiled, do not load type cache further.
        astType->ty = loadedType->ty;
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
                arg->ty = arg->expr->ty;
            }
            break;
        case ASTKind::POINTER_EXPR: {
            auto& arg = StaticCast<PointerExpr&>(expr).arg;
            if (arg) {
                CJC_NULLPTR_CHECK(arg->expr);
                arg->ty = arg->expr->ty;
            }
            break;
        }
        default:
            break;
    }
}
} // namespace

Ptr<Ty> ASTLoader::LoadType(FormattedIndex type) const
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
        dummyExpr->ty = dummyExpr->desugarExpr->ty;
    }
    // NOTE: funcArg expr's type reference is loaded after funcArg itself,
    // so we need to load for arg's ty after finish loading all reference for expression.
    // Also callExpr is loading before call base expr, loading possibly 'resolvedFunction' here.
    for (auto [_, expr] : allLoadedExprs) {
        PostLoadReference(*expr);
    }
}

// Load and reproduce ty according to type index, the mapping pointers are constructed using decl index.
Ptr<Ty> ASTLoader::ASTLoaderImpl::LoadType(FormattedIndex type)
{
    if (type == INVALID_FORMAT_INDEX) {
        return TypeManager::GetInvalidTy();
    }
    // NOTE: serialized index is real table offset plus 1.
    auto index = type - 1;
    // If ty is found in allTypes, return it.
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
    return allTypes[index];
}

bool ASTLoader::ASTLoaderImpl::GetPrimitiveTy(FormattedIndex type, const PackageFormat::SemaTy* typeObj)
{
    if (typeObj && typeObj->kind() >= PackageFormat::TypeKind_Unit &&
        typeObj->kind() <= PackageFormat::TypeKind_Bool) {
        auto astKind = GetASTTypeKind(typeObj->kind());
        CJC_ASSERT(astKind != TypeKind::TYPE_INVALID);
        auto t = TypeManager::GetPrimitiveTy(astKind);
        allTypes[type] = t;
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
        generic->assumptionCollection.emplace(gTy, gTy->upperBounds);
    }
}

std::vector<Ptr<Ty>> ASTLoader::ASTLoaderImpl::LoadTypeArgs(const PackageFormat::SemaTy& typeObj)
{
    std::vector<Ptr<Ty>> typeArgs;
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
    Ptr<Ty> ty = TypeManager::GetInvalidTy();
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
        ty = typeManager.GetTupleTy(LoadTypeArgs(typeObj));
    } else if constexpr (std::is_same_v<TypeT, FuncTy>) {
        auto info = typeObj.info_as_FuncTyInfo();
        CJC_NULLPTR_CHECK(info);
        ty = typeManager.GetFunctionTy(
            LoadTypeArgs(typeObj), LoadType(info->retType()), {info->isC(), false, info->hasVariableLenArg()});
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
            ty = info->isThisTy()
                ? typeManager.GetClassThisTy(*typeDecl, typeArgs)
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
            auto type = WrapTypeInNode(LoadType(index));
            CJC_NULLPTR_CHECK(type);
            if (!Ty::IsTyCorrect(type->ty) && isLoadCache) {
                return; // If any invalid type existed, do not load type cache for current 'id'.
            }
            if (auto classLikeTy = DynamicCast<ClassLikeTy*>(type->ty)) {
                (void)classLikeTy->directSubtypes.emplace(id.ty);
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
        constraint->type = WrapTypeInNode(LoadType(vConstraint->type()));
        auto upperSize = vConstraint->uppers()->size();
        for (uoffset_t j = 0; j < upperSize; j++) {
            constraint->upperBounds.emplace_back(WrapTypeInNode(LoadType(vConstraint->uppers()->Get(j))));
        }
        generic->genericConstraints.emplace_back(std::move(constraint));
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
            auto type = WrapTypeInNode(LoadType(info->funcBody()->retType()));
            UpdateType(astDecl.funcBody->retType, std::move(type));
        }
        astDecl.funcBody->ty = astDecl.ty;
    }
    if constexpr (std::is_same_v<DeclT, ExtendDecl>) {
        UpdateType(astDecl.extendedType, WrapTypeInNode(astDecl.ty));
    }
    if (astDecl.TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        astDecl.genericDecl = GetDeclFromIndex(decl.genericDecl());
    }
}

void ASTLoader::ASTLoaderImpl::LoadTypeAliasDeclRef(const PackageFormat::Decl& decl, TypeAliasDecl& tad)
{
    auto info = decl.info_as_AliasInfo();
    CJC_NULLPTR_CHECK(info);
    auto refType = WrapTypeInNode(LoadType(info->aliasedTy()));
    for (auto typeArg : refType->ty->typeArgs) {
        refType->typeArguments.emplace_back(WrapTypeInNode(typeArg));
    }
    tad.type = std::move(refType);
}

void ASTLoader::ASTLoaderImpl::LoadDeclRefs(const PackageFormat::Decl& declObj, Decl& decl)
{
    // Load ty for all decls and try to load generic constraints.
    decl.ty = LoadType(declObj.type());
    if (!Ty::IsTyCorrect(decl.ty) && isLoadCache) {
        return; // Skip following steps if ty is not correct during loading cache.
    }
    LoadGenericConstraintsRef(declObj.generic(), decl.GetGeneric());
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
        default:
            break;
    }
}

void ASTLoader::ASTLoaderImpl::LoadExprRefs(const PackageFormat::Expr& exprObj, Expr& expr)
{
    expr.ty = LoadType(exprObj.type());
    expr.mapExpr = GetExprByIndex(exprObj.mapExpr());
    // Only following 4 kind of expression has referenced target decl.
    switch (exprObj.kind()) {
        case PackageFormat::ExprKind_MemberAccess: {
            auto info = exprObj.info_as_ReferenceInfo();
            CJC_ASSERT(info && info->instTys());
            StaticCast<MemberAccess&>(expr).target = GetDeclFromIndex(info->target());
            for (uoffset_t i = 0; i < info->instTys()->size(); ++i) {
                StaticCast<MemberAccess&>(expr).instTys.emplace_back(LoadType(info->instTys()->Get(i)));
            }
            StaticCast<MemberAccess&>(expr).matchedParentTy = LoadType(info->matchedParentTy());
            break;
        }
        case PackageFormat::ExprKind_RefExpr: {
            auto info = exprObj.info_as_ReferenceInfo();
            CJC_ASSERT(info && info->instTys());
            StaticCast<RefExpr&>(expr).ref.target = GetDeclFromIndex(info->target());
            for (uoffset_t i = 0; i < info->instTys()->size(); ++i) {
                StaticCast<RefExpr&>(expr).instTys.emplace_back(LoadType(info->instTys()->Get(i)));
            }
            StaticCast<RefExpr&>(expr).matchedParentTy = LoadType(info->matchedParentTy());
            break;
        }
        case PackageFormat::ExprKind_ArrayLit: {
            if (expr.ty->kind == TypeKind::TYPE_VARRAY) {
                break;
            }
            auto info = exprObj.info_as_ArrayInfo();
            CJC_NULLPTR_CHECK(info);
            StaticCast<ArrayLit&>(expr).initFunc = DynamicCast<FuncDecl*>(GetDeclFromIndex(info->initFunc()));
            CJC_NULLPTR_CHECK(StaticCast<ArrayLit&>(expr).initFunc);
            break;
        }
        case PackageFormat::ExprKind_ArrayExpr: {
            if (expr.ty->kind == TypeKind::TYPE_VARRAY) {
                break;
            }
            auto info = exprObj.info_as_ArrayInfo();
            CJC_NULLPTR_CHECK(info);
            // ArrayExpr's 'initFunc' is optional.
            StaticCast<ArrayExpr&>(expr).initFunc = DynamicCast<FuncDecl*>(GetDeclFromIndex(info->initFunc()));
            break;
        }
        case PackageFormat::ExprKind_LambdaExpr:
            StaticCast<LambdaExpr&>(expr).funcBody->ty = expr.ty;
            break;
        case PackageFormat::ExprKind_LitConstExpr:
            if (expr.ty->IsString()) {
                StaticCast<LitConstExpr&>(expr).ref = WrapTypeInNode(expr.ty);
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
                auto typeIdx = GetFormatExprByIndex(index)->type();
                auto arg = ce.args[i - 1].get();
                arg->ty = LoadType(typeIdx);
                if (arg->TestAttr(Attribute::HAS_INITIAL)) {
                    // Default param's arg does not loaded expr, which should retrieve type from arg's type.
                    arg->expr->ty = arg->ty;
                }
            }
            break;
        }
        case PackageFormat::ExprKind_TryExpr: {
            auto info = exprObj.info_as_TryInfo();
            CJC_ASSERT(info && info->patterns() &&
                info->patterns()->size() == StaticCast<TryExpr&>(expr).catchPatterns.size());
            for (uoffset_t i = 0; i < info->patterns()->size(); i++) {
                LoadPatternRefs(*info->patterns()->Get(i), *StaticCast<TryExpr&>(expr).catchPatterns[i]);
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
                    me.matchCaseOthers[i]->ty = LoadType(mcObj->type());
                }
            }
            break;
        }
        case PackageFormat::ExprKind_ForInExpr: {
            auto info = exprObj.info_as_ForInInfo();
            CJC_NULLPTR_CHECK(info);
            LoadPatternRefs(*info->pattern(), *StaticCast<ForInExpr&>(expr).pattern);
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
            auto& let = StaticCast<LetPatternDestructor&>(expr);
            auto info = exprObj.info_as_LetPatternDestructorInfo();
            CJC_NULLPTR_CHECK(info);
            for (uoffset_t i{0}; i < info->patterns()->size(); ++i) {
                LoadPatternRefs(*info->patterns()->Get(i), *let.patterns[i]);
            }
            let.initializer->ty = LoadType(GetFormatExprByIndex(exprObj.operands()->Get(0))->type());
            break;
        }
        default:
            break;
    }
}

void ASTLoader::ASTLoaderImpl::LoadMatchCaseRef(FormattedIndex index, MatchCase& mc, Expr& selector)
{
    auto mcObj = GetFormatExprByIndex(index);
    mc.ty = LoadType(mcObj->type());
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
    pattern.ty = LoadType(pObj.types()->Get(0));
    // Sub patterns is guaranteed and created during 'LoadPattern'.
    switch (pObj.kind()) {
        case PackageFormat::PatternKind_TuplePattern:
            for (uoffset_t i = 0; i < pObj.patterns()->size(); i++) {
                LoadPatternRefs(*pObj.patterns()->Get(i), *StaticCast<TuplePattern&>(pattern).patterns[i]);
            }
            break;
        case PackageFormat::PatternKind_TypePattern:
            LoadPatternRefs(*pObj.patterns()->Get(0), *StaticCast<TypePattern&>(pattern).pattern);
            StaticCast<TypePattern&>(pattern).type = WrapTypeInNode(pattern.ty);
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
                etp.types[i - 1] = WrapTypeInNode(LoadType(pObj.types()->Get(i)));
            }
            break;
        }
        default:
            break;
    }
}
