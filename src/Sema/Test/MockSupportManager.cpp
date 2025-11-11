// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "MockSupportManager.h"

#include "TypeCheckUtil.h"
#include "Desugar/AfterTypeCheck.h"

#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Sema/TestManager.h"
#include "GenericInstantiation/PartialInstantiation.h"

namespace Cangjie {

using namespace AST;
using namespace TypeCheckUtil;
using namespace Cangjie::Sema::Desugar::AfterTypeCheck;

OwnedPtr<RefExpr> CreateThisRef(ClassLikeDecl& targetClass)
{
    auto thisRef = CreateRefExpr(SrcIdentifier{"this"}, targetClass.ty);
    thisRef->ref.target = &targetClass;
    return thisRef;
}

bool IsFieldOrVariable(AccessorKind kind)
{
    return kind == AccessorKind::FIELD_GETTER || kind == AccessorKind::FIELD_SETTER ||
        kind == AccessorKind::STATIC_FIELD_GETTER || kind == AccessorKind::STATIC_FIELD_SETTER ||
        kind == AccessorKind::TOP_LEVEL_VARIABLE_GETTER || kind == AccessorKind::TOP_LEVEL_VARIABLE_SETTER;
}

bool IsStaticField(AccessorKind kind)
{
    return kind == AccessorKind::STATIC_FIELD_GETTER || kind == AccessorKind::STATIC_FIELD_SETTER;
}

bool IsTopLevelField(AccessorKind kind)
{
    return kind == AccessorKind::TOP_LEVEL_VARIABLE_GETTER || kind == AccessorKind::TOP_LEVEL_VARIABLE_SETTER;
}

MockSupportManager::MockSupportManager(TypeManager& typeManager, const Ptr<MockUtils> mockUtils)
    : typeManager(typeManager),
      mockUtils(mockUtils)
{}

Ptr<Expr> ExtractLastDesugaredExpr(Expr& expr)
{
    auto lastDesugaredExpr = &expr;

    while (lastDesugaredExpr->desugarExpr != nullptr) {
        lastDesugaredExpr = lastDesugaredExpr->desugarExpr.get();
    }

    return lastDesugaredExpr;
}

bool MockSupportManager::IsDeclOpenToMock(const Decl& decl)
{
    return decl.TestAttr(Attribute::OPEN) && decl.TestAttr(Attribute::OPEN_TO_MOCK);
}

bool MockSupportManager::DoesClassLikeSupportMocking(ClassLikeDecl& classLikeToCheck)
{
    if (Is<InterfaceDecl>(classLikeToCheck) || classLikeToCheck.TestAttr(Attribute::MOCK_SUPPORTED)) {
        return true;
    }

    if (!classLikeToCheck.TestAttr(Attribute::OPEN) && !classLikeToCheck.TestAttr(Attribute::ABSTRACT)) {
        return false;
    }

    for (auto& superDecl : classLikeToCheck.GetAllSuperDecls()) {
        for (auto& member : superDecl->GetMemberDecls()) {
            if (member->TestAttr(Attribute::CONSTRUCTOR)) {
                continue;
            }
            if (!member->TestAttr(Attribute::OPEN) && !member->TestAttr(Attribute::ABSTRACT)) {
                return false;
            }
        }
    }

    return true;
}

void MockSupportManager::MakeOpenToMockIfNeeded(Decl& decl)
{
    if (auto outerDecl = decl.outerDecl; outerDecl && !outerDecl->IsClassLikeDecl()) {
        return;
    }
    if (!decl.TestAttr(Attribute::OPEN) && !decl.TestAttr(Attribute::ABSTRACT)) {
        decl.EnableAttr(Attribute::OPEN);
        decl.EnableAttr(Attribute::OPEN_TO_MOCK);
    }
}

namespace {

bool IsMemberOfGenericDecl(const Decl& decl)
{
    return decl.outerDecl && (decl.outerDecl->TestAttr(Attribute::GENERIC) || decl.outerDecl->genericDecl);
}

void MarkFuncMockSupportedIfNeeded(FuncDecl& decl)
{
    if (decl.IsStaticOrGlobal() && decl.TestAttr(AST::Attribute::GENERIC)) {
        if (!IsMemberOfGenericDecl(decl)) {
            decl.EnableAttr(Attribute::MOCK_SUPPORTED);
        }
    } else if (decl.TestAttr(AST::Attribute::IN_EXTEND) && decl.TestAttr(AST::Attribute::GENERIC)) {
        if (!IsMemberOfGenericDecl(decl)) {
            decl.EnableAttr(Attribute::MOCK_SUPPORTED);
        }
    }
}

} // namespace

void MockSupportManager::WriteUsedInternalDecl(Decl& decl) {
    usedInternalDecls.insert(&decl);
}

void MockSupportManager::MarkNodeMockSupportedIfNeeded(Node& node)
{
    auto decl = As<ASTKind::DECL>(&node);

    if (!decl) {
        return;
    }

    if (Is<ClassDecl>(decl)) {
        decl->EnableAttr(Attribute::MOCK_SUPPORTED);
        MakeOpenToMockIfNeeded(*decl);
    } else if ((Is<FuncDecl>(decl) || Is<PropDecl>(decl)) &&
        !MockUtils::IsMockAccessorRequired(*decl) &&
        !decl->IsStaticOrGlobal() &&
        !decl->TestAttr(Attribute::CONSTRUCTOR)
    ) {
        decl->EnableAttr(Attribute::MOCK_SUPPORTED);
        MakeOpenToMockIfNeeded(*decl);
        if (auto propMember = As<ASTKind::PROP_DECL>(decl); propMember) {
            decl->EnableAttr(Attribute::MOCK_SUPPORTED);
            MakeOpenToMockIfNeeded(*GetUsableGetterForProperty(*propMember));
            if (propMember->isVar) {
                decl->EnableAttr(Attribute::MOCK_SUPPORTED);
                MakeOpenToMockIfNeeded(*GetUsableSetterForProperty(*propMember));
            }
        }
    } else if (auto funcDecl = As<ASTKind::FUNC_DECL>(decl)) {
        MarkFuncMockSupportedIfNeeded(*funcDecl);
    }
}

void MockSupportManager::MarkMockAccessorWithAttributes(Decl& decl, AccessLevel accessLevel)
{
    decl.DisableAttr(Attribute::GENERIC_INSTANTIATED);
    decl.EnableAttr(Attribute::OPEN);
    decl.DisableAttr(Attribute::PRIVATE, Attribute::PROTECTED, Attribute::PUBLIC, Attribute::INTERNAL);
    decl.EnableAttr(GetAttrByAccessLevel(accessLevel));
    decl.EnableAttr(Attribute::GENERATED_TO_MOCK);
    decl.EnableAttr(Attribute::COMPILER_ADD);
    decl.EnableAttr(Attribute::IN_CLASSLIKE);
}

void MockSupportManager::PrepareDecls(DeclsToPrepare&& decls)
{
    for (auto decl : decls.interfacesWithDefaults) {
        // temporary exclusion std lib packages to preare default methods,
        // to make stdlib compailable in mock-compatible way
        if (decl->fullPackageName.rfind("std.", 0) == 0) {
            continue;
        }
        PrepareInterfaceDecl(*decl);
    }

    for (auto decl : decls.properties) {
        PrepareStaticDecl(*GetUsableGetterForProperty(*decl));
        if (decl->isVar) {
            PrepareStaticDecl(*GetUsableSetterForProperty(*decl));
        }
        decl->EnableAttr(Attribute::MOCK_SUPPORTED);
    }

    for (auto [classDecl, interfaceDecl] : decls.classWithInterfaceDefaults) {
        PrepareClassWithDefaults(*classDecl, *interfaceDecl);
    }

    for (auto decl : decls.functions) {
        if (decl->TestAttr(Attribute::FOREIGN) &&
            usedInternalDecls.find(decl) != usedInternalDecls.end() &&
            !decl->funcBody->paramLists[0]->hasVariableLenArg // vararg C functions are not supported yet
        ) {
            auto wrapperDecl = CreateForeignFunctionAccessorDecl(*decl);
            PrepareStaticDecl(*wrapperDecl);
            generatedMockDecls.emplace(std::move(wrapperDecl));
        } else {
            if (decl->outerDecl &&
                (decl->outerDecl->TestAttr(Attribute::GENERIC) || decl->outerDecl->genericDecl)) {
                continue;
            }

            PrepareStaticDecl(*decl);

            if (auto instantiatedDecls = mockUtils->TryGetInstantiatedDecls(*decl)) {
                for (auto& iDecl : *instantiatedDecls) {
                    PrepareStaticDecl(*iDecl);
                }
            }
        }
    }
}

namespace {

bool HasDefaults(Ptr<InterfaceDecl> decl)
{
    for (auto& memberDecl : decl->GetMemberDecls()) {
        auto funcDecl = As<ASTKind::FUNC_DECL>(memberDecl);
        if (!funcDecl || !funcDecl->TestAttr(Attribute::DEFAULT)) {
            continue;
        }
        return true;
    }

    return false;
}

std::vector<Ptr<InterfaceDecl>> FindInterfacesWithDefaults(Ptr<InheritableDecl> decl)
{
    std::vector<Ptr<InterfaceDecl>> res;

    for (auto& inherited : decl->inheritedTypes) {
        auto typeDecl = Ty::GetDeclOfTy(inherited->ty);
        auto interfaceDecl = As<ASTKind::INTERFACE_DECL>(typeDecl);
        if (!interfaceDecl) {
            continue;
        }

        if (HasDefaults(interfaceDecl)) {
            res.push_back(interfaceDecl);
        }
    }

    return res;
}

} // namespace

void MockSupportManager::CollectDeclsToPrepare(Decl& decl, DeclsToPrepare& decls)
{
    for (auto& member : decl.GetMemberDecls()) {
        if (auto propDecl = As<ASTKind::PROP_DECL>(member); propDecl) {
            decls.properties.emplace_back(propDecl);
        } else {
            CollectDeclsToPrepare(*member, decls);
        }
    }

    if (auto interfaceDecl = As<ASTKind::INTERFACE_DECL>(&decl)) {
        if (HasDefaults(interfaceDecl)) {
            decls.interfacesWithDefaults.emplace_back(interfaceDecl);
        }
        return;
    }

    if (decl.astKind == ASTKind::CLASS_DECL || decl.astKind == ASTKind::EXTEND_DECL) {
        Ptr<ClassDecl> classDecl = DynamicCast<ClassDecl>(&decl);
        if (!classDecl) {
            auto extendDecl = As<ASTKind::EXTEND_DECL>(&decl);
            CJC_ASSERT(extendDecl);
            classDecl = As<ASTKind::CLASS_DECL>(Ty::GetDeclOfTy(extendDecl->extendedType->ty));
        }
        if (!classDecl) {
            return;
        }
    }

    if (auto classDecl = As<ASTKind::CLASS_DECL>(&decl)) {
        for (auto interfaceDecl : FindInterfacesWithDefaults(classDecl)) {
            decls.classWithInterfaceDefaults.emplace_back(classDecl, interfaceDecl);
        }
        return;
    }

    if (auto extendDecl = As<ASTKind::EXTEND_DECL>(&decl)) {
        auto classDecl = As<ASTKind::CLASS_DECL>(Ty::GetDeclOfTy(extendDecl->extendedType->ty));
        if (!classDecl) {
            return;
        }

        for (auto interfaceDecl : FindInterfacesWithDefaults(extendDecl)) {
            decls.classWithInterfaceDefaults.emplace_back(classDecl, interfaceDecl);
        }
        return;
    }

    auto funcDecl = As<ASTKind::FUNC_DECL>(&decl);
    if (!funcDecl) {
        return;
    }

    if (!decl.IsStaticOrGlobal() && !funcDecl->TestAttr(Attribute::IN_EXTEND)) {
        return;
    }

    if (decl.TestAttr(Attribute::PRIVATE) || decl.TestAttr(Attribute::MAIN_ENTRY)) {
        return;
    }

    decls.functions.emplace_back(funcDecl);
}

std::vector<OwnedPtr<MatchCase>> MockSupportManager::GenerateHandlerMatchCases(
    const FuncDecl& funcDecl,
    OwnedPtr<EnumPattern> optionFuncTyPattern,
    OwnedPtr<CallExpr> handlerCallExpr)
{
    static const auto NOTHING_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_NOTHING);
    static const auto UNIT_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);

    auto arrayLitOfGetTypeCalls = mockUtils->WrapCallTypeArgsIntoArray(funcDecl);
    auto handlerRetTy = typeManager.GetAnyTy();
    auto optionFuncRetTy = typeManager.GetEnumTy(*mockUtils->optionDecl, { handlerRetTy });

    std::vector<OwnedPtr<MatchCase>> handlerResultCases;
    auto handlerResultPattern = MakeOwned<EnumPattern>();
    auto handlerResultPatternConstructor = LookupEnumMember(
        mockUtils->GetInstantiatedDecl(optionFuncRetTy->decl, {handlerRetTy}, IS_GENERIC_INSTANTIATION_ENABLED),
        OPTION_VALUE_CTOR);
    handlerResultPattern->ty = RawStaticCast<FuncTy*>(handlerResultPatternConstructor->ty)->retTy;
    handlerResultPattern->constructor = mockUtils->CreateRefExprWithInstTys(
        *handlerResultPatternConstructor, {handlerRetTy}, OPTION_VALUE_CTOR, *(funcDecl.curFile));

    auto handlerResultVarPattern = CreateVarPattern(V_COMPILER, handlerRetTy);
    auto handlerResultRef = CreateRefExpr(*(handlerResultVarPattern->varDecl));
    handlerResultRef->ty = handlerRetTy;
    handlerResultPattern->patterns.emplace_back(std::move(handlerResultVarPattern));

    auto castTy = RawStaticCast<const FuncTy*>(funcDecl.ty)->retTy;
    auto castType = MockUtils::CreateType<Type>(castTy);
    auto varPatternForTypeCast = CreateVarPattern(V_COMPILER, castTy);
    auto varPatternForTypeCastRef = CreateRefExpr(*(varPatternForTypeCast->varDecl));
    varPatternForTypeCastRef->ty = castTy;
    varPatternForTypeCastRef->instTys.emplace_back(castTy);

    std::vector<OwnedPtr<MatchCase>> matchCasesTypeCast;

    auto retExprWithCastedType = CreateReturnExpr(std::move(varPatternForTypeCastRef));
    retExprWithCastedType->ty = NOTHING_TY;
    auto typePattern = CreateTypePattern(std::move(varPatternForTypeCast), std::move(castType), *handlerResultRef);
    typePattern->curFile = funcDecl.curFile;
    auto typeCastMatchCase = CreateMatchCase(std::move(typePattern), std::move(retExprWithCastedType));

    auto zeroValueRet = CreateReturnExpr(mockUtils->CreateZeroValue(castTy, *funcDecl.curFile));
    zeroValueRet->ty = NOTHING_TY;

    if (!castTy->IsNothing()) {
        // There is no valid cast from Any to Nothing
        matchCasesTypeCast.emplace_back(std::move(typeCastMatchCase));
    }
    matchCasesTypeCast.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(zeroValueRet)));

    auto retExpr = CreateMatchExpr(std::move(handlerResultRef), std::move(matchCasesTypeCast), NOTHING_TY);

    handlerResultCases.emplace_back(CreateMatchCase(std::move(handlerResultPattern), std::move(retExpr)));
    handlerResultCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), CreateUnitExpr(UNIT_TY)));

    std::vector<OwnedPtr<MatchCase>> handlerCases;
    handlerCases.emplace_back(
        CreateMatchCase(
            std::move(optionFuncTyPattern),
            CreateMatchExpr(std::move(handlerCallExpr), std::move(handlerResultCases), UNIT_TY)));
    handlerCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), CreateUnitExpr(UNIT_TY)));
    return handlerCases;
}

void MockSupportManager::PrepareStaticDecl(Decl& decl)
{
    static const auto UNIT_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);

    auto funcDecl = As<ASTKind::FUNC_DECL>(&decl);
    if (!funcDecl) {
        return;
    }

    // Do not generate mock var for $test.entry function
    // Because it breaks cjvm
    if (funcDecl->identifier == TEST_ENTRY_NAME) {
        return;
    }

    auto body = funcDecl->funcBody->body.get();
    if (!body) {
        return;
    }

    auto isMethod = !decl.IsStaticOrGlobal();

    auto handlerRetTy = typeManager.GetAnyTy();
    auto optionFuncRetTy = typeManager.GetEnumTy(*mockUtils->optionDecl, { handlerRetTy });
    auto arrayTy = typeManager.GetStructTy(*mockUtils->arrayDecl, { typeManager.GetAnyTy() });
    auto toStrArrayTy = typeManager.GetStructTy(*mockUtils->arrayDecl, { mockUtils->toStringDecl->ty });
    auto objectTy = typeManager.GetClassTy(*mockUtils->objectDecl, {});
    auto funcTy = isMethod
        ? typeManager.GetFunctionTy({objectTy, arrayTy, toStrArrayTy}, optionFuncRetTy)
        : typeManager.GetFunctionTy({arrayTy, toStrArrayTy}, optionFuncRetTy);
    auto optionFuncTy = typeManager.GetEnumTy(*mockUtils->optionDecl, { funcTy });
    auto optionFunc = mockUtils->GetInstantiatedDecl(optionFuncTy->decl, {funcTy}, IS_GENERIC_INSTANTIATION_ENABLED);
    auto noneFuncTy = CreateRefExpr(*LookupEnumMember(optionFunc, OPTION_NONE_CTOR));
    noneFuncTy->ty = optionFuncTy;

    Ptr<VarDecl> varDecl = nullptr;
    if (funcDecl->genericDecl) {
        auto& genericDecl = *funcDecl->genericDecl;
        if (auto it = genericMockVarsDecls.find(&genericDecl); it != genericMockVarsDecls.end()) {
            // The var was already generated, but not yet written to file
            varDecl = it->second;
        } else {
            varDecl = As<ASTKind::VAR_DECL>(
                mockUtils->FindMockGlobalDecl(genericDecl, mockUtils->Mangle(genericDecl)));
        }

        CJC_ASSERT(varDecl);
    } else {
        auto varMangledName = mockUtils->Mangle(decl);
        auto newVarDecl = CreateVarDecl(varMangledName + MockUtils::mockAccessorSuffix, std::move(noneFuncTy), nullptr);
        newVarDecl->curFile = decl.curFile;
        newVarDecl->isVar = true;
        newVarDecl->EnableAttr(Attribute::PUBLIC);
        newVarDecl->EnableAttr(Attribute::GLOBAL);
        newVarDecl->fullPackageName = decl.fullPackageName;

        varDecl = newVarDecl.get();
        generatedMockDecls.insert(std::move(newVarDecl));
        genericMockVarsDecls.emplace(&decl, varDecl);
    }

    if (IS_GENERIC_INSTANTIATION_ENABLED && funcDecl->TestAttr(Attribute::GENERIC)) {
        return;
    }

    auto varDeclRef = CreateRefExpr(*varDecl);
    varDeclRef->ty = optionFuncTy;

    auto optionFuncTyPattern = MakeOwned<EnumPattern>();
    auto optionFuncTyPatternConstructor = LookupEnumMember(optionFunc, OPTION_VALUE_CTOR);
    optionFuncTyPattern->ty = RawStaticCast<FuncTy*>(optionFuncTyPatternConstructor->ty)->retTy;
    optionFuncTyPattern->constructor = mockUtils->CreateRefExprWithInstTys(
        *optionFuncTyPatternConstructor, {funcTy}, OPTION_VALUE_CTOR, *(decl.curFile));

    auto optionFuncTyVarPattern = CreateVarPattern(V_COMPILER, funcTy);
    auto varPatternRef = CreateRefExpr(*optionFuncTyVarPattern->varDecl.get());
    varPatternRef->ty = funcTy;
    optionFuncTyPattern->patterns.emplace_back(std::move(optionFuncTyVarPattern));

    std::vector<OwnedPtr<FuncArg>> handlerCallArgs {};
    if (isMethod) {
        handlerCallArgs.emplace_back(CreateFuncArg(CreateThisRef(*objectTy->decl)));
    }
    handlerCallArgs.emplace_back(CreateFuncArg(mockUtils->WrapCallArgsIntoArray(*funcDecl)));
    handlerCallArgs.emplace_back(CreateFuncArg(mockUtils->WrapCallTypeArgsIntoArray(*funcDecl)));
    auto handlerCallExpr = CreateCallExpr(
        std::move(varPatternRef), std::move(handlerCallArgs), nullptr, optionFuncRetTy);
    handlerCallExpr->callKind = CallKind::CALL_FUNCTION_PTR;

    auto handlerCases = GenerateHandlerMatchCases(
        *funcDecl, std::move(optionFuncTyPattern), std::move(handlerCallExpr));
    auto handlerMatch = CreateMatchExpr(std::move(varDeclRef), std::move(handlerCases), UNIT_TY);
    handlerMatch->curFile = funcDecl->curFile;
    mockUtils->Instantiate(*handlerMatch);
    body->body.push_back(std::move(handlerMatch));
    std::rotate(body->body.rbegin(), body->body.rbegin() + 1, body->body.rend());

    decl.EnableAttr(Attribute::MOCK_SUPPORTED);
}

void MockSupportManager::GenerateVarDeclAccessors(VarDecl& fieldDecl, AccessorKind getterKind, AccessorKind setterKind)
{
    generatedMockDecls.insert(GenerateVarDeclAccessor(fieldDecl, getterKind));
    if (fieldDecl.isVar) {
        generatedMockDecls.insert(GenerateVarDeclAccessor(fieldDecl, setterKind));
    }
    fieldDecl.EnableAttr(Attribute::MOCK_SUPPORTED);
}

void MockSupportManager::GenerateSpyCallMarker(Package& package)
{
    if (package.files.size() == 0) {
        return;
    }

    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto type = MockUtils::CreateType<PrimitiveType>(BOOL_TY);
    type->kind = TypeKind::TYPE_BOOLEAN;
    type->str = BOOL_TY->String();
    auto varDecl = CreateVarDecl(
        MockUtils::spyCallMarkerVarName + MockUtils::mockAccessorSuffix,
        CreateLitConstExpr(LitConstKind::BOOL, "false", BOOL_TY, true),
        std::move(type));
    varDecl->curFile = package.files[0].get();
    varDecl->isVar = true;
    varDecl->EnableAttr(Attribute::PUBLIC);
    varDecl->EnableAttr(Attribute::GLOBAL);
    varDecl->fullPackageName = package.fullPackageName;
    varDecl->TestAttr(Attribute::GENERATED_TO_MOCK);

    generatedMockDecls.insert(std::move(varDecl));
}

Ptr<Decl> MockSupportManager::GenerateSpiedObjectVar(const Decl& decl)
{
    auto declTy = typeManager.GetAnyTy();
    auto mangledName = mockUtils->Mangle(decl);
    auto optionDeclTy = typeManager.GetEnumTy(*mockUtils->optionDecl, { declTy });
    auto noneRef = CreateRefExpr(
        *LookupEnumMember(
            mockUtils->GetInstantiatedDecl(optionDeclTy->decl, {declTy}, IS_GENERIC_INSTANTIATION_ENABLED),
            OPTION_NONE_CTOR));
    noneRef->ty = optionDeclTy;

    auto varDecl = CreateVarDecl(
        MockUtils::spyObjVarName + "$" + mangledName + MockUtils::mockAccessorSuffix,
        std::move(noneRef));
    varDecl->curFile = decl.curFile;
    varDecl->isVar = true;
    varDecl->EnableAttr(Attribute::PUBLIC);
    varDecl->EnableAttr(Attribute::GLOBAL);
    varDecl->fullPackageName = decl.fullPackageName;
    varDecl->TestAttr(Attribute::GENERATED_TO_MOCK);

    auto varRef = varDecl.get();

    generatedMockDecls.insert(std::move(varDecl));

    return varRef;
}

void MockSupportManager::GenerateSpyCallHandler(FuncDecl& funcDecl, Decl& spiedObjectDecl)
{
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    static const auto UNIT_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);

    if (funcDecl.TestAttr(Attribute::CONSTRUCTOR) || funcDecl.TestAttr(Attribute::STATIC)) {
        return;
    }
    auto body = funcDecl.funcBody->body.get();
    if (!body || MockUtils::IsMockAccessor(funcDecl)) {
        return;
    }

    auto spiedObjOptionTy = StaticCast<EnumTy*>(spiedObjectDecl.ty);
    auto spiedObjTy = spiedObjOptionTy->typeArgs[0];
    auto optionSpiedObjTyPattern = MakeOwned<EnumPattern>();
    auto optionSpiedObjPatternConstructor = LookupEnumMember(
        mockUtils->GetInstantiatedDecl(spiedObjOptionTy->decl, {spiedObjTy}, IS_GENERIC_INSTANTIATION_ENABLED),
        OPTION_VALUE_CTOR);
    optionSpiedObjTyPattern->ty = RawStaticCast<FuncTy*>(optionSpiedObjPatternConstructor->ty)->retTy;
    optionSpiedObjTyPattern->constructor = mockUtils->CreateRefExprWithInstTys(
        *optionSpiedObjPatternConstructor, {spiedObjTy}, OPTION_VALUE_CTOR, *(funcDecl.curFile));

    Ptr<Decl> spyCallMarker = nullptr;
    for (auto& mockDecl : generatedMockDecls) {
        if (mockDecl->identifier == MockUtils::spyCallMarkerVarName + MockUtils::mockAccessorSuffix) {
            spyCallMarker = mockDecl;
            break;
        }
    }

    if (!spyCallMarker) {
        return;
    }

    std::vector<OwnedPtr<FuncArg>> callBaseArgs;
    for (auto& param : funcDecl.funcBody->paramLists[0]->params) {
        callBaseArgs.emplace_back(CreateFuncArg(CreateRefExpr(*param)));
    }

    auto castTy = funcDecl.outerDecl->ty;
    auto castType = MockUtils::CreateType<Type>(castTy);
    auto varPatternForTypeCast = CreateVarPattern(V_COMPILER, castTy);
    auto varPatternForTypeCastRef = CreateRefExpr(*(varPatternForTypeCast->varDecl));
    varPatternForTypeCastRef->ty = castTy;
    varPatternForTypeCastRef->instTys.emplace_back(castTy);

    auto memberAccessFuncBaseExpr = CreateMemberAccess(std::move(varPatternForTypeCastRef), funcDecl);
    memberAccessFuncBaseExpr->EnableAttr(Attribute::GENERATED_TO_MOCK);

    if (auto& genericInfo = funcDecl.funcBody->generic; genericInfo) {
        for (auto& typeParam : genericInfo->typeParameters) {
            memberAccessFuncBaseExpr->instTys.emplace_back(typeParam->ty);
        }
    }
    auto callMockedMember = MakeOwned<CallExpr>();
    callMockedMember->ty = RawStaticCast<const FuncTy*>(memberAccessFuncBaseExpr->ty)->retTy;
    callMockedMember->resolvedFunction = RawStaticCast<FuncDecl*>(memberAccessFuncBaseExpr->target);
    callMockedMember->baseFunc = std::move(memberAccessFuncBaseExpr);
    callMockedMember->args = std::move(callBaseArgs);
    callMockedMember->callKind = CallKind::CALL_DECLARED_FUNCTION;
    callMockedMember->curFile = funcDecl.curFile;
    callMockedMember->EnableAttr(Attribute::GENERATED_TO_MOCK);

    std::vector<OwnedPtr<Node>> nodes {};
    auto trueLit = CreateLitConstExpr(LitConstKind::BOOL, "true", BOOL_TY);
    trueLit->curFile = funcDecl.curFile;
    auto trueSpyCallMarkerAssign =
        CreateAssignExpr(CreateRefExpr(*spyCallMarker), std::move(trueLit), UNIT_TY);
    trueSpyCallMarkerAssign->curFile = funcDecl.curFile;
    trueSpyCallMarkerAssign->EnableAttr(Attribute::GENERATED_TO_MOCK);
    auto falseLit = CreateLitConstExpr(LitConstKind::BOOL, "false", BOOL_TY);
    falseLit->curFile = funcDecl.curFile;
    auto falseSpyCallMarkerAssign =
        CreateAssignExpr(CreateRefExpr(*spyCallMarker), std::move(falseLit), UNIT_TY);
    falseSpyCallMarkerAssign->curFile = funcDecl.curFile;
    falseSpyCallMarkerAssign->EnableAttr(Attribute::GENERATED_TO_MOCK);

    auto spiedObjVarPattern = CreateVarPattern(V_COMPILER, spiedObjTy);
    auto spiedObjVarRef = CreateRefExpr(*spiedObjVarPattern->varDecl.get());
    optionSpiedObjTyPattern->patterns.emplace_back(std::move(spiedObjVarPattern));

    auto callMockedMemberResult = CreateVarDecl(
        "callBaseResult" + MockUtils::mockAccessorSuffix, std::move(callMockedMember), nullptr);
    callMockedMemberResult->curFile = funcDecl.curFile;
    callMockedMemberResult->fullPackageName = funcDecl.fullPackageName;
    auto callMockedMemberResultRef = CreateRefExpr(*callMockedMemberResult);

    std::vector<OwnedPtr<MatchCase>> matchCasesTypeCast;

    auto typePattern = CreateTypePattern(std::move(varPatternForTypeCast), std::move(castType), *spiedObjVarRef);
    typePattern->curFile = funcDecl.curFile;
    auto typeCastMatchCase = CreateMatchCase(std::move(typePattern), std::move(trueSpyCallMarkerAssign));
    typeCastMatchCase->exprOrDecls->body.emplace_back(std::move(callMockedMemberResult));
    typeCastMatchCase->exprOrDecls->body.emplace_back(std::move(falseSpyCallMarkerAssign));
    typeCastMatchCase->exprOrDecls->body.emplace_back(
        CreateReturnExpr(std::move(callMockedMemberResultRef), funcDecl.funcBody));

    matchCasesTypeCast.emplace_back(std::move(typeCastMatchCase));
    matchCasesTypeCast.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), CreateUnitExpr(UNIT_TY)));

    auto typeCastMatch = CreateMatchExpr(std::move(spiedObjVarRef), std::move(matchCasesTypeCast), UNIT_TY);

    std::vector<OwnedPtr<MatchCase>> handlerCases;
    handlerCases.emplace_back(CreateMatchCase(std::move(optionSpiedObjTyPattern), std::move(typeCastMatch)));
    handlerCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), CreateUnitExpr(UNIT_TY)));

    auto handlerMatch = CreateMatchExpr(CreateRefExpr(spiedObjectDecl), std::move(handlerCases), UNIT_TY);
    handlerMatch->curFile = funcDecl.curFile;

    auto falseLitBackCall = CreateLitConstExpr(LitConstKind::BOOL, "false", BOOL_TY);
    falseLitBackCall->curFile = funcDecl.curFile;
    auto falseSpyCallMarkerAssignBackCall = CreateAssignExpr(
        CreateRefExpr(*spyCallMarker), std::move(falseLitBackCall), UNIT_TY);
    falseSpyCallMarkerAssignBackCall->curFile = funcDecl.curFile;
    falseSpyCallMarkerAssignBackCall->EnableAttr(Attribute::GENERATED_TO_MOCK);
    std::vector<OwnedPtr<MatchCase>> callMarkerCases;
    OwnedPtr<ConstPattern> truePattern = MakeOwned<ConstPattern>();
    truePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "true", BOOL_TY, true);
    truePattern->ty = BOOL_TY;
    callMarkerCases.emplace_back(CreateMatchCase(
        std::move(truePattern),
        std::move(falseSpyCallMarkerAssignBackCall)));

    OwnedPtr<ConstPattern> falsePattern = MakeOwned<ConstPattern>();
    falsePattern->ty = BOOL_TY;
    falsePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "false", BOOL_TY, true);

    callMarkerCases.emplace_back(CreateMatchCase(
        std::move(falsePattern),
        std::move(handlerMatch)));

    auto spyCallMarkerMatch = CreateMatchExpr(CreateRefExpr(*spyCallMarker), std::move(callMarkerCases), UNIT_TY);

    mockUtils->Instantiate(*spyCallMarkerMatch);
    body->body.push_back(std::move(spyCallMarkerMatch));
    std::rotate(body->body.rbegin(), body->body.rbegin() + 1, body->body.rend());
}

void MockSupportManager::PrepareToSpy(Decl& decl)
{
    auto classLikeDecl = As<ASTKind::CLASS_LIKE_DECL>(&decl);

    if (!classLikeDecl || (!classLikeDecl->TestAttr(Attribute::MOCK_SUPPORTED) && !Is<InterfaceDecl>(classLikeDecl))) {
        return;
    }

    auto spiedObjectDecl = GenerateSpiedObjectVar(decl);

    for (auto& member : classLikeDecl->GetMemberDecls()) {
        if (auto funcDecl = As<ASTKind::FUNC_DECL>(member); funcDecl && !funcDecl->IsFinalizer()) {
            GenerateSpyCallHandler(*funcDecl, *spiedObjectDecl);
        }
    }
}

void MockSupportManager::GenerateAccessors(Decl& decl)
{
    if (auto varDecl = As<ASTKind::VAR_DECL>(&decl); varDecl && varDecl->TestAttr(Attribute::GLOBAL)) {
        GenerateVarDeclAccessors(
            *varDecl, AccessorKind::TOP_LEVEL_VARIABLE_GETTER, AccessorKind::TOP_LEVEL_VARIABLE_SETTER);
        return;
    }

    auto classDecl = As<ASTKind::CLASS_DECL>(&decl);

    if (!classDecl) {
        return;
    }

    for (auto& member : classDecl->GetMemberDecls()) {
        if (member->TestAttr(Attribute::CONSTRUCTOR)) {
            continue;
        }
        if (member->TestAttr(Attribute::STATIC)) {
            if (auto fieldDecl = As<ASTKind::VAR_DECL>(member.get()); fieldDecl && !Is<PropDecl>(member.get())) {
                GenerateVarDeclAccessors(
                    *fieldDecl, AccessorKind::STATIC_FIELD_GETTER, AccessorKind::STATIC_FIELD_SETTER);
            }
            continue;
        }
        if (auto funcDecl = As<ASTKind::FUNC_DECL>(member); funcDecl &&
            (funcDecl->IsFinalizer() || funcDecl->isFrozen)
        ) {
            continue;
        }
        if (!MockUtils::IsMockAccessorRequired(*member)) {
            continue;
        }

        if (auto propDecl = As<ASTKind::PROP_DECL>(member.get()); propDecl) {
            generatedMockDecls.insert(GeneratePropAccessor(*propDecl));
        } else if (auto methodDecl = As<ASTKind::FUNC_DECL>(member.get()); methodDecl) {
            if (!IS_GENERIC_INSTANTIATION_ENABLED) {
                generatedMockDecls.insert(GenerateFuncAccessor(*RawStaticCast<FuncDecl*>(methodDecl)));
            }
            if (auto instantiatedDecls = mockUtils->TryGetInstantiatedDecls(*methodDecl)) {
                for (auto& instantiatedDecl : *instantiatedDecls) {
                    generatedMockDecls.insert(GenerateFuncAccessor(*RawStaticCast<FuncDecl*>(instantiatedDecl)));
                }
            } else if (IS_GENERIC_INSTANTIATION_ENABLED) {
                generatedMockDecls.insert(GenerateFuncAccessor(*RawStaticCast<FuncDecl*>(methodDecl)));
            }
        } else if (auto fieldDecl = As<ASTKind::VAR_DECL>(member.get()); fieldDecl) {
            GenerateVarDeclAccessors(*fieldDecl, AccessorKind::FIELD_GETTER, AccessorKind::FIELD_SETTER);
        }
    }
}

bool MockSupportManager::NeedEraseAccessorTypes(AST::Decl& decl) const
{
    if (decl.outerDecl) {
        if (decl.outerDecl->linkage == Linkage::INTERNAL) {
            return false;
        }
    }
    return mockUtils->MayContainInternalTypes(decl.ty);
}

OwnedPtr<FuncDecl> MockSupportManager::GenerateErasedFuncAccessor(FuncDecl& methodDecl) const
{
    auto outerClassDecl = As<ASTKind::CLASS_DECL>(methodDecl.outerDecl);
    CJC_ASSERT(outerClassDecl);

    OwnedPtr<FuncDecl> methodAccessor = ASTCloner::Clone(Ptr(&methodDecl));
    mockUtils->AddGenericIfNeeded(methodDecl, *methodAccessor);

    std::vector<Ptr<Ty>> typeParamTys;
    auto memberAccessOriginal = CreateRefExpr(methodDecl);

    if (auto& generic = methodAccessor->funcBody->generic; generic) {
        generic->genericConstraints.clear();
        for (auto& typeParam : generic->typeParameters) {
            typeParam->outerDecl = methodAccessor;
            typeParamTys.emplace_back(typeParam->ty);
            memberAccessOriginal->instTys.emplace_back(typeParam->ty);
        }
    }

    TypeSubst typeSubst = GenerateTypeMapping(methodDecl, typeParamTys);
    auto originalFuncTy = StaticCast<FuncTy>(typeManager.GetInstantiatedTy(methodDecl.ty, typeSubst));

    methodAccessor->ty = mockUtils->EraseFuncTypes(StaticCast<FuncTy>(methodDecl.ty));
    memberAccessOriginal->ty = originalFuncTy;

    std::vector<OwnedPtr<FuncArg>> mockedMethodArgRefs {};
    for (auto& param : methodAccessor->funcBody->paramLists[0]->params) {
        auto originalTy = typeManager.GetInstantiatedTy(param->ty, typeSubst);
        param->ty = typeManager.GetAnyTy();
        param->outerDecl = methodAccessor.get();
        auto refExpr = CreateRefExpr(*param);
        refExpr->curFile = param->curFile;
        auto arg = mockUtils->CreateTypeCastOrThrow(std::move(refExpr), originalTy, "internal error");
        mockedMethodArgRefs.emplace_back(CreateFuncArg(std::move(arg)));
    }

    auto callOriginalMethod = MakeOwned<CallExpr>();
    callOriginalMethod->ty = originalFuncTy->retTy;
    callOriginalMethod->resolvedFunction = &methodDecl;
    callOriginalMethod->baseFunc = std::move(memberAccessOriginal);
    callOriginalMethod->args = std::move(mockedMethodArgRefs);
    callOriginalMethod->callKind = CallKind::CALL_DECLARED_FUNCTION;
    callOriginalMethod->curFile = methodDecl.curFile;

    std::vector<OwnedPtr<Node>> mockedMethodBodyNodes;
    mockedMethodBodyNodes.emplace_back(CreateReturnExpr(std::move(callOriginalMethod), methodAccessor->funcBody.get()));
    methodAccessor->funcBody->body->body = std::move(mockedMethodBodyNodes);
    methodAccessor->funcBody->funcDecl = methodAccessor.get();
    methodAccessor->funcBody->ty = methodAccessor->ty;
    methodAccessor->funcBody->body->ty = methodAccessor->ty;
    methodAccessor->funcBody->retType->ty = typeManager.GetAnyTy();

    methodAccessor->propDecl = nullptr;
    methodAccessor->isSetter = false;
    methodAccessor->isGetter = false;
    bool includeArgumentTypes = true;
    methodAccessor->identifier = mockUtils->BuildMockAccessorIdentifier(
        methodDecl, AccessorKind::METHOD, includeArgumentTypes);
    methodAccessor->mangledName = mockUtils->Mangle(*methodAccessor);

    MarkMockAccessorWithAttributes(*methodAccessor, AccessLevel::PUBLIC);
    methodAccessor->linkage = Linkage::EXTERNAL;

    return methodAccessor;
}

OwnedPtr<FuncDecl> MockSupportManager::GenerateFuncAccessor(FuncDecl& methodDecl)
{
    bool needEraseTypes = NeedEraseAccessorTypes(methodDecl);

    auto outerClassDecl = As<ASTKind::CLASS_DECL>(methodDecl.outerDecl);
    CJC_ASSERT(outerClassDecl);

    OwnedPtr<FuncDecl> methodAccessor = ASTCloner::Clone(Ptr(&methodDecl));
    mockUtils->AddGenericIfNeeded(methodDecl, *methodAccessor);

    OwnedPtr<FuncDecl> erasedAccessor = needEraseTypes ? GenerateErasedFuncAccessor(methodDecl) : nullptr;

    std::vector<Ptr<Ty>> typeParamTys;
    OwnedPtr<RefExpr> memberAccessOriginal =
        needEraseTypes ? CreateRefExpr(*erasedAccessor) : CreateRefExpr(methodDecl);

    if (auto& generic = methodAccessor->funcBody->generic; generic) {
        for (auto& typeParam : generic->typeParameters) {
            typeParam->outerDecl = methodAccessor;
            typeParamTys.emplace_back(typeParam->ty);
            memberAccessOriginal->instTys.emplace_back(typeParam->ty);
        }
    }

    TypeSubst typeSubst = GenerateTypeMapping(methodDecl, typeParamTys);
    auto originalFuncTy = StaticCast<FuncTy>(typeManager.GetInstantiatedTy(methodDecl.ty, typeSubst));
    methodAccessor->ty = originalFuncTy;
    memberAccessOriginal->ty = needEraseTypes ? erasedAccessor->ty : originalFuncTy;

    std::vector<OwnedPtr<FuncArg>> mockedMethodArgRefs {};
    auto& accessorParams = methodAccessor->funcBody->paramLists[0]->params;
    for (std::size_t param_idx = 0; param_idx < accessorParams.size(); ++param_idx) {
        auto& param = accessorParams[param_idx];
        param->ty = typeManager.GetInstantiatedTy(param->ty, typeSubst);
        param->outerDecl = methodAccessor.get();
        auto refExpr = CreateRefExpr(*param);
        refExpr->curFile = param->curFile;

        if (param->desugarDecl) {
            auto& originalParam = methodDecl.funcBody->paramLists[0]->params[param_idx];
            CJC_ASSERT(originalParam->desugarDecl);
            param->desugarDecl = GenerateFuncAccessor(*originalParam->desugarDecl);
            param->desugarDecl->ownerFunc = methodAccessor;
        }
        mockedMethodArgRefs.emplace_back(CreateFuncArg(std::move(refExpr)));
    }

    auto callOriginalMethod = MakeOwned<CallExpr>();
    if (needEraseTypes) {
        callOriginalMethod->ty = typeManager.GetAnyTy();
        callOriginalMethod->resolvedFunction = erasedAccessor;
    } else {
        callOriginalMethod->ty = originalFuncTy->retTy;
        callOriginalMethod->resolvedFunction = &methodDecl;
    }
    callOriginalMethod->baseFunc = std::move(memberAccessOriginal);
    callOriginalMethod->args = std::move(mockedMethodArgRefs);
    callOriginalMethod->callKind = CallKind::CALL_DECLARED_FUNCTION;
    callOriginalMethod->curFile = methodDecl.curFile;

    OwnedPtr<Expr> retValueExpr = std::move(callOriginalMethod);
    if (needEraseTypes) {
        retValueExpr = mockUtils->CreateTypeCastOrZeroValue(std::move(retValueExpr), originalFuncTy->retTy);
    }

    std::vector<OwnedPtr<Node>> mockedMethodBodyNodes;
    mockedMethodBodyNodes.emplace_back(CreateReturnExpr(std::move(retValueExpr), methodAccessor->funcBody.get()));
    methodAccessor->funcBody->body->body = std::move(mockedMethodBodyNodes);
    methodAccessor->funcBody->funcDecl = methodAccessor.get();
    methodAccessor->funcBody->ty = originalFuncTy;
    methodAccessor->funcBody->body->ty = originalFuncTy;
    methodAccessor->funcBody->retType->ty = originalFuncTy->retTy;
    methodAccessor->identifier = mockUtils->BuildMockAccessorIdentifier(methodDecl, AccessorKind::METHOD);
    methodAccessor->mangledName = mockUtils->Mangle(*methodAccessor);

    if (needEraseTypes) {
        MarkMockAccessorWithAttributes(*methodAccessor, GetAccessLevel(methodDecl));
        methodAccessor->linkage = methodDecl.linkage;
    } else {
        MarkMockAccessorWithAttributes(*methodAccessor, AccessLevel::PUBLIC);
        methodAccessor->linkage = Linkage::EXTERNAL;
        methodDecl.linkage = Linkage::EXTERNAL;
    }

    // Mangling has to be done after outer function identifier is set
    for (auto& param : accessorParams) {
        if (param->desugarDecl) {
            param->desugarDecl->mangledName.clear();
            param->desugarDecl->mangledName = mockUtils->Mangle(*param->desugarDecl);
        }
    }

    if (needEraseTypes) {
        generatedMockDecls.insert(std::move(erasedAccessor));
    }

    return methodAccessor;
}

OwnedPtr<PropDecl> MockSupportManager::GeneratePropAccessor(PropDecl& propDecl)
{
    OwnedPtr<PropDecl> propAccessor = ASTCloner::Clone(Ptr(&propDecl));
    auto outerClassDecl = As<ASTKind::CLASS_DECL>(propDecl.outerDecl);
    CJC_ASSERT(outerClassDecl);

    std::vector<OwnedPtr<FuncDecl>> accessorForGetters;
    std::vector<OwnedPtr<FuncDecl>> accessorForSetters;

    auto propGetter = GenerateFuncAccessor(*GetUsableGetterForProperty(propDecl));
    propGetter->propDecl = propAccessor.get();
    accessorForGetters.emplace_back(std::move(propGetter));
    if (propDecl.isVar) {
        auto propSetter = GenerateFuncAccessor(*GetUsableSetterForProperty(propDecl));
        propSetter->propDecl = propAccessor.get();
        accessorForSetters.emplace_back(std::move(propSetter));
    }
    propAccessor->getters = std::move(accessorForGetters);
    propAccessor->setters = std::move(accessorForSetters);
    propAccessor->identifier = mockUtils->BuildMockAccessorIdentifier(propDecl, AccessorKind::PROP);
    propAccessor->mangledName = mockUtils->Mangle(*propAccessor);
    if (NeedEraseAccessorTypes(propDecl)) {
        MarkMockAccessorWithAttributes(*propAccessor, GetAccessLevel(propDecl));
        propAccessor->linkage = propDecl.linkage;
    } else {
        MarkMockAccessorWithAttributes(*propAccessor, AccessLevel::PUBLIC);
        propAccessor->linkage = Linkage::EXTERNAL;
    }

    return propAccessor;
}

namespace {

OwnedPtr<RefExpr> CreateRefForFieldAccess(Ptr<Decl> outerDecl, FuncBody& funcBody, AccessorKind kind)
{
    auto outerClassDecl = As<ASTKind::CLASS_DECL>(outerDecl);
    CJC_ASSERT(outerClassDecl);
    if (kind == AccessorKind::STATIC_FIELD_GETTER || kind == AccessorKind::STATIC_FIELD_SETTER) {
        auto ref = CreateRefExpr(*outerClassDecl);
        if (funcBody.generic) {
            for (auto& param : funcBody.generic->typeParameters) {
                ref->instTys.emplace_back(param->ty);
            }
        }
        return ref;
    } else {
        return CreateThisRef(*outerClassDecl);
    }
}

} // namespace

std::vector<OwnedPtr<Node>> MockSupportManager::GenerateFieldGetterAccessorBody(
    VarDecl& fieldDecl, FuncBody& funcBody, AccessorKind kind) const
{
    OwnedPtr<Expr> retExpr;

    if (kind == AccessorKind::TOP_LEVEL_VARIABLE_GETTER) {
        retExpr = CreateRefExpr(fieldDecl);
    } else {
        auto ref = CreateRefForFieldAccess(fieldDecl.outerDecl, funcBody, kind);
        retExpr = CreateMemberAccess(std::move(ref), fieldDecl.identifier);
    }

    std::vector<OwnedPtr<Node>> bodyNodes;
    bodyNodes.emplace_back(CreateReturnExpr(std::move(retExpr), &funcBody));
    return bodyNodes;
}

std::vector<OwnedPtr<Node>> MockSupportManager::GenerateFieldSetterAccessorBody(
    VarDecl& fieldDecl, FuncParam& setterParam, FuncBody& funcBody, AccessorKind kind) const
{
    static const auto UNIT_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    OwnedPtr<Expr> retExpr;

    if (kind == AccessorKind::TOP_LEVEL_VARIABLE_SETTER) {
        retExpr = CreateRefExpr(fieldDecl);
    } else {
        auto ref = CreateRefForFieldAccess(fieldDecl.outerDecl, funcBody, kind);
        retExpr = CreateMemberAccess(std::move(ref), fieldDecl.identifier);
    }

    std::vector<OwnedPtr<Node>> bodyNodes;
    bodyNodes.emplace_back(
        CreateReturnExpr(CreateAssignExpr(std::move(retExpr), CreateRefExpr(setterParam), UNIT_TY), &funcBody));
    return bodyNodes;
}

OwnedPtr<FuncDecl> MockSupportManager::CreateFieldAccessorDecl(
    const VarDecl &fieldDecl, FuncTy *accessorTy, AccessorKind kind) const
{
    OwnedPtr<FuncDecl> accessorDecl = MakeOwned<FuncDecl>();

    accessorDecl->curFile = fieldDecl.curFile;
    accessorDecl->keywordPos = fieldDecl.keywordPos;
    accessorDecl->identifier.SetPos(fieldDecl.identifier.Begin(), fieldDecl.identifier.End());
    accessorDecl->moduleName = fieldDecl.moduleName;
    accessorDecl->fullPackageName = fieldDecl.fullPackageName;
    accessorDecl->outerDecl = As<ASTKind::CLASS_DECL>(fieldDecl.outerDecl);
    accessorDecl->ty = accessorTy;
    accessorDecl->identifier = mockUtils->BuildMockAccessorIdentifier(fieldDecl, kind);
    if (fieldDecl.IsStaticOrGlobal()) {
        MarkMockAccessorWithAttributes(*accessorDecl, GetAccessLevel(fieldDecl));
        accessorDecl->linkage = fieldDecl.linkage;
    } else {
        MarkMockAccessorWithAttributes(*accessorDecl, AccessLevel::PUBLIC);
        accessorDecl->linkage = Linkage::EXTERNAL;
    }

    return accessorDecl;
}

OwnedPtr<FuncDecl> MockSupportManager::CreateForeignFunctionAccessorDecl(FuncDecl& funcDecl) const
{
    static const auto NOTHING_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_NOTHING);

    CJC_ASSERT(funcDecl.TestAttr(Attribute::FOREIGN));
    const auto& funcBody = funcDecl.funcBody;

    CJC_ASSERT(funcDecl.ty->kind == TypeKind::TYPE_FUNC);
    auto funcTy = Ptr(StaticCast<FuncTy>(funcDecl.ty));

    std::vector<OwnedPtr<FuncParamList>> accessorFuncParamLists;
    for (const auto& paramList : funcBody->paramLists) {
        std::vector<OwnedPtr<FuncParam>> accessorParamList;
        for (const auto& param : paramList->params) {
            auto paramDecl = As<ASTKind::FUNC_PARAM>(param);
            CJC_ASSERT(paramDecl);
            auto accessorParamDecl = CreateFuncParam(
                paramDecl->identifier,
                ASTCloner::Clone(paramDecl->type.get()),
                nullptr,
                paramDecl->ty);
            accessorParamList.emplace_back(std::move(accessorParamDecl));
        }
        accessorFuncParamLists.emplace_back(
            CreateFuncParamList(std::move(accessorParamList)));
    }

    std::vector<OwnedPtr<FuncArg>> args;
    for (const auto& paramList : accessorFuncParamLists) {
        for (const auto& param : paramList->params) {
            args.emplace_back(CreateFuncArg(CreateRefExpr(*param)));
        }
    }

    auto accessorFuncRetStmt = CreateReturnExpr(
        CreateCallExpr(CreateRefExpr(funcDecl), std::move(args), nullptr, funcTy->retTy));
    accessorFuncRetStmt->ty = NOTHING_TY;
    std::vector<OwnedPtr<Node>> accessorFuncBodyStmts;
    accessorFuncBodyStmts.emplace_back(std::move(accessorFuncRetStmt));
    auto accessorFuncBodyBlock = CreateBlock(std::move(accessorFuncBodyStmts), NOTHING_TY);

    auto accessorFuncBody = CreateFuncBody(
        std::move(accessorFuncParamLists),
        ASTCloner::Clone(funcBody->retType.get()),
        std::move(accessorFuncBodyBlock),
        funcTy);

    auto accessorName = MockUtils::GetForeignAccessorName(funcDecl) + MockUtils::mockAccessorSuffix;
    auto accessorDecl = CreateFuncDecl(accessorName,  std::move(accessorFuncBody), funcTy);
    accessorDecl->curFile = funcDecl.curFile;
    accessorDecl->fullPackageName = funcDecl.fullPackageName;
    accessorDecl->moduleName = funcDecl.moduleName;
    accessorDecl->EnableAttr(Attribute::PUBLIC);
    accessorDecl->EnableAttr(Attribute::GLOBAL);
    accessorDecl->EnableAttr(Attribute::UNSAFE);
    accessorDecl->EnableAttr(Attribute::GENERATED_TO_MOCK);
    accessorDecl->EnableAttr(Attribute::NO_MANGLE);

    return accessorDecl;
}

OwnedPtr<FuncDecl> MockSupportManager::GenerateVarDeclAccessor(VarDecl& fieldDecl, AccessorKind kind)
{
    CJC_ASSERT(IsFieldOrVariable(kind));

    auto isGetter = mockUtils->IsGeneratedGetter(kind);

    FuncTy* accessorTy = isGetter ?
        typeManager.GetFunctionTy({}, fieldDecl.ty) :
        typeManager.GetFunctionTy({fieldDecl.ty}, TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT));
    std::vector<OwnedPtr<Node>> body;
    std::vector<OwnedPtr<FuncParam>> accessorParams {};

    OwnedPtr<Type> fieldType =
        fieldDecl.type ? ASTCloner::Clone(fieldDecl.type.get()) : MockUtils::CreateType<Type>(fieldDecl.ty);

    auto accessorDecl = CreateFieldAccessorDecl(fieldDecl, accessorTy, kind);
    accessorDecl->funcBody = MakeOwned<FuncBody>();

    if (fieldDecl.IsStaticOrGlobal() && fieldDecl.outerDecl && fieldDecl.outerDecl->generic) {
        accessorDecl->funcBody->generic = MakeOwned<Generic>();
        CopyBasicInfo(fieldDecl.outerDecl->generic, accessorDecl->funcBody->generic);
        for (auto& param : fieldDecl.outerDecl->generic->typeParameters) {
            accessorDecl->funcBody->generic->typeParameters.emplace_back(
                CreateGenericParamDecl(*accessorDecl, param->identifier, typeManager));
        }
    }

    if (isGetter) {
        body = GenerateFieldGetterAccessorBody(fieldDecl, *accessorDecl->funcBody, kind);
    } else {
        auto setterParam = CreateFuncParam("newValue", std::move(fieldType), nullptr, fieldDecl.ty);
        setterParam->outerDecl = accessorDecl.get();
        setterParam->moduleName = fieldDecl.moduleName;
        setterParam->fullPackageName = fieldDecl.fullPackageName;
        setterParam->curFile = fieldDecl.curFile;
        body = GenerateFieldSetterAccessorBody(fieldDecl, *setterParam, *accessorDecl->funcBody, kind);
        accessorParams.emplace_back(std::move(setterParam));
    }

    std::vector<OwnedPtr<FuncParamList>> accessorParamLists {};
    accessorParamLists.emplace_back(CreateFuncParamList(std::move(accessorParams)));

    OwnedPtr<Type> retType = isGetter ?
        std::move(fieldType) : MockUtils::CreateType<PrimitiveType>(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT));

    if (IsStaticField(kind)) {
        accessorDecl->EnableAttr(Attribute::STATIC);
    }
    if (IsTopLevelField(kind)) {
        accessorDecl->EnableAttr(Attribute::GLOBAL);
    }
    accessorDecl->funcBody->ty = accessorTy;
    accessorDecl->funcBody->parentClassLike = As<ASTKind::CLASS_DECL>(fieldDecl.outerDecl);
    accessorDecl->funcBody->funcDecl = accessorDecl.get();
    accessorDecl->funcBody->paramLists = std::move(accessorParamLists);
    accessorDecl->funcBody->body = CreateBlock(std::move(body), accessorTy);
    accessorDecl->funcBody->retType = std::move(retType);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (fieldDecl.ty->IsStruct() && kind == AccessorKind::FIELD_SETTER) {
        accessorDecl->EnableAttr(Attribute::MUT);
    }
#endif
    accessorDecl->mangledName = mockUtils->Mangle(*accessorDecl);

    return accessorDecl;
}

bool MockSupportManager::NeedToSearchCallsToReplaceWithAccessors(Node& node)
{
    // Accessors only contain a call of their original declaration which shouldn't be replaced
    if (auto decl = As<ASTKind::DECL>(&node); decl && MockUtils::IsMockAccessor(*decl)) {
        return false;
    }

    return true;
}

void MockSupportManager::WriteGeneratedMockDecls()
{
    while (!generatedMockDecls.empty()) {
        auto accessorDecl = std::move(generatedMockDecls.extract(generatedMockDecls.begin()).value());
        mockUtils->Instantiate(*accessorDecl);
        if (auto outerDecl = As<ASTKind::CLASS_DECL>(accessorDecl->outerDecl); outerDecl) {
            outerDecl->body->decls.emplace_back(std::move(accessorDecl));
        } else if (accessorDecl->curFile) {
            auto file = accessorDecl->curFile;
            file->decls.emplace_back(std::move(accessorDecl));
            std::rotate(file->decls.rbegin(), file->decls.rbegin() + 1, file->decls.rend());
        }
    }
    generatedMockDecls.clear();
    genericMockVarsDecls.clear();
}

bool MockSupportManager::IsMemberAccessOnThis(const MemberAccess& memberAccess) const
{
    if (!memberAccess.baseExpr) {
        return false;
    }

    auto refBaseExpr = As<ASTKind::REF_EXPR>(memberAccess.baseExpr);

    if (!refBaseExpr) {
        return false;
    }

    return refBaseExpr->isThis;
}

/*
 * For calls involving mut operations on structs,
 * we cannot just replace intermediate member access expression (like field access) with an accessor call,
 * because it causes mutability rules violation.
 * Foe example, `foo.myStructField.mutY(newY)` -> `foo.myStructField$get().mutY(newY)`  <--- this is wrong replacement
 * Instead, we extract that intermediate expression into a mutable variable,
 * then substitute it instead of original expression.
 * And, finally invoke a setter accessor to pass the mutated struct back.
 *
 * Example of desugaring:
 *  myClass.myStruct.mutSomeField()
 *      =>
 *  {
 *      var $tmp1 = myClass.myStruct$get$ToMock()
 *      let $tmp2 = $tmp1.mutSomeField()
 *      myClass.myStruct$set$ToMock($tmp1)
 *      $tmp2
 *  }
 */
void MockSupportManager::TransformAccessorCallForMutOperation(
    NameReferenceExpr& originalNre, Expr& replacedNre, Expr& topLevelExpr)
{
    CJC_ASSERT(Is<AssignExpr>(topLevelExpr) ||
        (Is<CallExpr>(topLevelExpr) &&
            DynamicCast<CallExpr*>(&topLevelExpr)->resolvedFunction->TestAttr(Attribute::MUT)));

    auto tmpVarDecl = CreateTmpVarDecl(
        MockUtils::CreateType<Type>(replacedNre.ty),
        ASTCloner::Clone(Ptr(&replacedNre)));
    tmpVarDecl->isVar = true;

    auto tmpVarRefToMutate = CreateRefExpr(*tmpVarDecl);
    tmpVarRefToMutate->ref.identifier = tmpVarDecl->identifier;
    tmpVarRefToMutate->curFile = replacedNre.curFile;
    tmpVarRefToMutate->ty = originalNre.ty;

    OwnedPtr<Expr> newTopLevelExpr = ASTCloner::Clone(Ptr(&topLevelExpr));
    Ptr<Expr> mutBaseExpr;
    if (auto callExpr = As<ASTKind::CALL_EXPR>(newTopLevelExpr)) {
        mutBaseExpr = callExpr->baseFunc;
    } else if (auto assignExpr = As<ASTKind::ASSIGN_EXPR>(newTopLevelExpr)) {
        mutBaseExpr = assignExpr->leftValue;
    } else {
        CJC_ABORT();
    }
    auto tmpVarRefToAssign = ASTCloner::Clone(tmpVarRefToMutate.get());
    CJC_ASSERT(mutBaseExpr->astKind == ASTKind::MEMBER_ACCESS);
    As<ASTKind::MEMBER_ACCESS>(mutBaseExpr)->baseExpr = std::move(tmpVarRefToMutate);

    auto mutResultVarDecl = CreateTmpVarDecl(
        MockUtils::CreateType<RefType>(newTopLevelExpr->ty), std::move(newTopLevelExpr));
    auto mutResultVarRef = CreateRefExpr(*mutResultVarDecl);
    mutResultVarRef->ref.identifier = mutResultVarDecl->identifier;
    mutResultVarRef->curFile = replacedNre.curFile;

    auto ty = topLevelExpr.ty;

    auto newOriginalMa = ASTCloner::Clone(Ptr(&originalNre));
    newOriginalMa->desugarExpr = nullptr;

    auto backAssignExpr = CreateAssignExpr(std::move(newOriginalMa), std::move(tmpVarRefToAssign));
    backAssignExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    ReplaceFieldSetWithAccessor(*backAssignExpr, false);
    backAssignExpr->EnableAttr(Attribute::GENERATED_TO_MOCK);

    std::vector<OwnedPtr<Node>> nodes {};
    nodes.emplace_back(std::move(tmpVarDecl));
    nodes.emplace_back(std::move(mutResultVarDecl));
    nodes.emplace_back(std::move(backAssignExpr));
    nodes.emplace_back(CreateReturnExpr(std::move(mutResultVarRef), nullptr));

    std::vector<OwnedPtr<FuncParamList>> paramLists {};
    paramLists.emplace_back(CreateFuncParamList(std::vector<Ptr<FuncParam>> {}));

    auto lambda = CreateLambdaExpr(
        CreateFuncBody(
            std::move(paramLists),
            MockUtils::CreateType<Type>(replacedNre.ty), CreateBlock(std::move(nodes), ty), ty)
    );
    lambda->ty = typeManager.GetFunctionTy({}, ty);
    lambda->funcBody->ty = lambda->ty;

    topLevelExpr.desugarExpr = CreateCallExpr(std::move(lambda), {}, nullptr, ty);
}

void MockSupportManager::ReplaceSubMemberAccessWithAccessor(
    const MemberAccess& memberAccess, bool isInConstructor, const Ptr<Expr> topLevelMutExpr)
{
    if (auto nre = DynamicCast<NameReferenceExpr*>(ExtractLastDesugaredExpr(*memberAccess.baseExpr)); nre) {
        auto replacedNre = ReplaceExprWithAccessor(*nre, isInConstructor, true);
        if (topLevelMutExpr && replacedNre) {
            TransformAccessorCallForMutOperation(*nre, *replacedNre, *topLevelMutExpr);
        }
    }
}

Ptr<Expr> MockSupportManager::ReplaceExprWithAccessor(Expr& originalExpr, bool isInConstructor, bool isSubMemberAccess)
{
    auto expr = ExtractLastDesugaredExpr(originalExpr);
    if (auto fieldMemberAccess = As<ASTKind::MEMBER_ACCESS>(expr);
        fieldMemberAccess && fieldMemberAccess->target &&
        fieldMemberAccess->target->astKind == ASTKind::VAR_DECL &&
        fieldMemberAccess->target->astKind != ASTKind::PROP_DECL
    ) {
        // Left values of an assignment are handled below, by `ReplaceFieldSetWithAccessor`
        if (fieldMemberAccess->TestAttr(Attribute::LEFT_VALUE) || (!fieldMemberAccess->isAlone && !isSubMemberAccess)) {
            return nullptr;
        }
        return ReplaceFieldGetWithAccessor(*fieldMemberAccess, isInConstructor);
    } else if (auto assignment = As<ASTKind::ASSIGN_EXPR>(expr); assignment &&
        !assignment->TestAttr(Attribute::GENERATED_TO_MOCK)
    ) {
        // Some left value expressions don't have `LEFT_VALUE` attribute,
        // set the attribute to definitely skip left value expressions themthelves to generate accessor calls
        // as they are handled within whole assign expressions
        assignment->leftValue->EnableAttr(Attribute::LEFT_VALUE);

        // Support all compound assignments
        if (!assignment->isCompound) {
            return ReplaceFieldSetWithAccessor(*assignment, isInConstructor);
        } else {
            return nullptr;
        }
    } else if (auto memberAccess = As<ASTKind::MEMBER_ACCESS>(expr);  memberAccess && memberAccess->target) {
        return ReplaceMemberAccessWithAccessor(*memberAccess, isInConstructor);
    } else if (auto refExpr = As<ASTKind::REF_EXPR>(expr);
        refExpr && refExpr->GetTarget() && !refExpr->TestAttr(Attribute::LEFT_VALUE)
    ) {
        auto target = refExpr->GetTarget();
        if (target->astKind == ASTKind::VAR_DECL && target->TestAttr(Attribute::GLOBAL)) {
            return ReplaceTopLevelVariableGetWithAccessor(*refExpr);
        }
        return nullptr;
    } else {
        return nullptr;
    }
}

Ptr<Expr> MockSupportManager::ReplaceMemberAccessWithAccessor(MemberAccess& memberAccess, bool isInConstructor)
{
    Ptr<Expr> parentMutExpr = nullptr;
    if (auto ce = DynamicCast<CallExpr*>(memberAccess.callOrPattern)) {
        if (auto resolvedFunction = ce->resolvedFunction; resolvedFunction &&
            resolvedFunction->TestAttr(Attribute::MUT) && Is<StructDecl>(resolvedFunction->outerDecl)) {
            parentMutExpr = ce;
        }
    }
    ReplaceSubMemberAccessWithAccessor(memberAccess, isInConstructor, parentMutExpr);

    if (isInConstructor && IsMemberAccessOnThis(memberAccess)) {
        return nullptr;
    }

    if (auto funcDecl = As<ASTKind::FUNC_DECL>(memberAccess.target); funcDecl && funcDecl->propDecl) {
        auto propDeclToMock = As<ASTKind::PROP_DECL>(
            mockUtils->FindAccessorForMemberAccess(memberAccess, funcDecl->propDecl, {}, AccessorKind::METHOD));
        if (!propDeclToMock) {
            return nullptr;
        }
        if (funcDecl->isGetter) {
            memberAccess.target = GetUsableGetterForProperty(*propDeclToMock);
        } else if (funcDecl->isSetter) {
            memberAccess.target = GetUsableSetterForProperty(*propDeclToMock);
        }
    } else if (auto funcDeclToMock = mockUtils->FindAccessorForMemberAccess(
        memberAccess, memberAccess.target, memberAccess.instTys, AccessorKind::METHOD); funcDeclToMock) {
        memberAccess.target = funcDeclToMock;
        if (auto callExpr = DynamicCast<CallExpr>(memberAccess.callOrPattern)) {
            callExpr->resolvedFunction = StaticCast<FuncDecl>(funcDeclToMock);
        }
    }

    // No desugared expr generated here (instead, target is replaced) so return original member access
    return &memberAccess;
}

Ptr<Expr> MockSupportManager::ReplaceFieldGetWithAccessor(MemberAccess& memberAccess, bool isInConstructor)
{
    ReplaceSubMemberAccessWithAccessor(memberAccess, isInConstructor);

    if (!memberAccess.target || (isInConstructor && IsMemberAccessOnThis(memberAccess))) {
        return nullptr;
    }

    if (auto accessorCall = GenerateAccessorCallForField(memberAccess, AccessorKind::FIELD_GETTER); accessorCall) {
        accessorCall->sourceExpr = Ptr(&memberAccess);
        memberAccess.desugarExpr = std::move(accessorCall);
        return memberAccess.desugarExpr;
    }

    return nullptr;
}

Ptr<Expr> MockSupportManager::ReplaceTopLevelVariableGetWithAccessor(RefExpr& refExpr)
{
    if (!refExpr.GetTarget()) {
        return nullptr;
    }

    if (auto accessorCall = GenerateAccessorCallForTopLevelVariable(
        refExpr, AccessorKind::TOP_LEVEL_VARIABLE_GETTER); accessorCall
    ) {
        accessorCall->sourceExpr = Ptr(&refExpr);
        refExpr.desugarExpr = std::move(accessorCall);
        return refExpr.desugarExpr;
    }

    return nullptr;
}

Ptr<Expr> MockSupportManager::ReplaceFieldSetWithAccessor(AssignExpr& assignExpr, bool isInConstructor)
{
    auto leftValue = assignExpr.leftValue.get();
    OwnedPtr<CallExpr> accessorCall;
    if (auto refExpr = As<ASTKind::REF_EXPR>(leftValue); refExpr) {
        if (!refExpr->GetTarget()) {
            return nullptr;
        }
        accessorCall = GenerateAccessorCallForTopLevelVariable(*refExpr, AccessorKind::TOP_LEVEL_VARIABLE_SETTER);
        if (!accessorCall) {
            return nullptr;
        }
    } else if (auto memberAccess = As<ASTKind::MEMBER_ACCESS>(leftValue); memberAccess) {
        if (!memberAccess->target || (isInConstructor && IsMemberAccessOnThis(*memberAccess))) {
            return nullptr;
        }
        accessorCall = GenerateAccessorCallForField(*memberAccess, AccessorKind::FIELD_SETTER);
        if (!accessorCall) {
            ReplaceSubMemberAccessWithAccessor(
                *memberAccess, isInConstructor,
                Is<StructDecl>(memberAccess->target->outerDecl) ? &assignExpr : nullptr);
            return &assignExpr;
        }
    } else if (leftValue->astKind == ASTKind::WILDCARD_EXPR) {
        return nullptr;
    }

    CJC_ASSERT(accessorCall);
    accessorCall->args.emplace_back(CreateFuncArg(ASTCloner::Clone(assignExpr.rightExpr.get())));
    accessorCall->sourceExpr = Ptr(&assignExpr);
    assignExpr.desugarExpr = std::move(accessorCall);
    return assignExpr.desugarExpr;
}

OwnedPtr<CallExpr> MockSupportManager::GenerateAccessorCallForTopLevelVariable(
    const RefExpr& refExpr, AccessorKind kind)
{
    auto accessorDecl = mockUtils->FindTopLevelAccessor(refExpr.GetTarget(), kind);
    if (!accessorDecl) {
        return nullptr;
    }

    auto accessorCall = MakeOwned<CallExpr>();

    accessorCall->ty = RawStaticCast<const FuncTy*>(accessorDecl->ty)->retTy;
    accessorCall->resolvedFunction = accessorDecl;
    accessorCall->baseFunc = CreateRefExpr(*accessorDecl);
    std::vector<OwnedPtr<FuncArg>> mockedMethodArgRefs {};
    accessorCall->args = std::move(mockedMethodArgRefs);
    accessorCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    accessorCall->curFile = refExpr.curFile;

    return accessorCall;
}

OwnedPtr<CallExpr> MockSupportManager::GenerateAccessorCallForField(
    const MemberAccess& memberAccess, AccessorKind kind)
{
    Ptr<FuncDecl> accessorDecl = As<ASTKind::FUNC_DECL>(
        mockUtils->FindAccessorForMemberAccess(memberAccess, memberAccess.target, memberAccess.instTys, kind));

    if (!accessorDecl) {
        return nullptr;
    }

    auto maTy = memberAccess.ty;
    auto accessorCall = MakeOwned<CallExpr>();
    auto accessorMemberAccess = CreateMemberAccess(
        ASTCloner::Clone(memberAccess.baseExpr.get()), accessorDecl->identifier);

    accessorMemberAccess->curFile = memberAccess.curFile;
    accessorMemberAccess->target = accessorDecl;
    accessorMemberAccess->callOrPattern = accessorCall.get();

    if (kind == AccessorKind::FIELD_GETTER) {
        accessorMemberAccess->ty = typeManager.GetFunctionTy({}, maTy);
        accessorCall->ty = maTy;
    } else {
        accessorMemberAccess->ty = typeManager.GetFunctionTy({maTy}, TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT));
        accessorCall->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    }

    accessorCall->resolvedFunction = accessorDecl;
    accessorCall->baseFunc = std::move(accessorMemberAccess);
    std::vector<OwnedPtr<FuncArg>> mockedMethodArgRefs {};
    accessorCall->args = std::move(mockedMethodArgRefs);
    accessorCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    accessorCall->curFile = memberAccess.curFile;

    return accessorCall;
}

std::vector<Ptr<Ty>> MockSupportManager::CloneFuncDecl(Ptr<FuncDecl> fromDecl, Ptr<FuncDecl> toDecl)
{
    CopyBasicInfo(fromDecl, toDecl);
    toDecl->CloneAttrs(*fromDecl);
    toDecl->linkage = fromDecl->linkage;
    toDecl->fullPackageName = fromDecl->fullPackageName;
    toDecl->identifier = fromDecl->identifier;
    toDecl->outerDecl = fromDecl->outerDecl;

    toDecl->funcBody = MakeOwned<FuncBody>();

    auto genericTys = mockUtils->AddGenericIfNeeded(*fromDecl, *toDecl);
    auto typeSubsts = GenerateTypeMapping(*fromDecl, genericTys);

    toDecl->ty = typeManager.GetInstantiatedTy(fromDecl->ty, typeSubsts);

    toDecl->funcBody->ty = toDecl->ty;
    toDecl->funcBody->retType = ASTCloner::Clone(fromDecl->funcBody->retType.get());
    toDecl->funcBody->funcDecl = toDecl;
    toDecl->funcBody->parentClassLike = toDecl->funcBody->parentClassLike;

    auto paramList = MakeOwned<FuncParamList>();
    CopyBasicInfo(fromDecl->funcBody->paramLists[0], paramList);
    for (auto& param : fromDecl->funcBody->paramLists[0]->params) {
        auto clonedParam = MakeOwned<FuncParam>();
        CopyBasicInfo(param, clonedParam);
        clonedParam->CloneAttrs(*param);
        clonedParam->ty = typeManager.GetInstantiatedTy(param->ty, typeSubsts);
        clonedParam->type = ASTCloner::Clone(param->type.get());
        clonedParam->outerDecl = toDecl;
        clonedParam->identifier = param->identifier;
        paramList->params.emplace_back(std::move(clonedParam));
    }
    toDecl->funcBody->paramLists.emplace_back(std::move(paramList));

    return genericTys;
}

void MockSupportManager::PrepareInterfaceDecl(InterfaceDecl& interfaceDecl)
{
    if (IS_GENERIC_INSTANTIATION_ENABLED) {
        // Do not support CJVM for now
        return;
    }

    if (interfaceDecl.TestAttr(Attribute::GENERIC)) {
        // Generic interface not yet supported
        return;
    }

    std::vector<Ptr<FuncDecl>> funcDeclsWithDefault;
    for (auto& decl : interfaceDecl.GetMemberDecls()) {
        auto funcDecl = DynamicCast<FuncDecl>(decl.get());
        if (!funcDecl || !funcDecl->TestAttr(Attribute::DEFAULT)) {
            continue;
        }

        funcDeclsWithDefault.push_back(funcDecl);
    }

    if (funcDeclsWithDefault.empty()) {
        return;
    }

    auto accessorInterface = MakeOwned<InterfaceDecl>();
    CopyBasicInfo(&interfaceDecl, accessorInterface);
    accessorInterface->CloneAttrs(interfaceDecl);
    accessorInterface->identifier = interfaceDecl.identifier + MockUtils::defaultAccessorSuffix;
    accessorInterface->inheritedTypes.emplace_back(MockUtils::CreateType<Type>(interfaceDecl.ty));
    accessorInterface->ty = typeManager.GetInterfaceTy(*accessorInterface, {});
    accessorInterface->fullPackageName = interfaceDecl.fullPackageName;
    accessorInterface->linkage = interfaceDecl.linkage;

    accessorInterface->body = MakeOwned<InterfaceBody>();
    CopyBasicInfo(interfaceDecl.body, accessorInterface->body);

    for (auto funcDecl : funcDeclsWithDefault) {
        auto accessorDecl = MakeOwned<FuncDecl>();
        CloneFuncDecl(funcDecl, accessorDecl);
        accessorDecl->outerDecl = accessorInterface;
        accessorDecl->funcBody->parentClassLike = accessorInterface;
        accessorDecl->identifier = mockUtils->Mangle(*funcDecl) + MockUtils::defaultAccessorSuffix;

        CJC_ASSERT(accessorDecl->TestAttr(Attribute::DEFAULT));
        accessorDecl->DisableAttr(Attribute::DEFAULT);
        accessorDecl->EnableAttr(Attribute::ABSTRACT);
        accessorDecl->EnableAttr(Attribute::GENERATED_TO_MOCK);

        accessorInterface->body->decls.emplace_back(std::move(accessorDecl));
    }

    interfaceDecl.EnableAttr(Attribute::MOCK_SUPPORTED);
    generatedMockDecls.emplace(std::move(accessorInterface));
}

template <typename T>
Ptr<T> MockSupportManager::FindGeneratedGlobalDecl(Ptr<AST::File> file, const std::string& identifier)
{
    if (auto decl = MockUtils::FindGlobalDecl<T>(file, identifier)) {
        return decl;
    }

    for (auto& decl : generatedMockDecls) {
        if (decl->identifier == identifier) {
            return DynamicCast<T>(decl.get());
        }
    }

    return nullptr;
}

void MockSupportManager::PrepareClassWithDefaults(ClassDecl& classDecl, InterfaceDecl& interfaceDecl)
{
    Ptr<InterfaceDecl> accessorInterfaceDecl = FindGeneratedGlobalDecl<InterfaceDecl>(
        interfaceDecl.curFile, interfaceDecl.identifier + MockUtils::defaultAccessorSuffix);
    if (!accessorInterfaceDecl) {
        // Interface is in package, which was compiled without mocking support
        return;
    }

    auto extendDecl = MakeOwned<ExtendDecl>();
    CopyBasicInfo(&classDecl, extendDecl);
    extendDecl->extendedType = MockUtils::CreateType<RefType>(classDecl.ty);
    extendDecl->inheritedTypes.emplace_back(MockUtils::CreateType<RefType>(accessorInterfaceDecl->ty));
    extendDecl->ty = classDecl.ty;
    extendDecl->linkage = accessorInterfaceDecl->linkage;
    extendDecl->fullPackageName = classDecl.fullPackageName;

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto manglerCtxIt = mockUtils->mangler.manglerCtxTable.find(
        ManglerContext::ReduceUnitTestPackageName(extendDecl->fullPackageName));
    CJC_ASSERT(manglerCtxIt != mockUtils->mangler.manglerCtxTable.end());
    manglerCtxIt->second->SaveExtend2CurFile(extendDecl->curFile, extendDecl);
#endif

    // Declarations with default in original interface are in the same order
    // as in accessor interface
    auto& accessorDecls = accessorInterfaceDecl->GetMemberDecls();
    auto& originalDecls = interfaceDecl.GetMemberDecls();

    auto accessorDeclIt = accessorDecls.begin();
    auto originalDeclIt = originalDecls.begin();

    while (accessorDeclIt != accessorDecls.end()) {
        CJC_ASSERT(originalDeclIt != originalDecls.end());
        auto originalFunc = DynamicCast<FuncDecl>(originalDeclIt->get());
        if (!originalFunc || !originalFunc->TestAttr(Attribute::DEFAULT)) {
            originalDeclIt++;
            continue;
        }

        auto accessorDecl = As<ASTKind::FUNC_DECL>(*accessorDeclIt);
        CJC_ASSERT(accessorDecl);
        CJC_ASSERT(accessorDecl->identifier == mockUtils->Mangle(*originalFunc) + MockUtils::defaultAccessorSuffix);

        auto accessorImplDecl = MakeOwned<FuncDecl>();
        auto implTypeParamsTy = CloneFuncDecl(accessorDecl, accessorImplDecl);

        accessorImplDecl->identifier = accessorDecl->identifier;
        accessorImplDecl->outerDecl = extendDecl;
        accessorImplDecl->curFile = extendDecl->curFile;
        if (accessorDecl->TestAttr(Attribute::STATIC)) {
            accessorImplDecl->EnableAttr(Attribute::REDEF);
        } else {
            CJC_ASSERT(accessorImplDecl->TestAttr(Attribute::ABSTRACT));
        }
        accessorImplDecl->DisableAttr(Attribute::ABSTRACT);
        accessorImplDecl->EnableAttr(Attribute::GENERATED_TO_MOCK);

        std::vector<OwnedPtr<FuncArg>> args;
        for (auto& param : accessorImplDecl->funcBody->paramLists[0]->params) {
            args.emplace_back(CreateFuncArg(CreateRefExpr(*param)));
        }

        OwnedPtr<RefExpr> receiver;
        if (accessorDecl->TestAttr(Attribute::STATIC)) {
            receiver = CreateRefExpr(classDecl);
        } else {
            receiver = CreateThisRef(classDecl);
        }

        auto memberAccess = CreateMemberAccess(std::move(receiver), *originalFunc);
        memberAccess->ty = typeManager.GetInstantiatedTy(
            originalFunc->ty, GenerateTypeMapping(*originalFunc, implTypeParamsTy));
        for (auto& tyParam : implTypeParamsTy) {
            memberAccess->instTys.emplace_back(tyParam);
        }
        memberAccess->EnableAttr(Attribute::GENERATED_TO_MOCK);

        auto callExpr = CreateCallExpr(
            std::move(memberAccess),
            std::move(args),
            originalFunc,
            accessorImplDecl->funcBody->retType->ty,
            CallKind::CALL_DECLARED_FUNCTION);
        callExpr->EnableAttr(Attribute::GENERATED_TO_MOCK);

        accessorImplDecl->funcBody->body = MakeOwned<Block>();
        accessorImplDecl->funcBody->body->ty = accessorImplDecl->funcBody->retType->ty;
        accessorImplDecl->funcBody->body->body.emplace_back(CreateReturnExpr(std::move(callExpr)));

        PrepareStaticDecl(*accessorImplDecl);
        extendDecl->members.emplace_back(std::move(accessorImplDecl));

        accessorDeclIt++;
        originalDeclIt++;
    }

    typeManager.declToExtendMap[&classDecl].insert(extendDecl.get());
    defaultInterfaceAccessorExtends[classDecl.ty].insert(accessorInterfaceDecl->ty);
    generatedMockDecls.emplace(std::move(extendDecl));
}

namespace {

OwnedPtr<Expr> CreateBoolMatch(
    OwnedPtr<Expr> selector, OwnedPtr<Expr> trueBranch, OwnedPtr<Expr> falseBranch, Ptr<Ty> ty)
{
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    OwnedPtr<ConstPattern> truePattern = MakeOwned<ConstPattern>();
    truePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "true", BOOL_TY);
    truePattern->ty = BOOL_TY;

    OwnedPtr<ConstPattern> falsePattern = MakeOwned<ConstPattern>();
    falsePattern->literal = CreateLitConstExpr(LitConstKind::BOOL, "false", BOOL_TY);
    falsePattern->ty = BOOL_TY;

    auto caseTrue = CreateMatchCase(std::move(truePattern), std::move(trueBranch));
    auto caseFalse = CreateMatchCase(std::move(falsePattern), std::move(falseBranch));

    std::vector<OwnedPtr<MatchCase>> matchCases;
    matchCases.emplace_back(std::move(caseTrue));
    matchCases.emplace_back(std::move(caseFalse));
    return CreateMatchExpr(std::move(selector), std::move(matchCases), ty);
}

} // namespace


std::tuple<Ptr<InterfaceDecl>, Ptr<FuncDecl>> MockSupportManager::FindDefaultAccessorInterfaceAndFunction(
    Ptr<FuncDecl> original)
{
    auto interfaceDecl = DynamicCast<InterfaceDecl>(original->outerDecl);
    if (!interfaceDecl) {
        return {nullptr, nullptr};
    }

    if (!original->TestAttr(Attribute::DEFAULT)) {
        return {nullptr, nullptr};
    }

    Ptr<InterfaceDecl> accessorInterfaceDecl = FindGeneratedGlobalDecl<InterfaceDecl>(
        interfaceDecl->curFile, interfaceDecl->identifier + MockUtils::defaultAccessorSuffix);
    if (!accessorInterfaceDecl) {
        return {nullptr, nullptr};
    }

    Ptr<FuncDecl> accessorDecl = MockUtils::FindMemberDecl<FuncDecl>(
        *accessorInterfaceDecl, mockUtils->Mangle(*original) + MockUtils::defaultAccessorSuffix);

    return {accessorInterfaceDecl, accessorDecl};
}

Ptr<AST::FuncDecl> MockSupportManager::FindDefaultAccessorImplementation(
    Ptr<AST::Decl> baseDecl, Ptr<AST::FuncDecl> accessorDecl)
{
    CJC_NULLPTR_CHECK(baseDecl);
    auto extendDecl = typeManager.GetExtendDeclByMember(*accessorDecl, *baseDecl->ty);
    return MockUtils::FindMemberDecl<FuncDecl>(*extendDecl, accessorDecl->identifier);
}

void MockSupportManager::ReplaceInterfaceDefaultFunc(
    AST::Expr& originalExpr, Ptr<Decl> outerClassLike, bool isInMockAnnotatedLambda)
{
    auto expr = ExtractLastDesugaredExpr(originalExpr);
    if (expr->TestAttr(Attribute::GENERATED_TO_MOCK)) {
        return;
    }

    auto nameRefExpr = DynamicCast<NameReferenceExpr>(expr);
    if (!nameRefExpr) {
        return;
    }
    if (nameRefExpr->callOrPattern) {
        ReplaceInterfaceDefaultFuncInCall(
            *nameRefExpr->callOrPattern, outerClassLike, isInMockAnnotatedLambda);
        return;
    }

    auto target = nameRefExpr->GetTarget();
    if (!target) {
        return;
    }

    auto funcDecl = DynamicCast<FuncDecl>(target);
    if (!funcDecl || !funcDecl->outerDecl) {
        return;
    }

    CJC_ASSERT(!isInMockAnnotatedLambda);

    auto [buddyInterfaceDecl, buddyFuncDecl] = FindDefaultAccessorInterfaceAndFunction(funcDecl);
    if (!buddyInterfaceDecl || !buddyFuncDecl) {
        return;
    }

    if (auto maExpr = DynamicCast<MemberAccess>(nameRefExpr)) {
        if (funcDecl->TestAttr(Attribute::STATIC)) {
            if (auto genericTy = DynamicCast<GenericsTy>(maExpr->baseExpr->ty)) {
                // T.foo |-> match (IsSubtypeTypes<T, I$Buddy>()) {
                //   case true => T.foo$Buddy
                //   case false => T.foo
                // }
                genericTy->upperBounds.insert(buddyInterfaceDecl->ty);

                auto selector = mockUtils->CreateIsSubtypeTypesCall(maExpr->baseExpr->ty, buddyInterfaceDecl->ty);

                auto buddyMa = CreateMemberAccess(ASTCloner::Clone(Ptr(maExpr->baseExpr.get())), *buddyFuncDecl);
                CopyBasicInfo(maExpr, buddyMa);
                buddyMa->EnableAttr(Attribute::GENERATED_TO_MOCK);
                buddyMa->ty = typeManager.GetInstantiatedTy(
                    buddyFuncDecl->ty, GenerateTypeMapping(*buddyFuncDecl, maExpr->instTys));
                buddyMa->instTys = maExpr->instTys;

                auto originalMa = ASTCloner::Clone(Ptr(maExpr));
                originalMa->EnableAttr(Attribute::GENERATED_TO_MOCK);

                auto matchExpr = CreateBoolMatch(
                    std::move(selector), std::move(buddyMa), std::move(originalMa), maExpr->ty);

                maExpr->desugarExpr = std::move(matchExpr);
            } else if (HasDefaultInterfaceAccessor(maExpr->baseExpr->ty, buddyInterfaceDecl->ty)) {
                // C.foo |-> C.foo$Buddy
                auto buddyFuncImplDecl =
                    FindDefaultAccessorImplementation(maExpr->baseExpr->GetTarget(), buddyFuncDecl);
                auto buddyMa = CreateMemberAccess(ASTCloner::Clone(Ptr(maExpr->baseExpr.get())), *buddyFuncImplDecl);
                CopyBasicInfo(maExpr, buddyMa);
                buddyMa->EnableAttr(Attribute::GENERATED_TO_MOCK);
                buddyMa->ty = typeManager.GetInstantiatedTy(
                    buddyFuncImplDecl->ty, GenerateTypeMapping(*buddyFuncImplDecl, maExpr->instTys));
                buddyMa->instTys = maExpr->instTys;
                maExpr->desugarExpr = std::move(buddyMa);
            }
        } else {
            // a.foo |->
            // let tmp = a
            // match (a) {
            //   case v : I$Buddy => v.foo$Buddy
            //   case _ => tmp.foo
            // }
            auto baseExprVar = CreateTmpVarDecl(
                MockUtils::CreateType<Type>(maExpr->baseExpr->ty), ASTCloner::Clone(maExpr->baseExpr.get()));

            auto createBuddyMa = [this, maExpr, &funcDecl = *buddyFuncDecl](Ptr<VarDecl> castedExpr) {
                auto ma = CreateMemberAccess(CreateRefExpr(*castedExpr), funcDecl);
                CopyBasicInfo(maExpr, ma);
                ma->ty = typeManager.GetInstantiatedTy(funcDecl.ty, GenerateTypeMapping(funcDecl, maExpr->instTys));
                ma->instTys = maExpr->instTys;
                return ma;
            };
            auto originalMa = ASTCloner::Clone(Ptr(maExpr));
            originalMa->EnableAttr(Attribute::GENERATED_TO_MOCK);
            auto matchExpr = MockUtils::CreateTypeCast(
                CreateRefExpr(*baseExprVar), buddyInterfaceDecl->ty,
                std::move(createBuddyMa), std::move(originalMa), maExpr->ty);

            auto blockExpr = CreateBlock({}, maExpr->ty);
            blockExpr->body.emplace_back(std::move(baseExprVar));
            blockExpr->body.emplace_back(std::move(matchExpr));

            maExpr->desugarExpr = std::move(blockExpr);
        }

        return;
    } else if (auto refExpr = DynamicCast<RefExpr>(expr)) {
        if (funcDecl->TestAttr(Attribute::STATIC)) {
            if (!outerClassLike || HasDefaultInterfaceAccessor(outerClassLike->ty, buddyInterfaceDecl->ty)) {
                // foo |-> foo$Buddy
                auto buddyFuncImplDecl =
                    FindDefaultAccessorImplementation(outerClassLike, buddyFuncDecl);
                auto buddyFuncRef = CreateRefExpr(*buddyFuncImplDecl);
                CopyBasicInfo(refExpr, buddyFuncRef);
                buddyFuncRef->ty = typeManager.GetInstantiatedTy(
                    buddyFuncImplDecl->ty, GenerateTypeMapping(*buddyFuncImplDecl, refExpr->instTys));
                buddyFuncRef->instTys = refExpr->instTys;

                refExpr->desugarExpr = std::move(buddyFuncRef);
            }
        } else {
            // foo |-> match (this) {
            //   case v : I$Buddy => v.foo$Buddy
            //   case _ => foo
            // }
            auto thisExpr = CreateThisRef(*funcDecl->funcBody->parentClassLike);
            CopyBasicInfo(refExpr, thisExpr);
            thisExpr->EnableAttr(Attribute::GENERATED_TO_MOCK);

            auto createBuddyMa = [this, refExpr, &funcDecl = *buddyFuncDecl](Ptr<VarDecl> castedExpr) {
                auto ma = CreateMemberAccess(CreateRefExpr(*castedExpr), funcDecl);
                CopyBasicInfo(refExpr, ma);
                ma->ty = typeManager.GetInstantiatedTy(
                    funcDecl.ty, GenerateTypeMapping(funcDecl, refExpr->instTys));
                ma->instTys = refExpr->instTys;
                return ma;
            };
            auto originalRef = ASTCloner::Clone(Ptr(refExpr));
            originalRef->EnableAttr(Attribute::GENERATED_TO_MOCK);
            auto matchExpr = MockUtils::CreateTypeCast(
                std::move(thisExpr), buddyInterfaceDecl->ty,
                std::move(createBuddyMa), std::move(originalRef), refExpr->ty);

            refExpr->desugarExpr = std::move(matchExpr);
        }
    }
}

void MockSupportManager::ReplaceInterfaceDefaultFuncInCall(
    AST::Node& node, Ptr<Decl> outerClassLike, bool isInMockAnnotatedLambda)
{
    if (isInMockAnnotatedLambda) {
        return;
    }

    auto originalExpr = DynamicCast<Expr>(&node);
    if (!originalExpr) {
        return;
    }

    auto expr = ExtractLastDesugaredExpr(*originalExpr);
    if (expr->TestAttr(Attribute::GENERATED_TO_MOCK)) {
        return;
    }

    auto callExpr = DynamicCast<CallExpr>(expr);
    if (!callExpr || !callExpr->resolvedFunction) {
        return;
    }

    auto [buddyInterfaceDecl, buddyFuncDecl] = FindDefaultAccessorInterfaceAndFunction(callExpr->resolvedFunction);
    if (!buddyInterfaceDecl || !buddyFuncDecl) {
        return;
    }

    if (auto maExpr = DynamicCast<MemberAccess>(callExpr->baseFunc.get())) {
        if (callExpr->resolvedFunction->TestAttr(Attribute::STATIC)) {
            if (auto genericTy = DynamicCast<GenericsTy>(maExpr->baseExpr->ty)) {
                // T.foo() |-> match (IsSubtypeTypes<T, I$Buddy>()) {
                //   case true => T.foo$Buddy()
                //   case false => T.foo()
                // }
                genericTy->upperBounds.insert(buddyInterfaceDecl->ty);

                auto selector = mockUtils->CreateIsSubtypeTypesCall(maExpr->baseExpr->ty, buddyInterfaceDecl->ty);

                auto buddyMa = CreateMemberAccess(ASTCloner::Clone(Ptr(maExpr->baseExpr.get())), *buddyFuncDecl);
                CopyBasicInfo(maExpr, buddyMa);
                buddyMa->EnableAttr(Attribute::GENERATED_TO_MOCK);
                buddyMa->ty = typeManager.GetInstantiatedTy(
                    buddyFuncDecl->ty, GenerateTypeMapping(*buddyFuncDecl, maExpr->instTys));
                buddyMa->instTys = maExpr->instTys;

                auto buddyCall = ASTCloner::Clone(Ptr(callExpr));
                buddyCall->baseFunc = std::move(buddyMa);
                buddyCall->resolvedFunction = buddyFuncDecl;

                auto originalCall = ASTCloner::Clone(Ptr(callExpr));
                originalCall->EnableAttr(Attribute::GENERATED_TO_MOCK);

                auto retTy = StaticCast<FuncTy*>(maExpr->ty)->retTy;
                auto matchExpr = CreateBoolMatch(
                    std::move(selector), std::move(buddyCall), std::move(originalCall), retTy);

                callExpr->desugarExpr = std::move(matchExpr);
            } else if (HasDefaultInterfaceAccessor(maExpr->baseExpr->ty, buddyInterfaceDecl->ty)) {
                // C.foo() |-> C.foo$Buddy()
                auto buddyFuncImplDecl =
                    FindDefaultAccessorImplementation(maExpr->baseExpr->GetTarget(), buddyFuncDecl);
                auto buddyMa = CreateMemberAccess(ASTCloner::Clone(Ptr(maExpr->baseExpr.get())), *buddyFuncImplDecl);
                CopyBasicInfo(maExpr, buddyMa);
                buddyMa->EnableAttr(Attribute::GENERATED_TO_MOCK);
                buddyMa->ty = typeManager.GetInstantiatedTy(
                    buddyFuncImplDecl->ty, GenerateTypeMapping(*buddyFuncImplDecl, maExpr->instTys));
                buddyMa->instTys = maExpr->instTys;

                auto buddyCall = ASTCloner::Clone(Ptr(callExpr));
                buddyCall->baseFunc = std::move(buddyMa);
                buddyCall->resolvedFunction = buddyFuncImplDecl;

                callExpr->desugarExpr = std::move(buddyCall);
            }
        } else {
            // a.foo() |->
            // let tmp = a
            // match (a) {
            //   case v : I$Buddy => v.foo$Buddy()
            //   case _ => tmp.foo()
            // }
            auto baseExprVar = CreateTmpVarDecl(
                MockUtils::CreateType<Type>(maExpr->baseExpr->ty), ASTCloner::Clone(maExpr->baseExpr.get()));

            auto createBuddyCall = [this, &maExpr, &funcDecl = *buddyFuncDecl, callExpr](Ptr<VarDecl> castedExpr) {
                auto ma = CreateMemberAccess(CreateRefExpr(*castedExpr), funcDecl);
                ma->ty = typeManager.GetInstantiatedTy(funcDecl.ty, GenerateTypeMapping(funcDecl, maExpr->instTys));
                ma->instTys = maExpr->instTys;

                auto call = ASTCloner::Clone(Ptr(callExpr));
                call->baseFunc = std::move(ma);
                call->resolvedFunction = &funcDecl;
                return call;
            };
            auto originalCall = ASTCloner::Clone(Ptr(callExpr));
            originalCall->EnableAttr(Attribute::GENERATED_TO_MOCK);
            auto retTy = StaticCast<FuncTy*>(maExpr->ty)->retTy;
            auto matchExpr = MockUtils::CreateTypeCast(
                CreateRefExpr(*baseExprVar), buddyInterfaceDecl->ty,
                std::move(createBuddyCall), std::move(originalCall), retTy);

            auto blockExpr = CreateBlock({}, callExpr->ty);
            blockExpr->body.emplace_back(std::move(baseExprVar));
            blockExpr->body.emplace_back(std::move(matchExpr));

            callExpr->desugarExpr = std::move(blockExpr);
        }

        return;
    } else if (auto refExpr = DynamicCast<RefExpr>(callExpr->baseFunc.get())) {
        if (callExpr->resolvedFunction->TestAttr(Attribute::STATIC)) {
            if (!outerClassLike || HasDefaultInterfaceAccessor(outerClassLike->ty, buddyInterfaceDecl->ty)) {
                // foo() |-> foo$Buddy()
                auto buddyFuncImplDecl =
                    FindDefaultAccessorImplementation(outerClassLike, buddyFuncDecl);
                auto buddyFuncRef = CreateRefExpr(*buddyFuncImplDecl);
                CopyBasicInfo(refExpr, buddyFuncRef);
                buddyFuncRef->ty = typeManager.GetInstantiatedTy(
                    buddyFuncImplDecl->ty, GenerateTypeMapping(*buddyFuncImplDecl, refExpr->instTys));
                buddyFuncRef->instTys = refExpr->instTys;

                auto buddyCallExpr = ASTCloner::Clone(Ptr(callExpr));
                buddyCallExpr->baseFunc = std::move(buddyFuncRef);
                buddyCallExpr->resolvedFunction = buddyFuncImplDecl;

                callExpr->desugarExpr = std::move(buddyCallExpr);
            }
        } else {
            // foo() |-> match (this) {
            //   case v : I$Buddy => v.foo$Buddy()
            //   case _ => foo()
            // }
            auto thisExpr = CreateThisRef(*callExpr->resolvedFunction->funcBody->parentClassLike);
            CopyBasicInfo(refExpr, thisExpr);
            thisExpr->EnableAttr(Attribute::GENERATED_TO_MOCK);

            auto createBuddyCall = [this, refExpr, &funcDecl = *buddyFuncDecl, callExpr](Ptr<VarDecl> castedExpr) {
                auto ma = CreateMemberAccess(CreateRefExpr(*castedExpr), funcDecl);
                ma->ty = typeManager.GetInstantiatedTy(
                    funcDecl.ty, GenerateTypeMapping(funcDecl, refExpr->instTys));
                ma->instTys = refExpr->instTys;

                auto call = ASTCloner::Clone(Ptr(callExpr));
                call->baseFunc = std::move(ma);
                call->resolvedFunction = &funcDecl;
                return call;
            };
            auto originalCall = ASTCloner::Clone(Ptr(callExpr));
            originalCall->EnableAttr(Attribute::GENERATED_TO_MOCK);
            auto retTy = StaticCast<FuncTy*>(refExpr->ty)->retTy;
            auto matchExpr = MockUtils::CreateTypeCast(
                std::move(thisExpr), buddyInterfaceDecl->ty,
                std::move(createBuddyCall), std::move(originalCall), retTy);

            callExpr->desugarExpr = std::move(matchExpr);
        }

        return;
    }
}

bool MockSupportManager::HasDefaultInterfaceAccessor(Ptr<AST::Ty> declTy, Ptr<AST::Ty> accessorInterfaceDeclTy)
{
    if (typeManager.IsSubtype(declTy, accessorInterfaceDeclTy)) {
        return true;
    }
    return defaultInterfaceAccessorExtends[declTy].count(accessorInterfaceDeclTy) != 0;
}

}
