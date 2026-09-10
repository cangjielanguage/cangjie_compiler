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

MockUtils::MockUtils(ImportManager& importManager, TypeManager& typeManager, DiagnosticEngine& d, BaseMangler& mangler)
    : importManager(importManager), typeManager(typeManager), diag(d), mangler(mangler)
{
}

void MockUtils::LoadStdDecls()
{
    arrayDecl = importManager.GetCoreDecl<StructDecl>(STD_LIB_ARRAY);
    stringDecl = importManager.GetCoreDecl<StructDecl>(STD_LIB_STRING);
    optionDecl = importManager.GetCoreDecl<EnumDecl>(STD_LIB_OPTION);
    toStringDecl = importManager.GetCoreDecl<InheritableDecl>(TOSTRING_NAME);
    objectDecl = importManager.GetCoreDecl<ClassDecl>(OBJECT_NAME);
    zeroValueDecl = importManager.GetCoreDecl<FuncDecl>(std::string(ZERO_VALUE_INTRINSIC_NAME));
    exceptionClassDecl = importManager.GetCoreDecl<ClassDecl>(CLASS_EXCEPTION);
}

std::string MockUtils::mockAccessorSuffix = MOCKED_ACCESSOR_SUFFIX;
std::string MockUtils::spyObjVarName = "spiedObjectRef";
std::string MockUtils::spyCallMarkerVarName = "shouldReturnZeroForSpy";
std::string MockUtils::defaultAccessorSuffix = "$Buddy";

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
        paramRef->SetTy(param->GetTy());
        paramRef->curFile = mockedFunc.curFile;
        mockedMethodArgRefs.emplace_back(std::move(paramRef));
    }

    auto baseTy = typeManager.GetStructTy(*arrayDecl, {typeManager.GetAnyTy()});
    auto argRefsArray = CreateArrayLit(std::move(mockedMethodArgRefs), {baseTy});
    AddArrayLitConstructor(*argRefsArray);
    argRefsArray->curFile = mockedFunc.curFile;
    return argRefsArray;
}

bool MockUtils::CanMock(Node& node)
{
    // Currently common/specific declarations are not supported
    if (node.TestAnyAttr(Attribute::COMMON, Attribute::SPECIFIC, Attribute::FROM_COMMON_PART)) {
        return false;
    }
    // Don't generate accessors and spies for instantiated versions of non-MOCK_SUPPORTED declarations
    if (auto decl = DynamicCast<Decl>(&node);
        decl && decl->genericDecl && !decl->genericDecl->TestAttr(Attribute::MOCK_SUPPORTED)) {
        return false;
    }
    return true;
}

std::vector<Decl*> MockUtils::ListExistingMembers(Ptr<Decl> decl)
{
    auto& members = decl->GetMemberDecls();
    std::vector<Decl*> result;
    result.reserve(members.size());
    for (auto& item : members) {
        result.push_back(item.get());
    }
    return result;
}

namespace {

class WalkingGuard {
public:
    MockUtils* mockUtils;
    explicit WalkingGuard(MockUtils* mockUtils)
        : mockUtils(mockUtils)
    {
        CJC_ASSERT(!mockUtils->isWalking);
        mockUtils->isWalking = true;
    }
    ~WalkingGuard()
    {
        CJC_ASSERT(mockUtils->isWalking);
        mockUtils->isWalking = false;
    }
};

} // namespace

void MockUtils::Walk(Ptr<Node> node, std::function<VisitAction(Ptr<Node>)> visitPre,
    std::function<VisitAction(Ptr<Node>)> visitPost)
{
    WalkingGuard guard{this};
    Walker walker{node, visitPre, visitPost};
    walker.Walk();
}

std::unordered_map<std::string, Ptr<Decl>>& MockUtils::DemandIndex(Ptr<Package> package)
{
    auto [iter, inserted] = globalsByPackage.try_emplace(package);
    if (!inserted) {
        return iter->second;
    }
    for (auto& file : package->files) {
        for (auto& decl : file->decls) {
            iter->second[decl->identifier] = decl;
        }
    }
    return iter->second;
}

std::unordered_map<std::string, Ptr<Decl>>& MockUtils::DemandIndex(Ptr<File> file)
{
    auto [iter, inserted] = globalsByFile.try_emplace(file);
    if (!inserted) {
        return iter->second;
    }
    for (auto& decl : file->decls) {
        iter->second[decl->identifier] = decl;
    }
    return iter->second;
}

std::unordered_map<std::string, Ptr<Decl>>& MockUtils::DemandIndex(Ptr<Decl> decl)
{
    auto [iter, inserted] = localsByOuter.try_emplace(decl);
    if (!inserted) {
        return iter->second;
    }
    for (auto& memberDecl : decl->GetMemberDecls()) {
        iter->second[memberDecl->identifier] = memberDecl;
        if (Is<FuncDecl>(memberDecl)) {
            iter->second[memberDecl->identifier + BuildArgumentList(*memberDecl)] = memberDecl;
        }
    }
    return iter->second;
}

void MockUtils::AttachGeneratedDecl(OwnedPtr<Decl>&& decl)
{
    CJC_ASSERT(decl);
    CJC_ASSERT(!isWalking);
    decl->EnableAttr(Attribute::GENERATED_TO_MOCK);
    if (decl->outerDecl) {
        auto& index = DemandIndex(decl->outerDecl);
        index[decl->identifier] = decl;
        if (Is<FuncDecl>(decl)) {
            index[decl->identifier + BuildArgumentList(*decl)] = decl;
        }
        if (auto classDecl = As<ASTKind::CLASS_DECL>(decl->outerDecl); classDecl) {
            classDecl->body->decls.emplace_back(std::move(decl));
            return;
        }
        if (auto interfaceDecl = As<ASTKind::INTERFACE_DECL>(decl->outerDecl); interfaceDecl) {
            interfaceDecl->body->decls.emplace_back(std::move(decl));
            return;
        }
        if (auto extendDecl = As<ASTKind::EXTEND_DECL>(decl->outerDecl); extendDecl) {
            extendDecl->members.emplace_back(std::move(decl));
            return;
        }
        CJC_ABORT();
    }
    CJC_ASSERT(decl->curFile);
    if (!decl->TestAnyAttr(Attribute::PRIVATE, Attribute::INTERNAL, Attribute::PROTECTED, Attribute::PUBLIC)) {
        // Public if access level was not set manually
        decl->EnableAttr(Attribute::PUBLIC);
    }
    DemandIndex(decl->curFile)[decl->identifier] = decl;
    DemandIndex(decl->curFile->curPackage)[decl->identifier] = decl;
    decl->curFile->decls.emplace_back(std::move(decl));
}

void MockUtils::AttachGeneratedDecl(OwnedPtr<Decl>&& decl, const Package& originPackage)
{
    CJC_ASSERT(decl);
    decl->curFile = originPackage.files[0].get();
    decl->begin = originPackage.files[0]->GetBegin();
    decl->end = originPackage.files[0]->GetBegin();
    decl->fullPackageName = originPackage.fullPackageName;
    decl->moduleName = Utils::GetRootPackageName(originPackage.fullPackageName);
    AttachGeneratedDecl(std::move(decl));
}

void MockUtils::AttachGeneratedDecl(OwnedPtr<Decl>&& decl, const Decl& originDecl)
{
    CJC_ASSERT(decl);
    decl->curFile = originDecl.curFile;
    decl->fullPackageName = originDecl.fullPackageName;
    decl->moduleName = originDecl.moduleName;
    decl->begin = originDecl.begin;
    decl->end = originDecl.end;
    AttachGeneratedDecl(std::move(decl));
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

    if (decl.TestAttr(Attribute::PUBLIC) || decl.TestAttr(Attribute::PROTECTED)) {
        return false;
    }

    return true;
}

Ptr<Decl> MockUtils::FindAccessorForMemberAccess(
    const Ptr<Ty> ty, const Ptr<Decl> resolvedMember, AccessorKind kind)
{
    if (!resolvedMember || !IsMockAccessorRequired(*resolvedMember)) {
        return nullptr;
    }

    auto baseDecl = Ty::GetDeclOfTy(ty);
    if (!baseDecl || !Is<ClassDecl>(baseDecl) || !baseDecl->TestAttr(Attribute::MOCK_SUPPORTED)) {
        return nullptr;
    }

    Ptr<ClassDecl> baseClass = As<ASTKind::CLASS_DECL>(baseDecl);
    Ptr<ClassDecl> outerClass;
    if (baseClass->TestAttr(Attribute::GENERIC)) {
        outerClass = baseClass;
    } else if (baseClass->genericDecl != nullptr) {
        // Initially instantiated generic base type points to its original package's type
        // But we need to get the current package's version of that instantiated type
        outerClass = Ptr(As<ASTKind::CLASS_DECL>(baseClass->genericDecl));
    } else {
        outerClass = baseClass;
    }

    return FindAccessor(*outerClass, resolvedMember, kind);
}

Ptr<FuncDecl> MockUtils::FindTopLevelAccessor(Ptr<Decl> member, AccessorKind kind)
{
    auto accessorIdentifier = BuildMockAccessorIdentifier(*member, kind);
    return FindGlobalDecl<FuncDecl>(member->curFile, accessorIdentifier);
}

Ptr<FuncDecl> MockUtils::FindAccessor(Ptr<MemberAccess> ma, Ptr<Decl> target, AccessorKind kind)
{
    Ptr<Decl> accessor;
    if (kind == AccessorKind::FIELD_GETTER || kind == AccessorKind::FIELD_SETTER ||
        kind == AccessorKind::STATIC_FIELD_SETTER || kind == AccessorKind::STATIC_FIELD_GETTER) {
        CJC_ASSERT(ma);
        accessor = FindAccessorForMemberAccess(ma->baseExpr->DataTy(), target, kind);
    } else if (kind == AccessorKind::TOP_LEVEL_VARIABLE_GETTER || kind == AccessorKind::TOP_LEVEL_VARIABLE_SETTER) {
        accessor = FindTopLevelAccessor(target, kind);
    } else {
        CJC_ABORT();
    }
    return As<ASTKind::FUNC_DECL>(accessor);
}

Ptr<Decl> MockUtils::FindAccessor(
    ClassDecl& outerClass, const Ptr<Decl> member, AccessorKind kind)
{
    if (member->TestAttr(Attribute::IN_EXTEND)) {
        return nullptr;
    }
    auto accessorIdentifier = BuildMockAccessorIdentifier(*member, kind);
    if (Is<FuncDecl>(member)) {
        accessorIdentifier.append(BuildArgumentList(*member));
    }
    for (auto& superDecl : outerClass.GetAllSuperDecls()) {
        // Accessors are generated only for classes
        if (superDecl->astKind != ASTKind::CLASS_DECL) {
            continue;
        }
        auto decl = FindMemberDecl<Decl>(superDecl, accessorIdentifier);
        if (!decl) {
            continue;
        }
        return decl;
    }

    return nullptr;
}

AccessorKind MockUtils::ComputeAccessorKind(const FuncDecl& accessorDecl)
{
    if (accessorDecl.propDecl) {
        if (&accessorDecl == GetUsableAccessorForProperty(*accessorDecl.propDecl, true)) {
            if (accessorDecl.TestAttr(Attribute::STATIC)) {
                return AccessorKind::STATIC_PROP_GETTER;
            }
            return AccessorKind::PROP_GETTER;
        } else if (accessorDecl.propDecl->isVar &&
            &accessorDecl == GetUsableAccessorForProperty(*accessorDecl.propDecl, false)) {
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
    return accessorDecl.outerDecl
        ? FindMemberDecl<Decl>(accessorDecl.outerDecl, setterDeclId)
        : FindGlobalDecl<Decl>(accessorDecl.curFile, setterDeclId);
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

    auto outerDeclLen = decl.outerDecl ? decl.outerDecl->identifier.Val().size() + 1 : 0;
    return decl.identifier.Val().substr(0, decl.identifier.Val().size() - MOCKED_ACCESSOR_SUFFIX.size() - outerDeclLen);
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
        auto paramTy = (*it)->GetTy();
        if (auto paramTyDecl = Ty::GetDeclOfTy(paramTy)) {
            if (paramTyDecl->genericDecl) {
                paramTyDecl = paramTyDecl->genericDecl;
            }
            paramTy = paramTyDecl->GetTy();
        }
        result << paramTy->String();
        it++;
        if (it != end) {
            result << ",";
        }
    }
    result << ")";

    return result.str();
}

std::string MockUtils::BuildMockAccessorIdentifier(
    const Decl& originalDecl, AccessorKind kind, bool forErased) const
{
    static std::string underscore = "_";
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
            break;
    }

    std::string argumentsList;
    if (forErased) {
        argumentsList = BuildArgumentList(originalDecl);
    }

    return originalDecl.outerDecl
        ? Concatenate(originalDecl.identifier.Val(), argumentsList, additionalSuffix, MOCKED_ACCESSOR_SUFFIX,
            underscore, originalDecl.outerDecl->identifier.Val())
        : Concatenate(originalDecl.identifier.Val(), argumentsList, additionalSuffix, MOCKED_ACCESSOR_SUFFIX);
}

bool MockUtils::IsGeneratedGetter(AccessorKind kind)
{
    return kind == AccessorKind::FIELD_GETTER || kind == AccessorKind::STATIC_FIELD_GETTER ||
        kind == AccessorKind::TOP_LEVEL_VARIABLE_GETTER;
}

Ptr<Decl> MockUtils::FindMockGlobalDecl(const Decl& decl, const std::string& name)
{
    return FindGlobalDecl<Decl>(decl.curFile, name + MockUtils::mockAccessorSuffix);
}

std::string MockUtils::GetForeignAccessorName(const FuncDecl& decl)
{
    static std::string dollar = "$";
    return Concatenate(decl.fullPackageName, dollar, decl.identifier.Val());
}

OwnedPtr<Expr> MockUtils::CreateGetTypeForTypeParameterCall(const Ptr<GenericParamDecl> genericParam)
{
    auto funcTy = Ptr(StaticCast<FuncTy>(getTypeForTypeParamDecl->DataTy()));

    std::vector<OwnedPtr<FuncArg>> args;
    auto refExpr = CreateRefExpr(*getTypeForTypeParamDecl);
    refExpr->instTys.push_back(genericParam->DataTy());
    refExpr->curFile = genericParam->curFile;

    auto res = CreateCallExpr(std::move(refExpr), std::move(args), getTypeForTypeParamDecl,
        funcTy->retTy, CallKind::CALL_INTRINSIC_FUNCTION);
    res->curFile = genericParam->curFile;

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

    auto baseTy = typeManager.GetStructTy(*arrayDecl, {toStringDecl->DataTy()});
    auto arrayLitOfGetTypeCalls = CreateArrayLit(std::move(getTypeCalls), {baseTy});
    AddArrayLitConstructor(*arrayLitOfGetTypeCalls);
    arrayLitOfGetTypeCalls->curFile = decl.curFile;

    return arrayLitOfGetTypeCalls;
}

Ptr<AST::Decl> MockUtils::GetOuterDecl(AST::Decl& decl) const
{
    if (!decl.outerDecl) {
        return nullptr;
    }

    if (auto extendDecl = DynamicCast<ExtendDecl>(decl.outerDecl)) {
        return Ty::GetDeclOfTy(extendDecl->extendedType->GetTy());
    }

    return decl.outerDecl;
}

Ptr<Decl> MockUtils::GetExtendedTypeDecl(FuncDecl& decl) const
{
    CJC_ASSERT(decl.TestAttr(Attribute::IN_EXTEND));

    auto outerDecl = decl.outerDecl;
    CJC_NULLPTR_CHECK(outerDecl);

    Ptr<ExtendDecl> extendDecl = As<ASTKind::EXTEND_DECL>(outerDecl);
    CJC_NULLPTR_CHECK(extendDecl);

    return Ty::GetDeclOfTy(extendDecl->extendedType->GetTy());
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
        subst[DynamicCast<GenericsTy>(typeParam->DataTy())] = mockedGeneric->typeParameters[i]->DataTy();
        i++;
    }

    classSubsts.push_back(subst);
    std::rotate(classSubsts.rbegin(), classSubsts.rbegin() + 1, classSubsts.rend());
}

ModalTy MockUtils::GetInstantiatedTy(ModalTy ty, std::vector<TypeSubst>& typeSubsts)
{
    auto substitutedTy = ty;
    for (auto typeSubst : typeSubsts) {
        substitutedTy = typeManager.GetInstantiatedTy(substitutedTy, typeSubst);
    }
    return substitutedTy;
}

std::vector<TypeSubst> MockUtils::BuildGenericSubsts(const Ptr<InheritableDecl> decl)
{
    std::vector<TypeSubst> genericSubsts;
    std::queue<Ptr<InheritableDecl>> workList;
    workList.push(decl);

    while (!workList.empty()) {
        auto curDecl = workList.front();
        workList.pop();
        for (auto& inheritedType : curDecl->inheritedTypes) {
            if (inheritedType->GetTy() == curDecl->GetTy() || !inheritedType->GetTy()->HasGeneric()) {
                continue;
            }
            if (auto inheritedDecl = DynamicCast<InheritableDecl>(Ty::GetDeclPtrOfTy(inheritedType->DataTy()));
                inheritedDecl) {
                genericSubsts.emplace_back(GenerateTypeMapping(*inheritedDecl, inheritedType->GetTy()->TyArgs()));
                workList.emplace(inheritedDecl);
            }
        }
    }

    std::reverse(genericSubsts.begin(), genericSubsts.end());

    return genericSubsts;
}

int MockUtils::GetIndexOfGenericTypeParam(DataTy ty, Ptr<Generic> generic) const
{
    int i = 0;
    for (auto& typeParam : generic->typeParameters) {
        if (typeParam->DataTy() == ty) {
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

    if (auto genericTy = DynamicCast<GenericsTy>(refType->DataTy()); genericTy) {
        auto typeParamIndex = GetIndexOfGenericTypeParam(genericTy, oldGeneric);
        if (typeParamIndex != -1) {
            refType->ref.target = newGeneric->typeParameters[static_cast<size_t>(typeParamIndex)].get();
        }
    }

    for (auto& typeArg : refType->typeArguments) {
        UpdateRefTypesTarget(typeArg.get(), oldGeneric, newGeneric);
    }
}

std::vector<DataTy> MockUtils::AddGenericIfNeeded(Decl& originalDecl, Decl& mockedDecl) const
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

    std::vector<DataTy> typeParamTys{};
    auto newGeneric = CloneGeneric(*generic);
    for (auto& typeParam : newGeneric->typeParameters) {
        typeParam->outerDecl = &mockedDecl;
        typeParam->SetTy({typeManager.GetGenericsTy(*typeParam)});
        typeParam->fullPackageName = mockedDecl.fullPackageName;
        typeParam->curFile = mockedDecl.curFile;
        typeParam->DisableAttr(Attribute::IMPORTED);
        typeParamTys.emplace_back(typeParam->DataTy());
    }

    if (originalFuncDecl) {
        auto mockedFuncDecl = As<ASTKind::FUNC_DECL>(&mockedDecl);
        CJC_ASSERT(mockedFuncDecl && mockedFuncDecl->funcBody);
        UpdateRefTypesTarget(mockedFuncDecl->funcBody->retType.get(), generic.get(), newGeneric.get());
        mockedFuncDecl->funcBody->generic = std::move(newGeneric);
    } else {
        mockedDecl.generic = std::move(newGeneric);
        if (auto classDecl = As<ASTKind::CLASS_DECL>(&mockedDecl); classDecl) {
            mockedDecl.SetTy({typeManager.GetClassTy(*classDecl, std::move(typeParamTys))});
        } else if (auto interfaceDecl = As<ASTKind::INTERFACE_DECL>(&mockedDecl); interfaceDecl) {
            mockedDecl.SetTy({typeManager.GetInterfaceTy(*interfaceDecl, std::move(typeParamTys))});
        }
    }

    return typeParamTys;
}

void MockUtils::SetGetTypeForTypeParamDecl(Package& pkg)
{
    getTypeForTypeParamDecl = FindGlobalDecl<FuncDecl>(pkg.files[0], GET_TYPE_FOR_TYPE_PARAMETER_FUNC_NAME);
    if (!getTypeForTypeParamDecl) {
        getTypeForTypeParamDecl = GenerateGetTypeForTypeParamIntrinsic(pkg, typeManager);
        DemandIndex(&pkg)[GET_TYPE_FOR_TYPE_PARAMETER_FUNC_NAME] = getTypeForTypeParamDecl;
        DemandIndex(pkg.files[0])[GET_TYPE_FOR_TYPE_PARAMETER_FUNC_NAME] = getTypeForTypeParamDecl;
    }
}

OwnedPtr<CallExpr> MockUtils::CreateZeroValue(DataTy ty, File& curFile) const
{
    auto zeroValueCall = MakeOwned<CallExpr>();
    zeroValueCall->baseFunc = CreateDeclBasedReferenceExpr(
        *zeroValueDecl, {ty}, std::string(ZERO_VALUE_INTRINSIC_NAME), curFile);
    zeroValueCall->SetTy(ty);
    zeroValueCall->callKind = CallKind::CALL_INTRINSIC_FUNCTION;
    zeroValueCall->resolvedFunction = zeroValueDecl;
    zeroValueCall->curFile = &curFile;
    return zeroValueCall;
}

OwnedPtr<RefExpr> MockUtils::CreateRefExprWithInstTys(
    Decl& target, const std::vector<DataTy>& instTys, const std::string& refName, File& curFile) const
{
    auto refExpr = CreateRefExpr(target);
    refExpr->ref.identifier = refName;
    refExpr->curFile = &curFile;
    refExpr->SetTy(typeManager.GetInstantiatedTy(target.GetTy(), GenerateTypeMapping(target, instTys)));
    refExpr->instTys = instTys;
    return refExpr;
}

OwnedPtr<RefExpr> MockUtils::CreateDeclBasedReferenceExpr(
    Decl& target, const std::vector<DataTy>& instTys, const std::string& refName, File& curFile) const
{
    ModalTy ty = {};

    switch (target.astKind) {
        case ASTKind::FUNC_DECL: case ASTKind::VAR_DECL: {
            ty = ModalTy{typeManager.GetFunctionTy(
                std::vector<ModalTy>(instTys.cbegin(), instTys.cend() - 1), instTys.back())};
            break;
        }
        case ASTKind::CLASS_DECL: {
            ty = ModalTy{typeManager.GetClassTy(*StaticAs<ASTKind::CLASS_DECL>(&target), instTys)};
            break;
        }
        default:
            break;
    }

    std::vector<Ptr<Ty>> tys;
    for (const auto& arg : ty->typeArgs) {
        tys.push_back(arg.Ty());
    }
    return CreateRefExprWithInstTys(target, tys, refName, curFile);
}

OwnedPtr<Expr> MockUtils::CreateThrowExpr(const std::string& message, Ptr<File> curFile)
{
    std::vector<OwnedPtr<Expr>> exceptionCallArgs;
    exceptionCallArgs.emplace_back(CreateLitConstExpr(LitConstKind::STRING, message, stringDecl->GetTy()));
    exceptionCallArgs.back()->curFile = curFile;

    return CreateThrowException(*exceptionClassDecl, std::move(exceptionCallArgs), *curFile, typeManager);
}

OwnedPtr<Expr> MockUtils::CreateTypeCast(OwnedPtr<Expr> selector, ModalTy castTy,
    std::function<OwnedPtr<Expr>(Ptr<VarDecl>)> createMatchedBranch, OwnedPtr<Expr> otherwiseBranch, ModalTy ty)
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

OwnedPtr<Expr> MockUtils::CreateTypeCastOrThrow(OwnedPtr<Expr> selector, DataTy castTy, const std::string& message)
{
    auto createValueBranch = [](Ptr<VarDecl> varDecl) { return CreateRefExpr(*varDecl); };
    auto throwExpression = CreateThrowExpr(message, selector->curFile);

    return CreateTypeCast(
        std::move(selector), castTy, std::move(createValueBranch), std::move(throwExpression), castTy);
}

OwnedPtr<Expr> MockUtils::CreateTypeCastOrZeroValue(OwnedPtr<Expr> selector, DataTy castTy) const
{
    auto createValueBranch = [](Ptr<VarDecl> varDecl) { return CreateRefExpr(*varDecl); };
    auto throwExpression = CreateZeroValue(castTy, *selector->curFile);

    return CreateTypeCast(
        std::move(selector), castTy, std::move(createValueBranch), std::move(throwExpression), castTy);
}

Ptr<FuncTy> MockUtils::EraseFuncTypes(Ptr<FuncTy> funcTy)
{
    std::vector<ModalTy> paramTys;
    for ([[maybe_unused]] auto& paramTy : funcTy->paramTys) {
        paramTys.push_back(typeManager.GetAnyTy());
    }
    
    return typeManager.GetFunctionTy(paramTys, typeManager.GetAnyTy());
}

namespace {

struct InternalTypesChecker {
    bool Check(ModalTy ty)
    {
        if (visitedGenerics.count(ty) > 0) {
            return false;
        }

        if (auto decl = Ty::GetDeclOfTy(ty)) {
            if (decl->linkage == Linkage::INTERNAL) {
                return true;
            }
            if (decl->outerDecl && Check(decl->outerDecl->GetTy())) {
                return true;
            }
        }

        for (auto paramTy : ty->typeArgs) {
            if (Check(paramTy)) {
                return true;
            }
        }

        if (auto genericTy = DynamicCast<GenericsTy>(ty.Ty())) {
            visitedGenerics.insert(ty);
            for (auto upperBound : genericTy->upperBounds) {
                if (Check(upperBound)) {
                    return true;
                }
            }
        }

        return false;
    }

    std::unordered_set<ModalTy> visitedGenerics;
};

} // namespace

bool MockUtils::MayContainInternalTypes(ModalTy ty) const
{
    return InternalTypesChecker{}.Check(ty);
}

namespace {

std::tuple<AccessorKind, AccessorKind> GetVarDeclAccessorKinds(Ptr<Decl> varDecl)
{
    CJC_ASSERT(varDecl->astKind == ASTKind::VAR_DECL);
    if (varDecl->TestAttr(Attribute::STATIC)) {
        return {AccessorKind::STATIC_FIELD_GETTER, AccessorKind::STATIC_FIELD_SETTER};
    } else if (varDecl->TestAttr(Attribute::GLOBAL)) {
        return {AccessorKind::TOP_LEVEL_VARIABLE_GETTER, AccessorKind::TOP_LEVEL_VARIABLE_SETTER};
    } else {
        return {AccessorKind::FIELD_GETTER, AccessorKind::FIELD_SETTER};
    }
}

} // namespace

AccessorKind GetVarDeclSetterAccessorKind(Ptr<Decl> varDecl)
{
    return std::get<1>(GetVarDeclAccessorKinds(varDecl));
}

AccessorKind GetVarDeclGetterAccessorKind(Ptr<Decl> varDecl)
{
    return std::get<0>(GetVarDeclAccessorKinds(varDecl));
}

} // namespace Cangjie
