// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * MockUtils contains helper functions for MockManager, MockSupportManager and TestManager
 */

#include "MockUtils.h"

#include "cangjie/Sema/TestManager.h"
#include "cangjie/Utils/ConstantsUtils.h"

#include <regex>

#include "GenericInstantiation/PartialInstantiation.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Sema/GenericInstantiationManager.h"
#include "GenericInstantiation/GenericInstantiationManagerImpl.h"

namespace Cangjie {

using namespace AST;
using namespace TypeCheckUtil;

namespace {

static const std::string MOCKED_ACCESSOR_SUFFIX = "$ToMock";
static const std::string GETTER_SUFFIX = "$get";
static const std::string SETTER_SUFFIX = "$set";

static constexpr std::string_view ZERO_VALUE_INTRINSIC_NAME = "zeroValue";

} // namespace

MockUtils::MockUtils(ImportManager& importManager, TypeManager& typeManager, GenericInstantiationManager* gim)
    : importManager(importManager),
      typeManager(typeManager),
      gim(gim),
      arrayDecl(importManager.GetCoreDecl<StructDecl>(STD_LIB_ARRAY)),
      stringDecl(importManager.GetCoreDecl<StructDecl>(STD_LIB_STRING)),
      optionDecl(importManager.GetCoreDecl<EnumDecl>(STD_LIB_OPTION)),
      toStringDecl(importManager.GetCoreDecl<InheritableDecl>(TOSTRING_NAME)),
      objectDecl(importManager.GetCoreDecl<ClassDecl>(OBJECT_NAME)),
      zeroValueDecl(importManager.GetCoreDecl<FuncDecl>(std::string(ZERO_VALUE_INTRINSIC_NAME))),
      exceptionClassDecl(importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION))
{}

std::string MockUtils::mockAccessorSuffix = MOCKED_ACCESSOR_SUFFIX;
std::string MockUtils::spyObjVarName = "spiedObjectRef";
std::string MockUtils::spyCallMarkerVarName = "shouldReturnZeroForSpy";
std::string MockUtils::defaultAccessorSuffix = "$Buddy";

void MockUtils::Instantiate(Node& node) const
{
    if (!IS_GENERIC_INSTANTIATION_ENABLED) {
        return;
    }
    gim->impl->RecordExtend(node);
    gim->impl->WalkNonGenericExtendedType();
    if (Is<ClassDecl>(&node)) {
        gim->impl->CheckNodeInstantiation(node);
    } else {
        Walker(
            &node, gim->impl->instantiationWalkerID,
            [this](auto node) { return gim->impl->CheckNodeInstantiation(*node); }).Walk();
    }
}

std::optional<std::unordered_set<Ptr<Decl>>> MockUtils::TryGetInstantiatedDecls(Decl& decl) const
{
    if (!decl.TestAttr(AST::Attribute::GENERIC) && !decl.ty->HasGeneric()) {
        return std::nullopt;
    }
    return gim->impl->GetInstantiatedDecls(decl);
}

Ptr<Decl> MockUtils::GetInstantiatedMemberTarget(Ty& baseTy, Decl& target) const
{
    return gim->impl->GetInstantiatedMemberTarget(baseTy, target);
}


Ptr<Decl> MockUtils::GetInstantiatedDeclWithGenericInfo(Decl& decl, const std::vector<Ptr<Ty>>& instTys) const
{
    return gim->impl->GetInstantiatedDeclWithGenericInfo(gim->impl->ConstructGenericInfo(decl, instTys));
}

Ptr<ClassLikeDecl> MockUtils::GetInstantiatedDeclInCurrentPackage(const Ptr<const ClassLikeTy> classLikeToMockTy)
{
    if (!IS_GENERIC_INSTANTIATION_ENABLED) {
        return RawStaticCast<ClassLikeDecl*>(Ty::GetDeclOfTy(classLikeToMockTy));
    }
    auto classLikeToMock = DynamicCast<ClassLikeDecl*>(classLikeToMockTy->commonDecl.get());
    CJC_NULLPTR_CHECK(classLikeToMock);
    Ptr<Decl> genericDecl;
    if (classLikeToMock->TestAttr(Attribute::GENERIC)) {
        genericDecl = classLikeToMock;
    } else if (classLikeToMock->genericDecl) {
        genericDecl = classLikeToMock->genericDecl;
    } else {
        return classLikeToMock;
    }

    auto genericInfo = gim->impl->ConstructGenericInfo(*genericDecl, typeManager.GetTypeArgs(*classLikeToMockTy));
    return RawStaticCast<ClassLikeDecl*>(gim->impl->GetInstantiatedDeclWithGenericInfo(genericInfo));
}

std::string MockUtils::Mangle(const Decl& decl) const
{
    return mangler.Mangle(decl);
}

OwnedPtr<ArrayLit> MockUtils::WrapCallArgsIntoArray(const FuncDecl& mockedFunc)
{
    std::vector<OwnedPtr<Expr>> mockedMethodArgRefs {};

    for (auto const& param : mockedFunc.funcBody->paramLists[0]->params) {
        auto paramRef = MakeOwned<RefExpr>();
        paramRef->ref = Reference(param->identifier);
        paramRef->ref.target = param.get();
        paramRef->ty = param->ty;
        paramRef->curFile = mockedFunc.curFile;
        mockedMethodArgRefs.emplace_back(std::move(paramRef));
    }

    auto baseTy = typeManager.GetStructTy(*arrayDecl, { typeManager.GetAnyTy() });
    auto argRefsArray = CreateArrayLit(std::move(mockedMethodArgRefs), baseTy);
    AddArrayLitConstructor(*argRefsArray);
    argRefsArray->curFile = mockedFunc.curFile;
    return argRefsArray;
}

bool MockUtils::IsMockAccessor(const Decl& decl)
{
    if (!Is<FuncDecl>(decl) && !Is<PropDecl>(decl)) {
        return false;
    }
    if (decl.astKind == ASTKind::VAR_DECL && !decl.outerDecl) {
        return false;
    }
    return decl.TestAttr(Attribute::GENERATED_TO_MOCK);
}

bool MockUtils::IsMockAccessorRequired(const Decl& decl)
{
    if (decl.astKind == ASTKind::VAR_DECL && decl.outerDecl) {
        return true;
    }

    if (decl.astKind != ASTKind::FUNC_DECL && decl.astKind != ASTKind::PROP_DECL) {
        return false;
    }

    if (decl.TestAttr(Attribute::OPEN) ||
        decl.TestAttr(Attribute::ABSTRACT) ||
        decl.TestAttr(Attribute::CONSTRUCTOR) ||
        decl.TestAttr(Attribute::STATIC) ||
        (decl.outerDecl && decl.outerDecl->astKind == ASTKind::INTERFACE_DECL)
    ) {
        return false;
    }

    if (IS_GENERIC_INSTANTIATION_ENABLED &&
        (decl.TestAttr(Attribute::GENERIC_INSTANTIATED) || decl.TestAttr(Attribute::GENERIC))) {
        return true;
    }

    if (decl.TestAttr(Attribute::PUBLIC) || decl.TestAttr(Attribute::PROTECTED)) {
        return false;
    }

    return true;
}

Ptr<Decl> MockUtils::FindAccessorForMemberAccess(
    const MemberAccess& memberAccess, const Ptr<Decl> resolvedMember,
    const std::vector<Ptr<Ty>>& instTys, AccessorKind kind) const
{
    if (!resolvedMember || !IsMockAccessorRequired(*resolvedMember)) {
        return nullptr;
    }

    if (IS_GENERIC_INSTANTIATION_ENABLED) {
        for (auto tyVar : memberAccess.instTys) {
            if (tyVar->HasGeneric()) {
                return nullptr;
            }
        }
    }

    auto baseTy = memberAccess.baseExpr->ty;
    auto baseDecl = Ty::GetDeclOfTy(baseTy);
    if (!baseDecl || !Is<ClassDecl>(baseDecl) ||
        (IS_GENERIC_INSTANTIATION_ENABLED && baseTy->HasGeneric()) || !baseDecl->TestAttr(Attribute::MOCK_SUPPORTED)
    ) {
        return nullptr;
    }

    Ptr<ClassDecl> baseClass = As<ASTKind::CLASS_DECL>(baseDecl);
    Ptr<ClassDecl> outerClass;
    if (baseClass->TestAttr(Attribute::GENERIC)) {
        outerClass = GetInstantiatedDecl(baseClass, baseTy->typeArgs, IS_GENERIC_INSTANTIATION_ENABLED);
    } else if (baseClass->genericDecl != nullptr) {
        // Initially instantiated generic base type points to its original package's type
        // But we need to get the current package's version of that instantiated type
        outerClass = GetInstantiatedDecl(
            Ptr(As<ASTKind::CLASS_DECL>(baseClass->genericDecl)), baseTy->typeArgs, IS_GENERIC_INSTANTIATION_ENABLED);
    } else {
        outerClass = baseClass;
    }

    return FindAccessor(*outerClass, resolvedMember, instTys, kind);
}

Ptr<FuncDecl> MockUtils::FindTopLevelAccessor(Ptr<Decl> member, AccessorKind kind) const
{
    for (auto& decl : member->curFile->decls) {
        if (!IsMockAccessor(*decl) || !Is<VarDecl>(member)) {
            continue;
        }
        auto originalIdentifier = GetOriginalIdentifierOfMockAccessor(*decl);
        if (kind == AccessorKind::TOP_LEVEL_VARIABLE_GETTER &&
            originalIdentifier == member->identifier + GETTER_SUFFIX) {
            return As<ASTKind::FUNC_DECL>(decl.get());
        } else if (kind == AccessorKind::TOP_LEVEL_VARIABLE_SETTER &&
            originalIdentifier == member->identifier + SETTER_SUFFIX) {
            return As<ASTKind::FUNC_DECL>(decl.get());
        }
    }

    return nullptr;
}

Ptr<FuncDecl> MockUtils::FindAccessor(Ptr<MemberAccess> ma, Ptr<Decl> target, AccessorKind kind) const
{
    Ptr<Decl> accessor;
    if (kind == AccessorKind::FIELD_GETTER || kind == AccessorKind::FIELD_SETTER) {
        CJC_ASSERT(ma);
        accessor = FindAccessorForMemberAccess(*ma, target, {}, kind);
    } else if (kind == AccessorKind::TOP_LEVEL_VARIABLE_GETTER ||
        kind == AccessorKind::TOP_LEVEL_VARIABLE_SETTER
    ) {
        accessor = FindTopLevelAccessor(target, kind);
    } else {
        CJC_ABORT();
    }
    return As<ASTKind::FUNC_DECL>(accessor);
}

Ptr<Decl> MockUtils::FindAccessor(
    ClassDecl& outerClass, const Ptr<Decl> member, const std::vector<Ptr<Ty>>& instTys, AccessorKind kind) const
{
    if (member->TestAttr(Attribute::IN_EXTEND)) {
        return nullptr;
    }
    for (auto& superDecl : outerClass.GetAllSuperDecls()) {
        // Accessors are generated only for classes
        if (superDecl->astKind != ASTKind::CLASS_DECL) {
            continue;
        }
        for (auto& decl : superDecl->GetMemberDecls()) {
            if (!IsMockAccessor(*decl)) {
                continue;
            }

            auto originalIdentifier = GetOriginalIdentifierOfMockAccessor(*decl);
            if (Is<PropDecl>(member) && originalIdentifier == member->identifier) {
                return decl.get();
            } else if (Is<VarDecl>(member)) {
                if (kind == AccessorKind::FIELD_GETTER &&
                    originalIdentifier == member->identifier + GETTER_SUFFIX) {
                    return decl.get();
                } else if (kind == AccessorKind::FIELD_SETTER &&
                    originalIdentifier == member->identifier + SETTER_SUFFIX) {
                    return decl.get();
                }
            } else { // For functions we search accessors by using mangled names due to overloading
                auto instantiatedDecl = GetInstantiatedDecl(
                    member, instTys, IS_GENERIC_INSTANTIATION_ENABLED, outerClass.ty);
                auto originalMangledName = Mangle(*instantiatedDecl);
                auto originalMangledNameGivenFromAccessor = MangleUtils::ComputeMangledNameWithCustomIdentifier(
                    *decl, originalIdentifier);
                if (originalMangledName == originalMangledNameGivenFromAccessor) {
                    return decl.get();
                }
            }
        }
    }

    return nullptr;
}

AccessorKind MockUtils::ComputeAccessorKind(const FuncDecl& accessorDecl)
{
    if (accessorDecl.propDecl) {
        if (&accessorDecl == GetUsableGetterForProperty(*(accessorDecl.propDecl))) {
            if (accessorDecl.TestAttr(Attribute::STATIC)) {
                return AccessorKind::STATIC_PROP_GETTER;
            }
            return AccessorKind::PROP_GETTER;
        } else if (accessorDecl.propDecl->isVar &&
            &accessorDecl == GetUsableSetterForProperty(*(accessorDecl.propDecl))
        ) {
            if (accessorDecl.TestAttr(Attribute::STATIC)) {
                return AccessorKind::STATIC_PROP_SETTER;
            }
            return AccessorKind::PROP_SETTER;
        }
        CJC_ABORT();
    }

    static const std::regex FIELD_GETTER_PATTERN(
        "^.*?\\" + GETTER_SUFFIX + "\\" + MOCKED_ACCESSOR_SUFFIX + ".*?$");
    if (std::regex_match(accessorDecl.identifier.Val().c_str(), FIELD_GETTER_PATTERN)) {
        if (accessorDecl.TestAttr(Attribute::STATIC)) {
            return AccessorKind::STATIC_FIELD_GETTER;
        }
        if (accessorDecl.TestAttr(Attribute::GLOBAL)) {
            return AccessorKind::TOP_LEVEL_VARIABLE_GETTER;
        }
        return AccessorKind::FIELD_GETTER;
    }

    static const std::regex FIELD_SETTER_PATTERN(
        "^.*?\\" + SETTER_SUFFIX + "\\" + MOCKED_ACCESSOR_SUFFIX + ".*?$");
    if (std::regex_match(accessorDecl.identifier.Val().c_str(), FIELD_SETTER_PATTERN)) {
        if (accessorDecl.TestAttr(Attribute::STATIC)) {
            return AccessorKind::STATIC_FIELD_SETTER;
        }
        if (accessorDecl.TestAttr(Attribute::GLOBAL)) {
            return AccessorKind::TOP_LEVEL_VARIABLE_SETTER;
        }
        return AccessorKind::FIELD_SETTER;
    }

    if (accessorDecl.TestAttr(Attribute::GLOBAL)) {
        return AccessorKind::TOP_LEVEL_FUNCTION;
    }

    if (accessorDecl.TestAttr(Attribute::STATIC)) {
        return AccessorKind::STATIC_METHOD;
    }

    return AccessorKind::METHOD;
}

bool MockUtils::IsGetterForMutField(const FuncDecl& accessorDecl)
{
    auto accessorKind = ComputeAccessorKind(accessorDecl);
    if (accessorKind != AccessorKind::FIELD_GETTER &&
        accessorKind != AccessorKind::STATIC_FIELD_GETTER &&
        accessorKind != AccessorKind::TOP_LEVEL_VARIABLE_GETTER
    ) {
        return false;
    }

    static const auto FIELD_GETTER_REGEX =
        std::regex("\\" + GETTER_SUFFIX + "\\" + MOCKED_ACCESSOR_SUFFIX);
    auto getterDeclId = accessorDecl.identifier.Val();
    auto setterDeclId = std::regex_replace(
        getterDeclId,
        FIELD_GETTER_REGEX,
        SETTER_SUFFIX + MOCKED_ACCESSOR_SUFFIX);
    if (getterDeclId == setterDeclId) {
        return false;
    }
    if (accessorDecl.outerDecl) {
        for (auto& member : accessorDecl.outerDecl->GetMemberDecls()) {
            if (member->identifier == setterDeclId) {
                return true;
            }
        }
    } else {
        for (auto& member : accessorDecl.curFile->decls) {
            if (member->identifier == setterDeclId) {
                return true;
            }
        }
    }
    return false;
}

std::string MockUtils::GetOriginalIdentifierOfAccessor(const FuncDecl& decl) const
{
    auto mockSuffixTrimmedId = GetOriginalIdentifierOfMockAccessor(
        *(decl.propDecl ? RawStaticCast<Ptr<const Decl>>(decl.propDecl) : Ptr(&decl)));
    static const auto FIELD_ACCESSOR_REGEX =
        std::regex("^(.*?)(\\" + GETTER_SUFFIX + "|\\" + SETTER_SUFFIX + ")?$");
    return std::regex_replace(mockSuffixTrimmedId, FIELD_ACCESSOR_REGEX, "$01");
}

std::string MockUtils::GetOriginalIdentifierOfMockAccessor(const Decl& decl) const
{
    if (!IsMockAccessor(decl)) {
        return decl.identifier;
    }

    auto outerDeclSuffix = decl.outerDecl ? "_" + decl.outerDecl->identifier.Val() : "";
    auto identifier = decl.identifier.Val().substr(
        0, decl.identifier.Val().size() - (MOCKED_ACCESSOR_SUFFIX + outerDeclSuffix).length());

    if (IS_GENERIC_INSTANTIATION_ENABLED && decl.astKind == ASTKind::FUNC_DECL) {
        auto& fd = static_cast<const FuncDecl&>(decl);
        if (fd.funcBody->generic) {
            identifier = identifier.substr(0, identifier.size() - BuildTypeArgumentList(decl).length());
        }
    }

    return identifier;
}


std::string MockUtils::BuildArgumentList(const AST::Decl& decl) const
{
    if (decl.genericDecl) {
        return BuildArgumentList(*decl.genericDecl);
    }

    auto funcDecl = DynamicCast<FuncDecl>(&decl);
    if (!funcDecl) {
        return "";
    }

    auto it = funcDecl->funcBody->paramLists[0]->params.begin();
    auto end = funcDecl->funcBody->paramLists[0]->params.end();

    std::stringstream result;
    result << "(";
    while (it != end) {
        auto paramTy = (*it)->ty;
        if (auto paramTyDecl = Ty::GetDeclOfTy(paramTy)) {
            if (paramTyDecl->genericDecl) {
                paramTyDecl = paramTyDecl->genericDecl;
            }
            paramTy = paramTyDecl->ty;
        }
        result << Ty::ToString(paramTy);
        it++;
        if (it != end) {
            result << ",";
        }
    }
    result << ")";

    return result.str();
}

std::string MockUtils::BuildTypeArgumentList(const Decl& decl)
{
    if (!IS_GENERIC_INSTANTIATION_ENABLED || decl.astKind != ASTKind::FUNC_DECL) {
        return "";
    }
    auto& fd = static_cast<const FuncDecl&>(decl);

    if (!fd.funcBody->generic) {
        return "";
    }

    std::string typeArgs = "";
    typeArgs += "<";
    size_t i = 0;
    for (auto& arg : fd.funcBody->generic->typeParameters) {
        typeArgs += Ty::ToString(arg->ty);
        if (i != fd.funcBody->generic->typeParameters.size() - 1) {
            typeArgs += ",";
        }
        i++;
    }
    typeArgs += ">";
    return typeArgs;
}

std::string MockUtils::BuildMockAccessorIdentifier(
    const Decl& originalDecl, AccessorKind kind, bool includeArgumentTypes) const
{
    std::string additionalSuffix;
    switch (kind) {
        case AccessorKind::FIELD_GETTER:
        case AccessorKind::STATIC_FIELD_GETTER:
        case AccessorKind::TOP_LEVEL_VARIABLE_GETTER:
            additionalSuffix = GETTER_SUFFIX;
            break;
        case AccessorKind::FIELD_SETTER:
        case AccessorKind::STATIC_FIELD_SETTER:
        case AccessorKind::TOP_LEVEL_VARIABLE_SETTER:
            additionalSuffix = SETTER_SUFFIX;
            break;
        default:
            additionalSuffix = "";
            break;
    }

    auto mangledName = Mangle(originalDecl);
    auto outerDeclSuffix = originalDecl.outerDecl ? "_" + originalDecl.outerDecl->identifier.Val() : "";
    auto argumentSuffix = includeArgumentTypes ? BuildArgumentList(originalDecl) : "";
    return originalDecl.identifier + argumentSuffix + BuildTypeArgumentList(originalDecl) +
        additionalSuffix + MOCKED_ACCESSOR_SUFFIX + outerDeclSuffix;
}

bool MockUtils::IsGeneratedGetter(AccessorKind kind)
{
    return kind == AccessorKind::FIELD_GETTER || kind == AccessorKind::STATIC_FIELD_GETTER ||
        kind == AccessorKind::TOP_LEVEL_VARIABLE_GETTER;
}

Ptr<Decl> MockUtils::FindMockGlobalDecl(const Decl& decl, const std::string& name)
{
    for (auto& varDecl : decl.curFile->decls) {
        if (varDecl->identifier == name + MockUtils::mockAccessorSuffix) {
            return varDecl;
        }
    }
    return nullptr;
}

std::string MockUtils::GetForeignAccessorName(const FuncDecl& decl)
{
    return decl.fullPackageName + "$" + decl.identifier;
}

OwnedPtr<Expr> MockUtils::CreateGetTypeForTypeParameterCall(const Ptr<GenericParamDecl> genericParam)
{
    auto funcTy = Ptr(StaticCast<FuncTy>(getTypeForTypeParamDecl->ty));

    std::vector<OwnedPtr<FuncArg>> args;
    auto refExpr = CreateRefExpr(*getTypeForTypeParamDecl);
    refExpr->instTys.push_back(genericParam->ty);
    refExpr->curFile = genericParam->curFile;

    auto res = CreateCallExpr(std::move(refExpr), std::move(args), getTypeForTypeParamDecl,
        funcTy->retTy, CallKind::CALL_INTRINSIC_FUNCTION);
    res->curFile = genericParam->curFile;

    return res;
}

OwnedPtr<AST::Expr> MockUtils::CreateIsSubtypeTypesCall(Ptr<AST::Ty> tyToCheck, Ptr<AST::Ty> ty)
{
    auto funcTy = Ptr(StaticCast<FuncTy>(isSubtypeTypesDecl->ty));

    std::vector<OwnedPtr<FuncArg>> args;
    auto refExpr = CreateRefExpr(*isSubtypeTypesDecl);
    refExpr->instTys.push_back(tyToCheck);
    refExpr->instTys.push_back(ty);

    auto res = CreateCallExpr(std::move(refExpr), std::move(args), isSubtypeTypesDecl,
        funcTy->retTy, CallKind::CALL_INTRINSIC_FUNCTION);

    return res;
}

OwnedPtr<Expr> MockUtils::WrapCallTypeArgsIntoArray(const Decl& decl)
{
    std::vector<OwnedPtr<Expr>> getTypeCalls;

    if (auto outerDecl = decl.outerDecl; outerDecl) {
        if (auto generic = outerDecl->GetGeneric()) {
            for (auto& genericParam : generic->typeParameters) {
                getTypeCalls.emplace_back(CreateGetTypeForTypeParameterCall(genericParam));
            }
        }
    }

    if (auto generic = decl.GetGeneric(); generic) {
        for (auto& genericParam : generic->typeParameters) {
            getTypeCalls.emplace_back(CreateGetTypeForTypeParameterCall(genericParam));
        }
    }

    auto baseTy = typeManager.GetStructTy(*arrayDecl, { toStringDecl->ty });
    auto arrayLitOfGetTypeCalls = CreateArrayLit(std::move(getTypeCalls), baseTy);
    AddArrayLitConstructor(*arrayLitOfGetTypeCalls);
    arrayLitOfGetTypeCalls->curFile = decl.curFile;

    return arrayLitOfGetTypeCalls;
}

Ptr<ClassDecl> MockUtils::GetExtendedClassDecl(FuncDecl& decl) const
{
    CJC_ASSERT(decl.TestAttr(Attribute::IN_EXTEND));

    auto outerDecl = decl.outerDecl;
    CJC_NULLPTR_CHECK(outerDecl);

    Ptr<ExtendDecl> extendDecl = As<ASTKind::EXTEND_DECL>(outerDecl);
    CJC_NULLPTR_CHECK(extendDecl);

    Ptr<RefType> extendedRefType = As<ASTKind::REF_TYPE>(extendDecl->extendedType);
    CJC_NULLPTR_CHECK(extendedRefType);
    CJC_NULLPTR_CHECK(extendedRefType->ref.target);

    Ptr<ClassDecl> classLikeDecl = As<ASTKind::CLASS_DECL>(extendedRefType->ref.target);
    CJC_NULLPTR_CHECK(classLikeDecl);

    return classLikeDecl;
}

void MockUtils::PrependFuncGenericSubst(
    const Ptr<Generic> originalGeneric,
    const Ptr<Generic> mockedGeneric,
    std::vector<TypeSubst>& classSubsts)
{
    if (!originalGeneric || !mockedGeneric) {
        return;
    }
    CJC_ASSERT(originalGeneric->typeParameters.size() == mockedGeneric->typeParameters.size());

    if (originalGeneric->typeParameters.empty() && classSubsts.empty()) {
        return;
    }

    TypeSubst subst;
    std::vector<OwnedPtr<GenericParamDecl>>::size_type i = 0;
    for (auto& typeParam : originalGeneric->typeParameters) {
        subst[DynamicCast<GenericsTy>(typeParam->ty)] = mockedGeneric->typeParameters[i]->ty;
        i++;
    }

    classSubsts.push_back(subst);
    std::rotate(classSubsts.rbegin(), classSubsts.rbegin() + 1, classSubsts.rend());
}

Ptr<Ty> MockUtils::GetInstantiatedTy(const Ptr<Ty> ty, std::vector<TypeSubst>& typeSubsts)
{
    auto substitutedTy = ty;
    for (auto typeSubst : typeSubsts) {
        substitutedTy = typeManager.GetInstantiatedTy(substitutedTy, typeSubst);
    }
    return substitutedTy;
}

std::vector<TypeSubst> MockUtils::BuildGenericSubsts(const Ptr<InheritableDecl> decl)
{
    if (IS_GENERIC_INSTANTIATION_ENABLED) {
        return {};
    }
    std::vector<TypeSubst> genericSubsts;
    std::queue<Ptr<InheritableDecl>> workList;
    workList.push(decl);

    while (!workList.empty()) {
        auto curDecl = workList.front();
        workList.pop();
        for (auto& inheritedType : curDecl->inheritedTypes) {
            if (inheritedType->ty == curDecl->ty || !inheritedType->ty->HasGeneric()) {
                continue;
            }
            if (auto inheritedDecl = DynamicCast<InheritableDecl>(Ty::GetDeclPtrOfTy(inheritedType->ty));
                inheritedDecl
            ) {
                genericSubsts.emplace_back(GenerateTypeMapping(*inheritedDecl, inheritedType->ty->typeArgs));
                workList.emplace(inheritedDecl);
            }
        }
    }

    std::reverse(genericSubsts.begin(), genericSubsts.end());

    return genericSubsts;
}

int MockUtils::GetIndexOfGenericTypeParam(Ptr<Ty> ty, Ptr<Generic> generic) const
{
    int i = 0;
    for (auto& typeParam : generic->typeParameters) {
        if (typeParam->ty == ty) {
            return i;
        }
        i++;
    }
    return -1;
}

void MockUtils::UpdateRefTypesTarget(Ptr<Type> type, Ptr<Generic> oldGeneric, Ptr<Generic> newGeneric) const
{
    auto refType = As<ASTKind::REF_TYPE>(type);
    if (!refType) {
        return;
    }

    if (auto genericTy = DynamicCast<GenericsTy*>(refType->ty); genericTy) {
        auto typeParamIndex = GetIndexOfGenericTypeParam(genericTy, oldGeneric);
        if (typeParamIndex != -1) {
            refType->ref.target = newGeneric->typeParameters[static_cast<size_t>(typeParamIndex)].get();
        }
    }

    for (auto& typeArg : refType->typeArguments) {
        UpdateRefTypesTarget(typeArg.get(), oldGeneric, newGeneric);
    }
}

std::vector<Ptr<Ty>> MockUtils::AddGenericIfNeeded(Decl& originalDecl, Decl& mockedDecl) const
{
    if (!originalDecl.TestAttr(Attribute::GENERIC)) {
        return {};
    }

    mockedDecl.EnableAttr(Attribute::GENERIC);

    auto originalFuncDecl = As<ASTKind::FUNC_DECL>(&originalDecl);
    Ptr<Generic> generic;
    if (originalFuncDecl) {
        generic = originalFuncDecl->funcBody->generic.get();
    } else {
        generic = originalDecl.generic.get();
    }

    std::vector<Ptr<Ty>> typeParamTys {};
    auto newGeneric = CloneGeneric(*generic);
    for (auto& typeParam : newGeneric->typeParameters) {
        typeParam->outerDecl = &mockedDecl;
        typeParam->ty = typeManager.GetGenericsTy(*typeParam);
        typeParamTys.emplace_back(typeParam->ty);
    }

    if (originalFuncDecl) {
        auto mockedFuncDecl = As<ASTKind::FUNC_DECL>(&mockedDecl);
        CJC_ASSERT(mockedFuncDecl && mockedFuncDecl->funcBody);
        UpdateRefTypesTarget(mockedFuncDecl->funcBody->retType.get(), generic.get(), newGeneric.get());
        mockedFuncDecl->funcBody->generic = std::move(newGeneric);
    } else {
        mockedDecl.generic = std::move(newGeneric);
        if (auto classDecl = As<ASTKind::CLASS_DECL>(&mockedDecl); classDecl) {
            mockedDecl.ty = typeManager.GetClassTy(*classDecl, std::move(typeParamTys));
        } else if (auto interfaceDecl = As<ASTKind::INTERFACE_DECL>(&mockedDecl); interfaceDecl) {
            mockedDecl.ty = typeManager.GetInterfaceTy(*interfaceDecl, std::move(typeParamTys));
        }
    }

    return typeParamTys;
}

void MockUtils::SetGetTypeForTypeParamDecl(Package& pkg)
{
    getTypeForTypeParamDecl = GenerateGetTypeForTypeParamIntrinsic(pkg, typeManager, stringDecl->ty);
}

void MockUtils::SetIsSubtypeTypes(Package& pkg)
{
    isSubtypeTypesDecl = GenerateIsSubtypeTypesIntrinsic(pkg, typeManager);
}

OwnedPtr<CallExpr> MockUtils::CreateZeroValue(Ptr<Ty> ty, File& curFile) const
{
    auto zeroValueCall = MakeOwned<CallExpr>();
    zeroValueCall->baseFunc = CreateDeclBasedReferenceExpr(
        *zeroValueDecl, { ty }, std::string(ZERO_VALUE_INTRINSIC_NAME), curFile);
    zeroValueCall->ty = ty;
    zeroValueCall->callKind = CallKind::CALL_INTRINSIC_FUNCTION;
    zeroValueCall->resolvedFunction = zeroValueDecl;
    zeroValueCall->curFile = &curFile;
    return zeroValueCall;
}

OwnedPtr<RefExpr> MockUtils::CreateRefExprWithInstTys(
    Decl& target, const std::vector<Ptr<Ty>>& instTys, const std::string& refName, File& curFile) const
{
    auto refExpr = CreateRefExpr(target);
    refExpr->ref.identifier = refName;
    refExpr->curFile = &curFile;
    refExpr->ty = typeManager.GetInstantiatedTy(target.ty, GenerateTypeMapping(target, instTys));
    refExpr->instTys = instTys;
    return refExpr;
}

OwnedPtr<RefExpr> MockUtils::CreateDeclBasedReferenceExpr(
    Decl& target, const std::vector<Ptr<Ty>>& instTys, const std::string& refName, File& curFile) const
{
    Ptr<Ty> ty = nullptr;

    switch (target.astKind) {
        case ASTKind::FUNC_DECL: case ASTKind::VAR_DECL: {
            ty = typeManager.GetFunctionTy(std::vector<Ptr<Ty>>(instTys.cbegin(), instTys.cend() - 1), instTys.back());
            break;
        }
        case ASTKind::CLASS_DECL: {
            ty = typeManager.GetClassTy(*StaticAs<ASTKind::CLASS_DECL>(&target), instTys);
            break;
        }
        default:
            break;
    }

    return CreateRefExprWithInstTys(target, ty->typeArgs, refName, curFile);
}

OwnedPtr<Expr> MockUtils::CreateThrowExpr(const std::string& message, Ptr<File> curFile)
{
    std::vector<OwnedPtr<Expr>> exceptionCallArgs;
    exceptionCallArgs.emplace_back(CreateLitConstExpr(LitConstKind::STRING, message, stringDecl->ty));
    exceptionCallArgs.back()->curFile = curFile;

    return CreateThrowException(*exceptionClassDecl, std::move(exceptionCallArgs), *curFile, typeManager);
}


OwnedPtr<Expr> MockUtils::CreateTypeCast(
    OwnedPtr<Expr> selector, Ptr<Ty> castTy,
    std::function<OwnedPtr<Expr>(Ptr<VarDecl>)> createMatchedBranch,
    OwnedPtr<Expr> otherwiseBranch, Ptr<Ty> ty)
{
    auto castType = MockUtils::CreateType<Type>(castTy);
    auto varPatternForTypeCast = CreateVarPattern(V_COMPILER, castTy);
    auto matchedBranch = createMatchedBranch(varPatternForTypeCast->varDecl);

    std::vector<OwnedPtr<MatchCase>> matchCasesTypeCast;

    auto typePattern = CreateTypePattern(std::move(varPatternForTypeCast), std::move(castType), *selector);
    typePattern->curFile = selector->curFile;
    typePattern->matchBeforeRuntime = false;

    matchCasesTypeCast.emplace_back(CreateMatchCase(std::move(typePattern), std::move(matchedBranch)));
    matchCasesTypeCast.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(otherwiseBranch)));

    return CreateMatchExpr(std::move(selector), std::move(matchCasesTypeCast), ty);
}

OwnedPtr<Expr> MockUtils::CreateTypeCastOrThrow(
    OwnedPtr<Expr> selector, Ptr<Ty> castTy, const std::string& message)
{
    auto createValueBranch = [](Ptr<VarDecl> varDecl) { return CreateRefExpr(*varDecl); };
    auto throwExpression = CreateThrowExpr(message, selector->curFile);

    return CreateTypeCast(
        std::move(selector), castTy, std::move(createValueBranch), std::move(throwExpression), castTy);
}

OwnedPtr<Expr> MockUtils::CreateTypeCastOrZeroValue(OwnedPtr<Expr> selector, Ptr<Ty> castTy) const
{
    auto createValueBranch = [](Ptr<VarDecl> varDecl) { return CreateRefExpr(*varDecl); };
    auto throwExpression = CreateZeroValue(castTy, *selector->curFile);

    return CreateTypeCast(
        std::move(selector), castTy, std::move(createValueBranch), std::move(throwExpression), castTy);
}

Ptr<FuncTy> MockUtils::EraseFuncTypes(Ptr<FuncTy> funcTy)
{
    std::vector<Ptr<Ty>> paramTys;
    for ([[maybe_unused]] auto& paramTy : funcTy->paramTys) {
        paramTys.push_back(typeManager.GetAnyTy());
    }
    
    return typeManager.GetFunctionTy(paramTys, typeManager.GetAnyTy());
}

namespace {

struct InternalTypesChecker {
    bool Check(Ptr<Ty> ty)
    {
        if (visitedGenerics.count(ty) > 0) {
            return false;
        }

        if (auto decl = Ty::GetDeclOfTy(ty)) {
            if (decl->linkage == Linkage::INTERNAL) {
                return true;
            }
            if (decl->outerDecl && Check(decl->outerDecl->ty)) {
                return true;
            }
        }

        for (auto paramTy : ty->typeArgs) {
            if (Check(paramTy)) {
                return true;
            }
        }

        if (auto genericTy = DynamicCast<GenericsTy>(ty)) {
            visitedGenerics.insert(ty);
            for (auto upperBound : genericTy->upperBounds) {
                if (Check(upperBound)) {
                    return true;
                }
            }
        }

        return false;
    }

    std::unordered_set<Ptr<Ty>> visitedGenerics;
};

} // namespace

bool MockUtils::MayContainInternalTypes(Ptr<Ty> ty) const
{
    return InternalTypesChecker{}.Check(ty);
}

} // namespace Cangjie
