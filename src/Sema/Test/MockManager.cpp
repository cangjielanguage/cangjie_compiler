// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "MockManager.h"

#include "TypeCheckUtil.h"
#include "Desugar/AfterTypeCheck.h"

#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Sema/TestManager.h"

namespace Cangjie {

namespace {

using namespace AST;
using namespace TypeCheckUtil;

/*
 * Below are specified names to interfact with intermediate mocking framework API,
 * all declarations with these names are a part of a contract between the compiler and mocking framework,
 * they should be located in std.unittest.mock package.
 * All these declarations are given in std/unittest/mock/mock.cj
 */

static const std::string CREATE_MOCK_FUNC_NAME = "createMock";
static const std::string CREATE_SPY_FUNC_NAME = "createSpy";

static const std::string CALL_HANDLER_INTERFACE_NAME = "CallHandler";
static const std::string CURRENT_STATIC_FUNC_NAME = "currentStatic";

static const std::string MOCKED_INTERFACE_NAME = "Mocked";
static const std::string GET_HANDLER_METHOD_NAME = "getHandler";

static const std::string REQUIRE_MOCK_OBJ_NAME = "requireMockObject";

static const std::string CALL_STRUCT_NAME = "Call";
static const std::string FUNC_INFO_STRUCT_NAME = "FuncInfo";
static const std::string DECL_ID_STRUCT_NAME = "DeclId";
static const std::string PARAMETER_INFO_STRUCT_NAME = "ParameterInfo";
static const std::string ON_CALL_ENUM_NAME = "OnCall";
static const std::string DECL_KIND_ENUM_NAME = "DeclKind";

static const std::string HAS_DEFAULT_VALUE_FOR_STUB_INTERFACE_NAME = "HasDefaultValueForStub";
static const std::string DEFAULT_VALUE_FOR_STUB_METHOD_NAME = "defaultValueForStub";

static const std::string ON_CALL_METHOD_NAME = "onCall";

// OnCall enum entry names
static const std::string RETURN_ZERO_ENTRY = "ReturnZero";
static const std::string RETURN_ENTRY = "Return";
static const std::string THROW_ENTRY = "Throw";
static const std::string CALL_BASE_ENTRY = "CallBase";
static const std::string RETURN_DEFAULT_ENTRY = "ReturnDefault";

// Exceptions related to framework-compiler interaction
static const std::string ILLEGAL_MOCK_CALL_EXCEPTION_NAME = "IllegalMockCallException";
static const std::string NO_DEFAULT_VALUE_FOR_MOCK_EXCEPTION_NAME = "NoDefaultValueForMockException";
static const std::string MOCK_RETURN_VALUE_TYPE_MISMATCH_EXCEPTION_NAME = "MockReturnValueTypeMismatchException";

static const std::string MOCK_ZERO_VALUE_NAME = "MockZeroValue";

/*
 * Below are internal names not related to intermediate mocking framework API
*/

static const std::string MOCKED_DECL_SUFFIX = "$Mocked";

static const std::string FIELD_NAME_TO_STORE_HANDLER = "handler";
static const std::string FIELD_NAME_TO_STORE_OBJ_TO_SPY = "objectToSpyOn";

static const std::string PARAMETER_TO_MATCH_RETURN_ENTRY = "value";
static const std::string PARAMETER_TO_MATCH_THROW_ENTRY = "e";

// This map describes the correspondance between accessor kind in the compiler code
// and accessor kind in the unittest library code
static const std::map<AccessorKind, std::string> ACCESSOR_KINDS = {
    { AccessorKind::METHOD, "Method" },
    // Technically, accessors for properies themselves are also generated,
    // it isn't used in the mocking framework intermediate API, so it's just delegated to the propery getter
    { AccessorKind::PROP, "PropertyGetter" },
    { AccessorKind::PROP_GETTER, "PropertyGetter" },
    { AccessorKind::PROP_SETTER, "PropertySetter" },
    { AccessorKind::FIELD_GETTER, "FieldGetter" },
    { AccessorKind::FIELD_SETTER, "FieldSetter" },
    { AccessorKind::TOP_LEVEL_FUNCTION, "TopLevelFunction" },
    { AccessorKind::STATIC_METHOD, "StaticMethod" },
    { AccessorKind::STATIC_PROP_GETTER, "StaticPropertyGetter" },
    { AccessorKind::STATIC_PROP_SETTER, "StaticPropertySetter" },
    { AccessorKind::STATIC_FIELD_GETTER, "StaticFieldGetter" },
    { AccessorKind::STATIC_FIELD_SETTER, "StaticFieldSetter" },
    { AccessorKind::TOP_LEVEL_VARIABLE_GETTER, "TopLevelVariableGetter" },
    { AccessorKind::TOP_LEVEL_VARIABLE_SETTER, "TopLevelVariableSetter" }
};

template <typename T> T* GetDeclFromMockPackage(const ImportManager& importManager, const std::string& identifier)
{
    Ptr<Decl> decl = importManager.GetImportedDecl(UNITTEST_MOCK_INTERNAL_PACKAGE_NAME, identifier);
    return DynamicCast<T*>(decl);
}

} // namespace

// Note that this function doesn't care of private and protected members inside the current of inherited class/interface
// We don't support officially mocking private members or protected ones outside the declaring package
bool IsDeclAccessible(const Package& currentPackage, const Decl& decl)
{
    if (decl.TestAttr(Attribute::PUBLIC)) {
        return true;
    }

    if (decl.TestAttr(Attribute::PRIVATE)) {
        return false;
    }

    return decl.fullPackageName == currentPackage.fullPackageName;
}

MockManager::MockManager(ImportManager& importManager, TypeManager& typeManager, const Ptr<MockUtils> mockUtils)
    : typeManager(typeManager),
      importManager(importManager),
      mockUtils(mockUtils),

      // Core used declarations
      objectDecl(importManager.GetCoreDecl<ClassDecl>(OBJECT_NAME)),

      // Mocking specific used declarations
      callHandlerDecl(GetDeclFromMockPackage<InterfaceDecl>(importManager, CALL_HANDLER_INTERFACE_NAME)),
      mockedInterfaceDecl(GetDeclFromMockPackage<InterfaceDecl>(importManager, MOCKED_INTERFACE_NAME)),
      parameterInfoDecl(GetDeclFromMockPackage<StructDecl>(importManager, PARAMETER_INFO_STRUCT_NAME)),
      declIdDecl(GetDeclFromMockPackage<StructDecl>(importManager, DECL_ID_STRUCT_NAME)),
      funcInfoDecl(GetDeclFromMockPackage<StructDecl>(importManager, FUNC_INFO_STRUCT_NAME)),
      callDecl(GetDeclFromMockPackage<StructDecl>(importManager, CALL_STRUCT_NAME)),
      onCallEnumDecl(GetDeclFromMockPackage<EnumDecl>(importManager, ON_CALL_ENUM_NAME)),
      declKindEnumDecl(GetDeclFromMockPackage<EnumDecl>(importManager, DECL_KIND_ENUM_NAME)),
      hasDefaultValueForStubDecl(
        GetDeclFromMockPackage<InterfaceDecl>(importManager, HAS_DEFAULT_VALUE_FOR_STUB_INTERFACE_NAME)),
      noDefaultValueForMockException(
        GetDeclFromMockPackage<ClassDecl>(importManager, NO_DEFAULT_VALUE_FOR_MOCK_EXCEPTION_NAME)),
      mockReturnValueTypeMismatchException(
        GetDeclFromMockPackage<ClassDecl>(importManager, MOCK_RETURN_VALUE_TYPE_MISMATCH_EXCEPTION_NAME)),
      requireMockObject(
        GetDeclFromMockPackage<FuncDecl>(importManager, REQUIRE_MOCK_OBJ_NAME)),
      mockZeroValueDecl(GetDeclFromMockPackage<StructDecl>(importManager, MOCK_ZERO_VALUE_NAME))
{}

bool MockManager::IsMockClass(const Decl& decl)
{
    return decl.astKind == ASTKind::CLASS_DECL && decl.TestAttr(Attribute::GENERATED_TO_MOCK);
}

void MockManager::AddObjectSuperTypeIfNeeded(
    const ClassLikeDecl& originalDecl, ClassDecl& mockedDecl) const
{
    if (!originalDecl.ty->IsInterface()) {
        return;
    }
    auto objectRef = MakeOwned<RefType>();
    objectRef->ref.identifier = OBJECT_NAME;
    objectRef->ref.target = objectDecl;
    objectRef->ty = objectDecl->ty;
    mockedDecl.inheritedTypes.insert(mockedDecl.inheritedTypes.begin(), std::move(objectRef));
}

MockManager::GeneratedClassResult MockManager::GenerateMockClassIfNeededAndGet(
    ClassLikeDecl& originalDecl, Package& curPkg, MockKind mockKind)
{
    CJC_ASSERT(originalDecl.astKind == ASTKind::CLASS_DECL ||
        originalDecl.astKind == ASTKind::INTERFACE_DECL);

    auto originalMangledName = mockUtils->Mangle(originalDecl);
    if (mockedClassDecls.find(originalMangledName) != mockedClassDecls.end()) {
        return {
            .classDecl=mockedClassDecls[originalMangledName],
            .generated=false,
        };
    }

    if (IS_GENERIC_INSTANTIATION_ENABLED &&
        !originalDecl.TestAttr(Attribute::GENERIC_INSTANTIATED) && originalDecl.generic
    ) {
        return {
            .classDecl=nullptr,
            .generated=false,
        };
    }

    // No matter in which file of the package mocked decl would be generated, so take the first file of the package
    auto curFile = Ptr(curPkg.files[0].get());
    auto mockedDecl = MakeOwned<ClassDecl>();

    if (!IS_GENERIC_INSTANTIATION_ENABLED) {
        mockUtils->AddGenericIfNeeded(originalDecl, *mockedDecl);
    }

    if (IS_GENERIC_INSTANTIATION_ENABLED || !mockedDecl->generic) {
        mockedDecl->ty = typeManager.GetClassTy(*mockedDecl, {});
    }

    mockedDecl->moduleName = Utils::GetRootPackageName(curPkg.fullPackageName);
    mockedDecl->curFile = curFile;
    mockedDecl->fullPackageName = curPkg.fullPackageName;
    mockedDecl->identifier = originalDecl.identifier + MOCKED_DECL_SUFFIX
        + std::to_string(ComputeInstantiationNumberToMangleMockedDecl(originalDecl));
    mockedDecl->EnableAttr(Attribute::GENERATED_TO_MOCK);
    mockedDecl->body = MakeOwned<ClassBody>();
    mockedDecl->linkage = Linkage::INTERNAL;
    mockedDecl->EnableAttr(GetAttrByAccessLevel(GetAccessLevel(originalDecl)));
    mockedDecl->EnableAttr(Attribute::OPEN);

    auto originalDeclRef = CreateRefType(originalDecl);
    Ptr<Ty> substitutedOriginalTy;
    if (!IS_GENERIC_INSTANTIATION_ENABLED) {
        substitutedOriginalTy = typeManager.GetInstantiatedTy(
            originalDeclRef->ty, GenerateTypeMapping(originalDecl, mockedDecl->ty->typeArgs));
        originalDeclRef->ty = substitutedOriginalTy;
    } else {
        substitutedOriginalTy = originalDecl.ty;
    }

    mockedDecl->inheritedTypes.emplace_back(std::move(originalDeclRef));
    originalDecl.subDecls.insert(mockedDecl.get());

    auto genericSubsts = MockUtils::BuildGenericSubsts(mockedDecl);
    auto handlerField = CreateFieldDecl(*mockedDecl, FIELD_NAME_TO_STORE_HANDLER, callHandlerDecl->ty, curPkg);

    AddMockedInterface(*mockedDecl, *handlerField);

    mockedDecl->body->decls.emplace_back(std::move(handlerField));
    mockedDecl->body->decls.emplace_back(
        CreateFieldDecl(*mockedDecl, FIELD_NAME_TO_STORE_OBJ_TO_SPY, substitutedOriginalTy, curPkg));
    mockedDecl->body->decls.emplace_back(CreateConstructorDecl(
        *mockedDecl, { FIELD_NAME_TO_STORE_HANDLER, FIELD_NAME_TO_STORE_OBJ_TO_SPY }, mockKind, *curFile));
    mockedDecl->body->decls.emplace_back(CreateConstructorDecl(
        *mockedDecl, { FIELD_NAME_TO_STORE_HANDLER }, mockKind, *curFile));

    AddObjectSuperTypeIfNeeded(originalDecl, *mockedDecl);
    AddMockedMembers(originalDecl, *mockedDecl, genericSubsts);
    AddCurFile(*mockedDecl, curFile);

    auto mockClassPtr = mockedDecl.get();

    mockedClassDecls[originalMangledName] = std::move(mockedDecl);

    return {
        .classDecl=mockClassPtr,
        .generated=true,
    };
}

void MockManager::AddMockedMembers(
    ClassLikeDecl& originalDecl, ClassDecl& mockedDecl, std::vector<TypeSubst>& classGenericSubsts)
{
    std::set<Ptr<Decl>> foundOverrides;

    // originalDecl itself is also included here
    for (auto& superDecl : originalDecl.GetAllSuperDecls()) {
        for (auto& member : superDecl->GetMemberDecls()) {
            if (foundOverrides.find(member.get()) != foundOverrides.end()) {
                continue;
            }
            FindOverridesInSuperDecl(*superDecl, *member, foundOverrides);
            AddMockedMemberIfNeeded(mockedDecl, *member, classGenericSubsts);
        }
    }
}

void MockManager::WrapWithRequireMockObject(Expr& receiverExpr)
{
    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.emplace_back(CreateFuncArg(ASTCloner::Clone(Ptr(&receiverExpr))));

    auto requireMockObjCall = MakeOwned<CallExpr>();
    requireMockObjCall->baseFunc = mockUtils->CreateDeclBasedReferenceExpr(
        *requireMockObject, { receiverExpr.ty }, REQUIRE_MOCK_OBJ_NAME, *receiverExpr.curFile);
    requireMockObjCall->ty = receiverExpr.ty;
    requireMockObjCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    requireMockObjCall->resolvedFunction = requireMockObject;
    requireMockObjCall->curFile = receiverExpr.curFile;
    requireMockObjCall->args = std::move(callArgs);
    if (IS_GENERIC_INSTANTIATION_ENABLED) {
        mockUtils->Instantiate(*requireMockObjCall);
    }
    receiverExpr.desugarExpr = std::move(requireMockObjCall);
}

void MockManager::AddMockedInterface(ClassDecl& mockedDecl, VarDecl& handlerFieldDecl)
{
    static const auto NOTHING_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_NOTHING);

    mockedDecl.inheritedTypes.emplace_back(CreateRefType(*mockedInterfaceDecl));
    mockedInterfaceDecl->subDecls.emplace(&mockedDecl);

    // Mocked interface contains exactly one func `getHandler`
    auto& interfaceMembers = mockedInterfaceDecl->GetMemberDecls();
    CJC_ASSERT(interfaceMembers.size() == 1);
    Ptr<FuncDecl> getHandlerMethodDecl = As<ASTKind::FUNC_DECL>(interfaceMembers[0]);
    CJC_NULLPTR_CHECK(getHandlerMethodDecl);
    CJC_ASSERT(getHandlerMethodDecl->identifier == GET_HANDLER_METHOD_NAME);

    auto getHandlerMethod = ASTCloner::Clone(getHandlerMethodDecl);
    auto getHandlerMethodBody = MakeOwned<FuncBody>();

    auto returnExpr = CreateReturnExpr(CreateRefExpr(handlerFieldDecl), getHandlerMethodBody);
    returnExpr->ty = NOTHING_TY;

    auto getHandlerMethodBlock = MakeOwned<Block>();
    getHandlerMethodBlock->ty = NOTHING_TY;
    getHandlerMethodBlock->body.emplace_back(std::move(returnExpr));

    getHandlerMethodBody->ty = getHandlerMethod->ty;
    getHandlerMethodBody->funcDecl = getHandlerMethod;
    getHandlerMethodBody->retType = CreateRefType(*callHandlerDecl);
    getHandlerMethodBody->paramLists.emplace_back(CreateFuncParamList(std::vector<OwnedPtr<FuncParam>>{}));
    getHandlerMethodBody->parentClassLike = &mockedDecl;
    getHandlerMethodBody->body = std::move(getHandlerMethodBlock);

    getHandlerMethod->outerDecl = &mockedDecl;
    getHandlerMethod->curFile = mockedDecl.curFile;
    getHandlerMethod->moduleName = mockedDecl.moduleName;
    getHandlerMethod->fullPackageName = mockedDecl.fullPackageName;
    getHandlerMethod->EnableAttr(Attribute::OVERRIDE);
    getHandlerMethod->DisableAttr(Attribute::ABSTRACT);
    getHandlerMethod->EnableAttr(Attribute::IN_CLASSLIKE);
    getHandlerMethod->linkage = Linkage::EXTERNAL;
    getHandlerMethod->funcBody = std::move(getHandlerMethodBody);

    mockedDecl.body->decls.emplace_back(std::move(getHandlerMethod));
}

void MockManager::FindOverridesInSuperDecl(
    ClassLikeDecl& superDecl, const Decl& member, std::set<Ptr<Decl>>& foundOverrides) const
{
    for (auto& anotherSuperDecl : superDecl.GetAllSuperDecls()) {
        if (anotherSuperDecl == &superDecl) {
            continue;
        }
        for (auto& superMember : anotherSuperDecl->GetMemberDecls()) {
            if (typeManager.PairIsOverrideOrImpl(member, *superMember)) {
                foundOverrides.insert(superMember);
                break; // In one super decl, only one member can be overriden
            }
        }
    }
}

void MockManager::WriteGeneratedClasses()
{
    for (auto& [mn, mockedClass] : mockedClassDecls) {
        mockUtils->Instantiate(*mockedClass);
        mockedClass->curFile->decls.emplace_back(std::move(mockedClass));
    }
    mockedClassDecls.clear();
}

bool MockManager::IsMockCall(const CallExpr& ce)
{
    return GetMockKind(ce) != MockKind::NOT_MOCK;
}

MockKind MockManager::GetMockKind(const CallExpr& ce)
{
    if (ce.callKind != CallKind::CALL_INTRINSIC_FUNCTION || !ce.resolvedFunction) {
        return MockKind::NOT_MOCK;
    }

    if (ce.resolvedFunction->fullPackageName != UNITTEST_MOCK_INTERNAL_PACKAGE_NAME) {
        return MockKind::NOT_MOCK;
    }

    if (ce.resolvedFunction->identifier == CREATE_MOCK_FUNC_NAME) {
        return MockKind::PLAIN_MOCK;
    } else if (ce.resolvedFunction->identifier == CREATE_SPY_FUNC_NAME) {
        return MockKind::SPY;
    } else {
        return MockKind::NOT_MOCK;
    }
}

Ptr<ClassDecl> MockManager::FindMockedClassOf(const ClassLikeDecl& decl)
{
    auto mockClassIterator = std::find_if(
        decl.subDecls.begin(), decl.subDecls.end(),
        [] (const Ptr<Decl> decl) { return IsMockClass(*decl); });
    if (mockClassIterator == decl.subDecls.end()) {
        return nullptr;
    }

    return As<ASTKind::CLASS_DECL>(*mockClassIterator);
}

int MockManager::ComputeInstantiationNumberToMangleMockedDecl(const ClassLikeDecl& declToMock)
{
    auto genericDecl = As<ASTKind::CLASS_LIKE_DECL>(declToMock.genericDecl);

    if (!genericDecl) {
        return 0;
    }

    if (instantiationCounters.count(genericDecl) > 0) {
        instantiationCounters[genericDecl]++;
    } else {
        instantiationCounters[genericDecl] = 1;
    }

    return instantiationCounters[genericDecl];
}

OwnedPtr<CallExpr> MockManager::CreateInitCallOfMockClass(
    ClassDecl& mockClass,
    std::vector<OwnedPtr<FuncArg>>& mockCallArgs,
    TypeManager& typeManager,
    const std::vector<Ptr<Ty>> instTys,
    const std::vector<Ptr<Ty>> valueParamTys)
{
    std::vector<OwnedPtr<Expr>> initArgs;
    std::transform(
        mockCallArgs.begin(), mockCallArgs.end(), std::back_inserter(initArgs),
        [] (auto & arg) { return ASTCloner::Clone(arg->expr.get()); }
    );

    return CreateInitCall(
        FindInitDecl(mockClass, typeManager, valueParamTys, instTys).value(),
        initArgs, *mockClass.curFile, instTys);
}

OwnedPtr<ThrowExpr> MockManager::CreateIllegalMockCallException(
    File& curFile, TypeManager& typeMgr, ImportManager& importMgr)
{
    CJC_ASSERT(GetDeclFromMockPackage<ClassDecl>(importMgr, ILLEGAL_MOCK_CALL_EXCEPTION_NAME));
    return CreateThrowException(
        *GetDeclFromMockPackage<ClassDecl>(importMgr, ILLEGAL_MOCK_CALL_EXCEPTION_NAME),
        {}, curFile, typeMgr);
}

OwnedPtr<MatchExpr> MockManager::CreateTypeCastForOnCallReturnValue(
    OwnedPtr<Expr> exprToCast, const Ptr<Ty> castTy) const
{
    auto castType = MockUtils::CreateType<Type>(castTy);
    auto varPattern = CreateVarPattern(V_COMPILER, castTy);
    auto varPatternRef = CreateRefExpr(*(varPattern->varDecl));

    std::vector<OwnedPtr<MatchCase>> matchCases;

    auto typePattern = CreateTypePattern(std::move(varPattern), std::move(castType), *exprToCast);
    typePattern->matchBeforeRuntime = false;

    matchCases.emplace_back(CreateMatchCase(std::move(typePattern), std::move(varPatternRef)));

    std::vector<OwnedPtr<Expr>> exceptionCallArgs;
    exceptionCallArgs.emplace_back(
        CreateLitConstExpr(LitConstKind::STRING, Ty::ToString(castTy), mockUtils->stringDecl->ty, true));

    matchCases.emplace_back(
        CreateMatchCase(MakeOwned<WildcardPattern>(),
        CreateThrowException(*mockReturnValueTypeMismatchException, std::move(exceptionCallArgs),
            *exprToCast->curFile, typeManager)));

    return CreateMatchExpr(std::move(exprToCast), std::move(matchCases), castTy);
}

OwnedPtr<AssignExpr> MockManager::CreateMemberAssignment(
    VarDecl& member, OwnedPtr<Expr> rhsExpr) const
{
    static const auto UNIT_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto decl = RawStaticCast<ClassDecl*>(member.outerDecl);
    auto thisExpr = CreateRefExpr(
        {"this", DEFAULT_POSITION, DEFAULT_POSITION, false},
        typeManager.GetClassThisTy(*decl, decl->ty->typeArgs));
    thisExpr->ref.target = decl;
    return CreateAssignExpr(CreateMemberAccess(std::move(thisExpr), member), std::move(rhsExpr), UNIT_TY);
}

OwnedPtr<MatchCase> MockManager::CreateOnCallReturnZeroMatchCase(
    const FuncDecl& originalMethod, const Ptr<Ty> zeroValueTy, Decl& enumConstructor) const
{
    auto pattern = MakeOwned<EnumPattern>();
    pattern->ty = enumConstructor.ty->IsFunc() ? StaticCast<FuncTy>(enumConstructor.ty)->retTy : enumConstructor.ty;
    pattern->constructor = mockUtils->CreateRefExprWithInstTys(
        enumConstructor, enumConstructor.ty->typeArgs, RETURN_ZERO_ENTRY, *(originalMethod.curFile));
    return CreateMatchCase(
        std::move(pattern), mockUtils->CreateZeroValue(zeroValueTy, *(originalMethod.curFile)));
}

OwnedPtr<MatchCase> MockManager::CreateOnCallReturnMatchCase(
    const FuncDecl& originalMethod, const Ptr<Ty> retTy, Decl& enumConstructor) const
{
    auto arg = MakeOwned<VarPattern>(SrcIdentifier{
        PARAMETER_TO_MATCH_RETURN_ENTRY, INVALID_POSITION, INVALID_POSITION, false});
    arg->ty = typeManager.GetAnyTy();
    arg->varDecl->ty = typeManager.GetAnyTy();

    auto argVarDecl = arg->varDecl.get();
    auto curFile = originalMethod.curFile;

    std::vector<OwnedPtr<Pattern>> patternExprArgs;
    patternExprArgs.emplace_back(std::move(arg));

    auto pattern = MakeOwned<EnumPattern>();
    pattern->ty = enumConstructor.ty->IsFunc() ? StaticCast<FuncTy>(enumConstructor.ty)->retTy : enumConstructor.ty;
    pattern->constructor = mockUtils->CreateRefExprWithInstTys(
        enumConstructor, enumConstructor.ty->typeArgs, RETURN_ENTRY, *curFile);
    pattern->patterns = std::move(patternExprArgs);

    auto refExpr = CreateRefExpr(*argVarDecl);
    refExpr->curFile = originalMethod.curFile;

    return CreateMatchCase(std::move(pattern), CreateTypeCastForOnCallReturnValue(std::move(refExpr), retTy));
}

OwnedPtr<MatchCase> MockManager::CreateOnCallThrowMatchCase(
    const FuncDecl& originalMethod, Decl& enumConstructor) const
{
    auto arg = MakeOwned<VarPattern>(
        SrcIdentifier{PARAMETER_TO_MATCH_THROW_ENTRY, INVALID_POSITION, INVALID_POSITION, false});
    arg->ty = mockUtils->exceptionClassDecl->ty;
    arg->varDecl->ty = mockUtils->exceptionClassDecl->ty;

    auto throwExpr = CreateThrowExpr(*(arg->varDecl.get()));
    throwExpr->ty = TypeManager::GetNothingTy();
    throwExpr->expr->ty = mockUtils->exceptionClassDecl->ty;

    std::vector<OwnedPtr<Pattern>> patternExprArgs;
    patternExprArgs.emplace_back(std::move(arg));

    auto pattern = MakeOwned<EnumPattern>();
    pattern->ty = enumConstructor.ty->IsFunc() ? StaticCast<FuncTy>(enumConstructor.ty)->retTy : enumConstructor.ty;
    pattern->patterns = std::move(patternExprArgs);
    pattern->constructor = mockUtils->CreateRefExprWithInstTys(
        enumConstructor, enumConstructor.ty->typeArgs, THROW_ENTRY, *(originalMethod.curFile));
    return CreateMatchCase(std::move(pattern), std::move(throwExpr));
}

OwnedPtr<MatchCase> MockManager::CreateOnCallCallBaseMatchCase(
    FuncDecl& originalFunc, const FuncDecl& mockedFunc, Decl& enumConstructor) const
{
    auto mockedClass = As<ASTKind::CLASS_DECL>(mockedFunc.outerDecl);
    CJC_NULLPTR_CHECK(mockedClass);
    auto objToSpyDecl = GetMemberDecl<VarDecl>(*mockedClass, FIELD_NAME_TO_STORE_OBJ_TO_SPY, {}, typeManager);
    CJC_ASSERT(objToSpyDecl);

    std::vector<OwnedPtr<FuncArg>> callBaseArgs;
    for (auto& param : mockedFunc.funcBody->paramLists[0]->params) {
        callBaseArgs.emplace_back(CreateFuncArg(CreateRefExpr(*param)));
    }

    auto memberAccessFuncBaseExpr = CreateMemberAccess(CreateRefExpr(*objToSpyDecl), originalFunc);
    memberAccessFuncBaseExpr->ty = mockedFunc.ty;
    memberAccessFuncBaseExpr->EnableAttr(Attribute::GENERATED_TO_MOCK);

    if (auto& genericInfo = mockedFunc.funcBody->generic; genericInfo) {
        for (auto& typeParam : genericInfo->typeParameters) {
            memberAccessFuncBaseExpr->instTys.emplace_back(typeParam->ty);
        }
    }

    auto retTy = mockedFunc.funcBody->retType->ty;
    auto callBaseFunc = CreateCallExpr(
        std::move(memberAccessFuncBaseExpr), std::move(callBaseArgs),
        &originalFunc, retTy, CallKind::CALL_DECLARED_FUNCTION);
    callBaseFunc->EnableAttr(Attribute::GENERATED_TO_MOCK);

    auto accessorKind = MockUtils::ComputeAccessorKind(originalFunc);
    if (accessorKind == AccessorKind::METHOD ||
        accessorKind == AccessorKind::PROP_GETTER ||
        accessorKind == AccessorKind::PROP_SETTER
    ) {
        callBaseFunc->EnableAttr(Attribute::MOCK_SUPPORTED);
    }

    auto pattern = MakeOwned<EnumPattern>();
    pattern->ty = enumConstructor.ty->IsFunc() ? StaticCast<FuncTy>(enumConstructor.ty)->retTy : enumConstructor.ty;
    pattern->constructor = mockUtils->CreateRefExprWithInstTys(
        enumConstructor, enumConstructor.ty->typeArgs, CALL_BASE_ENTRY, *(originalFunc.curFile));

    auto mangledName = mockUtils->Mangle(*originalFunc.outerDecl);
    auto spiedInstanceDecl = MockUtils::FindMockGlobalDecl(
        *mockedClass, MockUtils::spyObjVarName + "$" + mangledName);
    if (!spiedInstanceDecl || !originalFunc.funcBody->body) {
        return CreateMatchCase(std::move(pattern), std::move(callBaseFunc));
    }

    static const auto UNIT_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    auto thisRef = CreateRefExpr(
        SrcIdentifier{"this"}, typeManager.GetClassThisTy(*mockedClass, mockedClass->ty->typeArgs));
    thisRef->ref.target = mockedClass;

    auto enumMember = mockUtils->GetInstantiatedDecl(
        mockUtils->optionDecl, {typeManager.GetAnyTy()}, IS_GENERIC_INSTANTIATION_ENABLED);
    auto someOuterDecl = Sema::Desugar::AfterTypeCheck::LookupEnumMember(enumMember, OPTION_VALUE_CTOR);
    auto someOuterDeclRef = CreateRefExpr(*someOuterDecl);
    auto optionOuterDeclTy = typeManager.GetEnumTy(*mockUtils->optionDecl, {typeManager.GetAnyTy()});
    someOuterDeclRef->ty = typeManager.GetFunctionTy({ typeManager.GetAnyTy() }, optionOuterDeclTy);

    std::vector<OwnedPtr<FuncArg>> someOuterDeclCallArgs {};
    someOuterDeclCallArgs.emplace_back(CreateFuncArg(std::move(thisRef)));
    auto someOuterDeclCall = CreateCallExpr(std::move(someOuterDeclRef), std::move(someOuterDeclCallArgs));
    someOuterDeclCall->ty = optionOuterDeclTy;
    someOuterDeclCall->resolvedFunction = As<ASTKind::FUNC_DECL>(someOuterDecl);
    someOuterDeclCall->callKind = CallKind::CALL_DECLARED_FUNCTION;

    auto optionOuterDeclMember = mockUtils->GetInstantiatedDecl(
        optionOuterDeclTy->decl, {mockedClass->ty}, IS_GENERIC_INSTANTIATION_ENABLED);
    auto noneRef = CreateRefExpr(
        *Sema::Desugar::AfterTypeCheck::LookupEnumMember(optionOuterDeclMember, OPTION_NONE_CTOR));
    noneRef->ty = optionOuterDeclTy;

    auto callBaseMatch = CreateMatchCase(
        std::move(pattern),
        CreateAssignExpr(CreateRefExpr(*spiedInstanceDecl), std::move(someOuterDeclCall), UNIT_TY));

    auto callBaseResult = CreateVarDecl(
        "callBaseResult" + MockUtils::mockAccessorSuffix, std::move(callBaseFunc), nullptr);
    callBaseResult->curFile = mockedClass->curFile;
    callBaseResult->fullPackageName = mockedClass->fullPackageName;
    auto callBaseResultRef = CreateRefExpr(*callBaseResult);

    auto trueLit = CreateLitConstExpr(LitConstKind::BOOL, "true", BOOL_TY);
    trueLit->curFile = mockedClass->curFile;
    auto trueSpyCallMarkerAssign =
        CreateAssignExpr(
            CreateRefExpr(*MockUtils::FindMockGlobalDecl(*mockedClass, MockUtils::spyCallMarkerVarName)),
            std::move(trueLit), UNIT_TY);

    callBaseMatch->exprOrDecls->body.emplace_back(std::move(trueSpyCallMarkerAssign));
    callBaseMatch->exprOrDecls->body.emplace_back(std::move(callBaseResult));
    callBaseMatch->exprOrDecls->body.emplace_back(
        CreateAssignExpr(CreateRefExpr(*spiedInstanceDecl), std::move(noneRef), UNIT_TY));
    callBaseMatch->exprOrDecls->body.emplace_back(std::move(callBaseResultRef));
    callBaseMatch->exprOrDecls->ty = retTy;

    return std::move(callBaseMatch);
}

Ptr<FuncDecl> MockManager::FindDefaultValueForStubMethod(const Ptr<Ty> retTy) const
{
    for (auto& extend : typeManager.GetAllExtendsByTy(*retTy)) {
        auto extendedTy = extend->extendedType->ty;
        auto foundHasDefaultValueForStub = false;
        for (auto& iTy : extend->GetSuperInterfaceTys()) {
            if (auto iTyDecl = As<ASTKind::INTERFACE_DECL>(Ty::GetDeclOfTy(iTy));
                iTyDecl &&
                (iTyDecl == hasDefaultValueForStubDecl || iTyDecl->genericDecl == hasDefaultValueForStubDecl) &&
                iTy->typeArgs[0] == extendedTy
            ) {
                foundHasDefaultValueForStub = true;
                break;
            }
        }
        if (!foundHasDefaultValueForStub) {
            continue;
        }
        for (auto& extendDecl : extend->GetMemberDeclPtrs()) {
            auto extendFuncDecl = As<ASTKind::FUNC_DECL>(extendDecl);

            if (!extendFuncDecl || extendDecl->identifier != DEFAULT_VALUE_FOR_STUB_METHOD_NAME) {
                continue;
            }

            auto fb = extendFuncDecl->funcBody.get();
            if (fb->paramLists[0]->params.empty()) {
                return extendFuncDecl;
            }
        }
    }

    return nullptr;
}

OwnedPtr<MatchCase> MockManager::CreateOnCallReturnDefaultMatchCase(
    const FuncDecl& originalMethod, const Ptr<Ty> retTy, Decl& enumConstructor)
{
    OwnedPtr<Expr> rhsExpr;

    if (auto defaultValueProviderDecl = FindDefaultValueForStubMethod(retTy); defaultValueProviderDecl) {
        defaultForTypePresence[&originalMethod] = true;
        auto defaultValueProviderRef = MakeOwned<RefExpr>();
        defaultValueProviderRef->ref = Reference(Ty::ToString(retTy));
        defaultValueProviderRef->ref.target = Ty::GetDeclOfTy(retTy);
        defaultValueProviderRef->instTys = retTy->typeArgs;
        defaultValueProviderRef->ty = retTy;

        auto defaultValueProviderMemberAccess =
            CreateMemberAccess(std::move(defaultValueProviderRef), DEFAULT_VALUE_FOR_STUB_METHOD_NAME);
        defaultValueProviderMemberAccess->ty = typeManager.GetFunctionTy({}, retTy);
        defaultValueProviderMemberAccess->target = defaultValueProviderDecl;

        auto defaultValueProviderCall = MakeOwned<CallExpr>();
        defaultValueProviderCall->ty = retTy;
        defaultValueProviderCall->resolvedFunction = defaultValueProviderDecl;
        defaultValueProviderCall->baseFunc = std::move(defaultValueProviderMemberAccess);
        defaultValueProviderCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
        defaultValueProviderCall->curFile = originalMethod.curFile;
        rhsExpr = std::move(defaultValueProviderCall);
    } else {
        defaultForTypePresence[&originalMethod] = false;
        std::vector<OwnedPtr<Expr>> exceptionCallArgs;
        exceptionCallArgs.emplace_back(
            CreateLitConstExpr(LitConstKind::STRING, Ty::ToString(retTy), mockUtils->stringDecl->ty, true));
        exceptionCallArgs.emplace_back(
            CreateLitConstExpr(
                LitConstKind::STRING, HAS_DEFAULT_VALUE_FOR_STUB_INTERFACE_NAME, mockUtils->stringDecl->ty, true));
        rhsExpr = CreateThrowException(
            *noDefaultValueForMockException, std::move(exceptionCallArgs), *originalMethod.curFile, typeManager);
    }

    auto pattern = MakeOwned<EnumPattern>();
    pattern->ty = enumConstructor.ty->IsFunc() ? StaticCast<FuncTy>(enumConstructor.ty)->retTy : enumConstructor.ty;
    pattern->constructor = mockUtils->CreateRefExprWithInstTys(
        enumConstructor, enumConstructor.ty->typeArgs, RETURN_DEFAULT_ENTRY, *(originalMethod.curFile));

    return CreateMatchCase(std::move(pattern), std::move(rhsExpr));
}

OwnedPtr<MatchCase> MockManager::CreateOnCallResultMatchCase(
    FuncDecl& originalFunc, const Ptr<FuncDecl> mockedFunc,
    Decl& enumConstructor)
{
    auto id = enumConstructor.identifier;
    auto retTy = mockedFunc ? mockedFunc->funcBody->retType->ty : typeManager.GetAnyTy();

    if (id == RETURN_ZERO_ENTRY) {
        auto zeroValueTy = retTy->IsAny() ? mockZeroValueDecl->ty : retTy;
        return CreateOnCallReturnZeroMatchCase(originalFunc, zeroValueTy, enumConstructor);
    } else if (id == RETURN_ENTRY) {
        return CreateOnCallReturnMatchCase(originalFunc, retTy, enumConstructor);
    } else if (id == THROW_ENTRY) {
        return CreateOnCallThrowMatchCase(originalFunc, enumConstructor);
    } else if (id == CALL_BASE_ENTRY && mockedFunc) {
        return CreateOnCallCallBaseMatchCase(originalFunc, *mockedFunc, enumConstructor);
    } else if (id == RETURN_DEFAULT_ENTRY) {
        return CreateOnCallReturnDefaultMatchCase(originalFunc, retTy, enumConstructor);
    } else {
        return nullptr;
    }
}

OwnedPtr<FuncDecl> MockManager::CreateEmptyConstructorDecl(
    ClassDecl& mockedClass,
    std::vector<OwnedPtr<FuncParam>> params,
    File& curFile) const
{
    auto constructorDecl = MakeOwned<FuncDecl>();
    constructorDecl->identifier = "init";
    constructorDecl->curFile = &curFile;
    constructorDecl->moduleName = Utils::GetRootPackageName(curFile.curPackage->fullPackageName);
    constructorDecl->fullPackageName = curFile.curPackage->fullPackageName;
    constructorDecl->outerDecl = &mockedClass;
    constructorDecl->EnableAttr(Attribute::CONSTRUCTOR);
    std::vector<Ptr<Ty>> paramTys;
    std::for_each(params.begin(), params.end(),
        [&paramTys](const OwnedPtr<FuncParam>& param) { paramTys.emplace_back(param->ty); });
    constructorDecl->ty = typeManager.GetFunctionTy(paramTys, mockedClass.ty);
    constructorDecl->EnableAttr(Attribute::IN_CLASSLIKE);

    std::vector<OwnedPtr<FuncParamList>> constructorParamLists;
    constructorParamLists.emplace_back(CreateFuncParamList(std::move(params)));

    constructorDecl->funcBody = MakeOwned<FuncBody>();
    constructorDecl->funcBody->ty = constructorDecl->ty;
    constructorDecl->funcBody->parentClassLike = &mockedClass;
    constructorDecl->funcBody->paramLists = std::move(constructorParamLists);
    constructorDecl->funcBody->funcDecl = constructorDecl.get();

    return constructorDecl;
}

OwnedPtr<FuncDecl> MockManager::CreateConstructorDecl(
    ClassDecl& mockedClass, std::set<std::string> membersToAssign,
    MockKind mockKind, File& curFile) const
{
    std::vector<OwnedPtr<Node>> constructorBody;
    std::vector<OwnedPtr<FuncParam>> constructorParams;

    for (auto& member : mockedClass.GetMemberDecls()) {
        auto memberDecl = As<ASTKind::VAR_DECL>(member.get());
        if (!memberDecl) {
            continue;
        }

        OwnedPtr<Expr> rhsExpr;
        if (membersToAssign.find(member->identifier) != membersToAssign.end()) {
            auto constructorParam = CreateFuncParam(member->identifier, nullptr, nullptr, memberDecl->ty);
            rhsExpr = CreateRefExpr(*constructorParam);
            constructorParams.emplace_back(std::move(constructorParam));
        } else {
            rhsExpr = mockUtils->CreateZeroValue(memberDecl->ty, *(mockedClass.curFile));
        }
        constructorBody.emplace_back(CreateMemberAssignment(*memberDecl, std::move(rhsExpr)));
    }

    auto constructorDecl = CreateEmptyConstructorDecl(mockedClass, std::move(constructorParams), curFile);
    constructorDecl->funcBody->body = CreateBlock(std::move(constructorBody), mockedClass.ty);

    if (mockKind == MockKind::SPY) {
        AddAssignmentsForSuperFields(*constructorDecl);
    }

    // Add return at the end of creation.
    constructorDecl->funcBody->body->body.emplace_back(CreateReturnExpr(
        CreateUnitExpr(TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)), constructorDecl->funcBody.get()));

    return constructorDecl;
}

void MockManager::AddAssignmentsForSuperFields(const FuncDecl& constructorOfMockedDecl) const
{
    auto decl = As<ASTKind::CLASS_DECL>(constructorOfMockedDecl.outerDecl);
    CJC_ASSERT(decl);
    Ptr<VarDecl> objToSpyOnDecl = GetMemberDecl<VarDecl>(*decl, FIELD_NAME_TO_STORE_OBJ_TO_SPY, {}, typeManager);
    CJC_ASSERT(objToSpyOnDecl);

    for (auto& superDecl : decl->GetAllSuperDecls()) {
        if (superDecl == decl) {
            continue;
        }
        for (auto& member : superDecl->GetMemberDecls()) {
            auto fieldDecl = As<ASTKind::VAR_DECL>(member.get());
            if (!fieldDecl || Is<PropDecl>(*fieldDecl)) {
                continue;
            }

            auto objToSpyOnRef = MakeOwned<RefExpr>();
            objToSpyOnRef->ref = Reference(FIELD_NAME_TO_STORE_OBJ_TO_SPY);
            objToSpyOnRef->ref.target = objToSpyOnDecl;
            objToSpyOnRef->ty = objToSpyOnDecl->ty;

            auto rhsExpr = CreateMemberAccess(std::move(objToSpyOnRef), fieldDecl->identifier);

            constructorOfMockedDecl.funcBody->body->body.emplace_back(
                CreateMemberAssignment(*fieldDecl, std::move(rhsExpr)));
        }
    }
}

OwnedPtr<VarDecl> MockManager::CreateFieldDecl(
    ClassLikeDecl& decl, const std::string& identifier, const Ptr<Ty> ty, const Package& curPkg)
{
    auto fieldDecl = MakeOwned<VarDecl>();
    fieldDecl->identifier = identifier;
    fieldDecl->ty = ty;
    fieldDecl->isMemberParam = false;
    fieldDecl->outerDecl = &decl;
    fieldDecl->curFile = decl.curFile;
    fieldDecl->moduleName = Utils::GetRootPackageName(curPkg.fullPackageName);
    fieldDecl->fullPackageName = curPkg.fullPackageName;
    return fieldDecl;
}

OwnedPtr<CallExpr> MockManager::CreateDeclId(const Decl& decl, File& curFile) const
{
    std::vector<OwnedPtr<Expr>> valueArgs;
    auto mangledName = mockUtils->Mangle(*mockUtils->GetGenericDecl(Ptr(&decl)));
    valueArgs.emplace_back(
        CreateLitConstExpr(
            LitConstKind::STRING, mangledName, mockUtils->stringDecl->ty, true));
    valueArgs.emplace_back(
        CreateLitConstExpr(
            LitConstKind::STRING,
            mockUtils->GetOriginalIdentifierOfMockAccessor(decl),
            mockUtils->stringDecl->ty,
            true));
    return CreateInitCall(FindInitDecl(*declIdDecl, typeManager, valueArgs).value(), valueArgs, curFile);
}

OwnedPtr<CallExpr> MockManager::CreateParamInfo(const FuncParam& param, int position, File& curFile) const
{
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    static const auto INT_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);

    std::vector<OwnedPtr<Expr>> valueArgs;
    valueArgs.emplace_back(CreateLitConstExpr(LitConstKind::STRING, param.identifier, mockUtils->stringDecl->ty, true));
    valueArgs.emplace_back(CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(position), INT_TY, true));
    valueArgs.emplace_back(
        CreateLitConstExpr(LitConstKind::BOOL, param.isNamedParam ? "true" : "false", BOOL_TY, true));
    valueArgs.emplace_back(
        CreateLitConstExpr(LitConstKind::BOOL, param.assignment != nullptr ? "true" : "false", BOOL_TY, true));
    return CreateInitCall(
        FindInitDecl(*parameterInfoDecl, typeManager, valueArgs).value(), valueArgs, curFile);
}

OwnedPtr<ArrayLit> MockManager::CreateParamsInfo(const FuncDecl& decl, File& curFile) const
{
    std::vector<OwnedPtr<Expr>> paramInfoArgs {};
    int paramNumber = 1;
    for (auto& param : decl.funcBody->paramLists[0]->params) {
        paramInfoArgs.emplace_back(CreateParamInfo(*param, paramNumber, curFile));
        paramNumber++;
    }
    auto paramInfoArray = CreateArrayLit(
        std::move(paramInfoArgs), typeManager.GetStructTy(*mockUtils->arrayDecl, { parameterInfoDecl->ty }));
    AddArrayLitConstructor(*paramInfoArray);
    paramInfoArray->curFile = &curFile;
    return paramInfoArray;
}

OwnedPtr<ArrayLit> MockManager::CreateTypeParamsInfo(const FuncDecl& decl, File& curFile) const
{
    std::vector<OwnedPtr<Expr>> typeParamInfoArgs {};
    if (decl.funcBody->generic) {
        for (auto& typeParam : decl.funcBody->generic->typeParameters) {
            typeParamInfoArgs.emplace_back(
                CreateLitConstExpr(LitConstKind::STRING, typeParam->identifier, mockUtils->stringDecl->ty, true)
            );
        }
    }
    auto typeParamInfoArray = CreateArrayLit(
        std::move(typeParamInfoArgs), typeManager.GetStructTy(*mockUtils->arrayDecl, { mockUtils->stringDecl->ty }));
    AddArrayLitConstructor(*typeParamInfoArray);
    typeParamInfoArray->curFile = &curFile;
    return typeParamInfoArray;
}

OwnedPtr<CallExpr> MockManager::CreateFuncInfo(FuncDecl& funcDecl, File& curFile)
{
    static const auto INT_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    std::vector<OwnedPtr<Expr>> positionInfo;
    positionInfo.emplace_back(
        CreateLitConstExpr(LitConstKind::STRING, funcDecl.curFile->fileName, mockUtils->stringDecl->ty, true));
    positionInfo.emplace_back(
        CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(funcDecl.identifier.Begin().line), INT_TY, true));
    positionInfo.emplace_back(
        CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(funcDecl.identifier.Begin().column), INT_TY, true));

    std::vector<OwnedPtr<Expr>> funcIntoItems;
    funcIntoItems.emplace_back(CreateDeclId(funcDecl, curFile));
    funcIntoItems.emplace_back(CreateParamsInfo(funcDecl, curFile));
    funcIntoItems.emplace_back(CreateTypeParamsInfo(funcDecl, curFile));
    funcIntoItems.emplace_back(
        CreateTupleLit(std::move(positionInfo), typeManager.GetTupleTy({ mockUtils->stringDecl->ty, INT_TY, INT_TY })));
    funcIntoItems.emplace_back(
        CreateLitConstExpr(LitConstKind::BOOL, funcDecl.funcBody->body != nullptr ? "true" : "false", BOOL_TY, true));

    OwnedPtr<Expr> outerDeclExpr;
    auto optionDecl = mockUtils->optionDecl;
    auto optionOuterIdDeclTy = typeManager.GetEnumTy(*optionDecl, { declIdDecl->ty });
    if (funcDecl.outerDecl) {
        auto someOuterDeclIdDecl = Sema::Desugar::AfterTypeCheck::LookupEnumMember(optionDecl, OPTION_VALUE_CTOR);
        auto someInstanceRef = CreateRefExpr(*someOuterDeclIdDecl);
        someInstanceRef->ty = typeManager.GetFunctionTy({declIdDecl->ty}, optionOuterIdDeclTy);

        Ptr<Decl> outerDecl = funcDecl.outerDecl;
        if (funcDecl.TestAttr(Attribute::IN_EXTEND)) {
            outerDecl = mockUtils->GetExtendedClassDecl(funcDecl);
        }

        auto outerDeclId = CreateDeclId(*outerDecl, curFile);
        std::vector<OwnedPtr<FuncArg>> someOuterDeclIdCallArgs {};
        someOuterDeclIdCallArgs.emplace_back(CreateFuncArg(std::move(outerDeclId)));
        auto someOuterDeclIdCall = CreateCallExpr(std::move(someInstanceRef), std::move(someOuterDeclIdCallArgs));
        someOuterDeclIdCall->ty = optionOuterIdDeclTy;
        someOuterDeclIdCall->resolvedFunction = As<ASTKind::FUNC_DECL>(someOuterDeclIdDecl);
        someOuterDeclIdCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
        outerDeclExpr = std::move(someOuterDeclIdCall);
    } else {
        outerDeclExpr = CreateRefExpr(*Sema::Desugar::AfterTypeCheck::LookupEnumMember(optionDecl, OPTION_NONE_CTOR));
    }
    outerDeclExpr->ty = optionOuterIdDeclTy;

    funcIntoItems.emplace_back(std::move(outerDeclExpr));
    funcIntoItems.emplace_back(
        CreateLitConstExpr(LitConstKind::BOOL, defaultForTypePresence[&funcDecl] ? "true" : "false", BOOL_TY, true));
    funcIntoItems.emplace_back(
        CreateLitConstExpr(
            LitConstKind::STRING,
            Ty::ToString(RawStaticCast<FuncTy*>(funcDecl.ty)->retTy), mockUtils->stringDecl->ty, true));
    funcIntoItems.emplace_back(
        CreateLitConstExpr(
            LitConstKind::BOOL,
            IsDeclAccessible(*curFile.curPackage, funcDecl) ? "true" : "false", BOOL_TY, true));
    funcIntoItems.emplace_back(CreateDeclKind(funcDecl));
    return CreateInitCall(
        FindInitDecl(*funcInfoDecl, typeManager, funcIntoItems).value(), funcIntoItems, curFile);
}

OwnedPtr<CallExpr> MockManager::CreateDeclKind(const FuncDecl& decl) const
{
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    std::vector<OwnedPtr<FuncArg>> declKindArgs;

    declKindArgs.emplace_back(
        CreateFuncArg(
            CreateLitConstExpr(
                LitConstKind::STRING,
                mockUtils->GetOriginalIdentifierOfAccessor(decl), mockUtils->stringDecl->ty, true)));

    auto accessorKind = MockUtils::ComputeAccessorKind(decl);
    auto libAccessorKind = ACCESSOR_KINDS.at(accessorKind);
    auto needToRecordSetterPresence = false;
    auto hasSetter = false;

    if (accessorKind == AccessorKind::FIELD_GETTER) {
        needToRecordSetterPresence = true;
        hasSetter = MockUtils::IsGetterForMutField(decl);
    } else if (accessorKind == AccessorKind::PROP_GETTER) {
        needToRecordSetterPresence = true;
        hasSetter = decl.propDecl->isVar;
    }

    if (needToRecordSetterPresence) {
        declKindArgs.emplace_back(
            CreateFuncArg(CreateLitConstExpr(LitConstKind::BOOL, hasSetter ? "true" : "false", BOOL_TY, true)));
    }

    for (auto& enumEntry : declKindEnumDecl->constructors) {
        if (enumEntry->identifier != libAccessorKind) {
            continue;
        }

        auto entryRetType = RawStaticCast<const FuncTy*>(enumEntry->ty)->retTy;
        auto enumEntryRef = MakeOwned<RefExpr>();
        enumEntryRef->ref = Reference(libAccessorKind);
        enumEntryRef->ref.target = enumEntry;
        enumEntryRef->ty = entryRetType;

        auto enumEntryBase = CreateMemberAccess(std::move(enumEntryRef), libAccessorKind);
        auto enumEntryCallExpr = MakeOwned<CallExpr>();
        enumEntryCallExpr->ty = entryRetType;
        enumEntryCallExpr->resolvedFunction = RawStaticCast<FuncDecl*>(enumEntryBase->target);
        enumEntryCallExpr->baseFunc = std::move(enumEntryBase);
        enumEntryCallExpr->args = std::move(declKindArgs);
        enumEntryCallExpr->callKind = CallKind::CALL_DECLARED_FUNCTION;
        enumEntryCallExpr->curFile = decl.curFile;

        return enumEntryCallExpr;
    }

    return nullptr;
}

OwnedPtr<CallExpr> MockManager::CreateCallInfo(
    FuncDecl& originalFunction,
    OwnedPtr<Expr> mockedArgsArray,
    OwnedPtr<Expr> typeArgsArray,
    const Ptr<File> curFile,
    OwnedPtr<RefExpr> objRef)
{
    OwnedPtr<Expr> instanceExpr;
    auto optionObjectTy = typeManager.GetEnumTy(*mockUtils->optionDecl, { objectDecl->ty });
    if (objRef) {
        auto someInstanceDecl = Sema::Desugar::AfterTypeCheck::LookupEnumMember(
            mockUtils->optionDecl, OPTION_VALUE_CTOR);
        auto someInstanceRef = CreateRefExpr(*someInstanceDecl);
        someInstanceRef->ty = typeManager.GetFunctionTy({objectDecl->ty}, optionObjectTy);

        std::vector<OwnedPtr<FuncArg>> someInstancCallArgs {};
        someInstancCallArgs.emplace_back(CreateFuncArg(std::move(objRef)));
        auto instanceCall = CreateCallExpr(std::move(someInstanceRef), std::move(someInstancCallArgs));
        instanceCall->ty = optionObjectTy;
        instanceCall->resolvedFunction = As<ASTKind::FUNC_DECL>(someInstanceDecl);
        instanceCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
        instanceExpr = std::move(instanceCall);
    } else {
        instanceExpr = CreateRefExpr(
            *Sema::Desugar::AfterTypeCheck::LookupEnumMember(mockUtils->optionDecl, OPTION_NONE_CTOR));
    }
    instanceExpr->ty = optionObjectTy;

    std::vector<OwnedPtr<Expr>> callInfoItems;
    callInfoItems.emplace_back(std::move(instanceExpr));
    callInfoItems.emplace_back(std::move(typeArgsArray));
    callInfoItems.emplace_back(std::move(mockedArgsArray));
    callInfoItems.emplace_back(CreateFuncInfo(originalFunction, *curFile));

    return CreateInitCall(
        FindInitDecl(*callDecl, typeManager, callInfoItems).value(), callInfoItems, *curFile);
}

OwnedPtr<RefExpr> MockManager::GetHandlerRefFromClass(const Ptr<ClassDecl> decl)
{
    Ptr<VarDecl> callHandler = GetMemberDecl<VarDecl>(*decl, FIELD_NAME_TO_STORE_HANDLER, {}, typeManager);
    CJC_ASSERT(callHandler);

    auto callHandlerRef = MakeOwned<RefExpr>();
    callHandlerRef->ref = Reference(FIELD_NAME_TO_STORE_HANDLER);
    callHandlerRef->ref.target = callHandler;
    callHandlerRef->ty = callHandler->ty;
    callHandlerRef->curFile = decl->curFile;

    return callHandlerRef;
}

OwnedPtr<CallExpr> MockManager::CreateOnCallInvocation(
    OwnedPtr<Expr> mockedArgsArray, OwnedPtr<Expr> typeArgsArray,
    FuncDecl& originalFunc, OwnedPtr<RefExpr> objRef, OwnedPtr<Expr> handler
)
{
    auto curFile = handler->curFile;
    std::vector<OwnedPtr<FuncArg>> onCallArgs;
    onCallArgs.emplace_back(
        CreateFuncArg(
            CreateCallInfo(
                originalFunc, std::move(mockedArgsArray), std::move(typeArgsArray), curFile, std::move(objRef))));

    auto onCallBaseFunc = CreateMemberAccess(std::move(handler), ON_CALL_METHOD_NAME);
    auto onCallFunc = MakeOwned<CallExpr>();
    onCallFunc->ty = RawStaticCast<const FuncTy*>(onCallBaseFunc->ty)->retTy;
    onCallFunc->resolvedFunction = RawStaticCast<FuncDecl*>(onCallBaseFunc->target);
    onCallFunc->baseFunc = std::move(onCallBaseFunc);
    onCallFunc->args = std::move(onCallArgs);
    onCallFunc->callKind = CallKind::CALL_DECLARED_FUNCTION;
    onCallFunc->curFile = curFile;

    return onCallFunc;
}

OwnedPtr<FuncDecl> MockManager::CreateStaticMethodStub(
    FuncDecl& originalMethod, ClassDecl& mockedDecl, std::vector<TypeSubst>& classGenericSubsts)
{
    OwnedPtr<FuncDecl> mockedMethod = CreateMockedMethodWithoutBody(originalMethod, mockedDecl, classGenericSubsts);
    auto retTy = mockedMethod->funcBody->retType->ty;

    // Static methods in mocked classes should never be called, there is no way to do it from user's code
    std::vector<OwnedPtr<Node>> nodes;
    nodes.emplace_back(CreateIllegalMockCallException(*originalMethod.curFile, typeManager, importManager));

    mockedMethod->funcBody->body = CreateBlock(std::move(nodes));
    mockedMethod->funcBody->body->ty = retTy;

    return mockedMethod;
}

OwnedPtr<FuncDecl> MockManager::CreateMockedMethod(
    FuncDecl& originalMethod, ClassDecl& mockedDecl, std::vector<TypeSubst>& classGenericSubsts)
{
    // Generate mocked methods only for previously generated accessors or for an original method,
    // if it doesn't have accessor
    if (MockUtils::IsMockAccessorRequired(originalMethod)) {
        return nullptr;
    }
    OwnedPtr<FuncDecl> mockedMethod = CreateMockedMethodWithoutBody(originalMethod, mockedDecl, classGenericSubsts);
    auto retTy = mockedMethod->funcBody->retType->ty;

    auto thisRef = CreateRefExpr(
        SrcIdentifier{"this"}, typeManager.GetClassThisTy(mockedDecl, mockedDecl.ty->typeArgs));
    thisRef->ref.target = &mockedDecl;

    std::vector<OwnedPtr<MatchCase>> matchCasesForOnCallReturnedValue;
    for (auto& constructor : onCallEnumDecl->constructors) {
        if (auto matchCase = CreateOnCallResultMatchCase(originalMethod, mockedMethod, *constructor); matchCase) {
            matchCasesForOnCallReturnedValue.emplace_back(std::move(matchCase));
        }
    }

    std::vector<OwnedPtr<Node>> mockedMethodBodyNodes;
    mockedMethodBodyNodes.emplace_back(
        CreateReturnExpr(
            CreateMatchExpr(
                CreateOnCallInvocation(
                    mockUtils->WrapCallArgsIntoArray(*mockedMethod),
                    mockUtils->WrapCallTypeArgsIntoArray(*mockedMethod),
                    originalMethod, std::move(thisRef), GetHandlerRefFromClass(&mockedDecl)),
                std::move(matchCasesForOnCallReturnedValue), retTy),
            mockedMethod->funcBody.get()));

    mockedMethod->funcBody->body = CreateBlock(std::move(mockedMethodBodyNodes));
    mockedMethod->funcBody->body->ty = retTy;
    mockedMethod->linkage = Linkage::INTERNAL;

    return mockedMethod;
}

OwnedPtr<FuncDecl> MockManager::CreateMockedMethodWithoutBody(
    FuncDecl& originalMethod, ClassDecl& mockedDecl, std::vector<TypeSubst>& classGenericSubsts)
{
    OwnedPtr<FuncDecl> mockedMethod = ASTCloner::Clone(Ptr(&originalMethod));

    if (!IS_GENERIC_INSTANTIATION_ENABLED) {
        mockUtils->AddGenericIfNeeded(originalMethod, *mockedMethod);
        MockUtils::PrependFuncGenericSubst(
            originalMethod.funcBody->generic, mockedMethod->funcBody->generic, classGenericSubsts);
    }

    auto funcTy = mockUtils->GetInstantiatedTy(originalMethod.ty, classGenericSubsts);
    auto retTy = mockUtils->GetInstantiatedTy(originalMethod.funcBody->retType->ty, classGenericSubsts);
    auto mockedMethodBody = MakeOwned<FuncBody>();
    mockedMethodBody->generic = std::move(mockedMethod->funcBody->generic);
    mockedMethodBody->parentClassLike = &mockedDecl;
    mockedMethodBody->funcDecl = mockedMethod.get();
    mockedMethodBody->ty = funcTy;
    mockedMethodBody->retType = std::move(mockedMethod->funcBody->retType);
    mockedMethodBody->retType->ty = retTy;

    std::vector<OwnedPtr<FuncParam>> mockedMethodParams {};
    for (auto& param : originalMethod.funcBody->paramLists[0]->params) {
        auto funcParam = CreateFuncParam(
            param->identifier, nullptr, nullptr, mockUtils->GetInstantiatedTy(param->ty, classGenericSubsts));
        funcParam->outerDecl = mockedMethod.get();
        funcParam->curFile = mockedDecl.curFile;
        mockedMethodParams.emplace_back(std::move(funcParam));
    }

    std::vector<OwnedPtr<FuncParamList>> mockedMethodParamLists {};
    mockedMethodParamLists.emplace_back(CreateFuncParamList(std::move(mockedMethodParams)));
    mockedMethodBody->paramLists = std::move(mockedMethodParamLists);

    mockedMethod->outerDecl = &mockedDecl;
    mockedMethod->ty = funcTy;
    mockedMethod->linkage = Linkage::EXTERNAL;
    mockedMethod->EnableAttr(Attribute::OVERRIDE);
    mockedMethod->DisableAttr(Attribute::ABSTRACT);
    mockedMethod->EnableAttr(Attribute::IN_CLASSLIKE);
    mockedMethod->EnableAttr(Attribute::OPEN);
    mockedMethod->curFile = mockedDecl.curFile;
    mockedMethod->funcBody = std::move(mockedMethodBody);
    mockedMethod->mangledName = mockUtils->Mangle(*mockedMethod);

    return mockedMethod;
}

OwnedPtr<PropDecl> MockManager::CreateMockedProp(
    PropDecl& originalProp, ClassDecl& mockedDecl, std::vector<TypeSubst>& classGenericSubsts)
{
    if (MockUtils::IsMockAccessorRequired(originalProp)) {
        return nullptr;
    }
    auto mockedProp = ASTCloner::Clone(Ptr(&originalProp));

    mockedProp->EnableAttr(Attribute::OVERRIDE);
    mockedProp->DisableAttr(Attribute::ABSTRACT);
    mockedProp->EnableAttr(Attribute::IN_CLASSLIKE);

    std::vector<OwnedPtr<FuncDecl>> mockedGetters;
    std::vector<OwnedPtr<FuncDecl>> mockedSetters;

    for (auto& getter : originalProp.getters) {
        auto mockedGetter = CreateMockedMethod(*getter, mockedDecl, classGenericSubsts);
        if (mockedGetter) {
            mockedGetters.emplace_back(std::move(mockedGetter));
        }
    }

    for (auto& setter : originalProp.setters) {
        auto mockedSetter = CreateMockedMethod(*setter, mockedDecl, classGenericSubsts);
        if (mockedSetter) {
            mockedSetters.emplace_back(std::move(mockedSetter));
        }
    }

    mockedProp->getters = std::move(mockedGetters);
    mockedProp->setters = std::move(mockedSetters);
    mockedProp->linkage = Linkage::INTERNAL;

    return mockedProp;
}

void MockManager::AddMockedMemberIfNeeded(ClassDecl& mockedDecl,
    Decl& member, std::vector<TypeSubst>& classGenericSubsts)
{
    if (member.TestAttr(Attribute::CONSTRUCTOR)) {
        return;
    }
    if (member.TestAttr(Attribute::STATIC)) {
        auto originalMethod = As<ASTKind::FUNC_DECL>(&member);
        if (!originalMethod || originalMethod->funcBody->body != nullptr) {
            return;
        }
        // For static methods without default implementation creating stub implementation
        if (auto mockedMethod = CreateStaticMethodStub(*originalMethod, mockedDecl, classGenericSubsts)) {
            mockedDecl.body->decls.emplace_back(std::move(mockedMethod));
        }
        return;
    }
    if (auto originalProp = As<ASTKind::PROP_DECL>(&member); originalProp) {
        if (auto mockedProp = CreateMockedProp(*originalProp, mockedDecl, classGenericSubsts); mockedProp) {
            mockedDecl.body->decls.emplace_back(std::move(mockedProp));
        }
    }
    if (auto originalMethod = As<ASTKind::FUNC_DECL>(&member); originalMethod) {
        if (IS_GENERIC_INSTANTIATION_ENABLED && (originalMethod->genericDecl != nullptr ||
            originalMethod->TestAttr(Attribute::GENERIC) || originalMethod->ty->HasGeneric())
        ) {
            return;
        }
        if (auto mockedMethod = CreateMockedMethod(*originalMethod, mockedDecl, classGenericSubsts); mockedMethod) {
            mockedDecl.body->decls.emplace_back(std::move(mockedMethod));
        }
    }
}

std::vector<OwnedPtr<MatchCase>> MockManager::GenerateCallHandlerCases(FuncDecl& staticDecl, const Expr& injectTo)
{
    std::vector<OwnedPtr<MatchCase>> matchCasesForOnCallReturnedValue;

    auto handlerRetTy = typeManager.GetAnyTy();
    auto optionFuncRetTy = typeManager.GetEnumTy(*mockUtils->optionDecl, { handlerRetTy });
    auto optionFuncRet = optionFuncRetTy->decl;

    for (auto& constructor : onCallEnumDecl->constructors) {
        auto matchCase = CreateOnCallResultMatchCase(staticDecl, nullptr, *constructor);
        if (matchCase) {
            auto someCaseDecl = Sema::Desugar::AfterTypeCheck::LookupEnumMember(optionFuncRet, OPTION_VALUE_CTOR);
            auto someCaseRef = CreateRefExpr(*someCaseDecl);
            someCaseRef->ty = typeManager.GetFunctionTy({handlerRetTy}, optionFuncRetTy);

            std::vector<OwnedPtr<FuncArg>> someCaseCallArgs {};
            auto firstExpr = As<ASTKind::EXPR>(matchCase->exprOrDecls->body[0]);
            CJC_NULLPTR_CHECK(firstExpr);
            someCaseCallArgs.emplace_back(CreateFuncArg(ASTCloner::Clone(Ptr(firstExpr))));
            auto someCaseCall = CreateCallExpr(std::move(someCaseRef), std::move(someCaseCallArgs));
            someCaseCall->resolvedFunction = As<ASTKind::FUNC_DECL>(someCaseDecl);
            someCaseCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
            someCaseCall->ty = optionFuncRetTy;

            firstExpr->desugarExpr = std::move(someCaseCall);
            matchCasesForOnCallReturnedValue.emplace_back(std::move(matchCase));
        } else {
            auto pattern = MakeOwned<EnumPattern>();
            pattern->ty = constructor->ty->IsFunc() ? StaticCast<FuncTy>(constructor->ty)->retTy : constructor->ty;
            pattern->constructor = mockUtils->CreateRefExprWithInstTys(
                *constructor, constructor->ty->typeArgs, RETURN_ZERO_ENTRY, *(injectTo.curFile));
            auto none = Sema::Desugar::AfterTypeCheck::LookupEnumMember(optionFuncRet, OPTION_NONE_CTOR);
            matchCase = CreateMatchCase(std::move(pattern),
                mockUtils->CreateRefExprWithInstTys(*none, {handlerRetTy}, OPTION_NONE_CTOR, *(injectTo.curFile)));
            matchCasesForOnCallReturnedValue.emplace_back(std::move(matchCase));
        }
    }

    return matchCasesForOnCallReturnedValue;
}

OwnedPtr<Expr> MockManager::GetMockedObjectHandler(OwnedPtr<RefExpr> objRef, const Ptr<File> curFile)
{
    static const auto NOTHING_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_NOTHING);

    auto handlerRetTy = typeManager.GetAnyTy();
    auto optionFuncRetTy = typeManager.GetEnumTy(*mockUtils->optionDecl, { handlerRetTy });

    auto noneRef = CreateRefExpr(
        *Sema::Desugar::AfterTypeCheck::LookupEnumMember(mockUtils->optionDecl, OPTION_NONE_CTOR));
    noneRef->ty = optionFuncRetTy;
    auto returnNoneExpr = CreateReturnExpr(std::move(noneRef));
    returnNoneExpr->ty = NOTHING_TY;

    auto mockedObjVarPattern = CreateVarPattern(V_COMPILER, mockedInterfaceDecl->ty);
    auto mockedObjRef = CreateRefExpr(*mockedObjVarPattern->varDecl);
    mockedObjRef->ty = mockedInterfaceDecl->ty;

    Ptr<FuncDecl> getHandlerMethodDecl = GetMemberDecl<FuncDecl>(
        *mockedInterfaceDecl, GET_HANDLER_METHOD_NAME, {}, typeManager);
    CJC_NULLPTR_CHECK(getHandlerMethodDecl);

    auto getHandlerMa = CreateMemberAccess(ASTCloner::Clone(mockedObjRef.get()), *getHandlerMethodDecl);
    auto getHandlerCall = MakeOwned<CallExpr>();
    getHandlerCall->ty = callHandlerDecl->ty;
    getHandlerCall->resolvedFunction = RawStaticCast<FuncDecl*>(getHandlerMa->target);
    CJC_NULLPTR_CHECK(getHandlerCall->resolvedFunction);
    getHandlerCall->baseFunc = std::move(getHandlerMa);
    getHandlerCall->args = std::vector<OwnedPtr<FuncArg>>{};
    getHandlerCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    getHandlerCall->curFile = curFile;

    auto mockedObjTypePattern = CreateTypePattern(
        std::move(mockedObjVarPattern), CreateRefType(*mockedInterfaceDecl), *objRef);
    mockedObjTypePattern->matchBeforeRuntime = false;
    mockedObjTypePattern->needRuntimeTypeCheck = true;

    std::vector<OwnedPtr<MatchCase>> mockedCastMatchCases;
    mockedCastMatchCases.emplace_back(CreateMatchCase(std::move(mockedObjTypePattern), std::move(getHandlerCall)));
    mockedCastMatchCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(returnNoneExpr)));

    auto mockedObjMatch = CreateMatchExpr(std::move(objRef), std::move(mockedCastMatchCases), callHandlerDecl->ty);
    mockedObjMatch->curFile = curFile;

    return mockedObjMatch;
}

OwnedPtr<Expr> MockManager::GetCurrentStaticHandler(const Ptr<File> curFile)
{
    auto currentStaticMa = CreateMemberAccess(CreateRefExpr(*callHandlerDecl), CURRENT_STATIC_FUNC_NAME);
    currentStaticMa->ty = typeManager.GetFunctionTy({}, callHandlerDecl->ty);
    auto target = currentStaticMa->target;
    auto currentStaticCall = CreateCallExpr(std::move(currentStaticMa), {});
    currentStaticCall->resolvedFunction = As<ASTKind::FUNC_DECL>(target);
    currentStaticCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    currentStaticCall->ty = callHandlerDecl->ty;
    currentStaticCall->curFile = curFile;

    return currentStaticCall;
}

OwnedPtr<LambdaExpr> MockManager::GenerateCallHandlerLambda(
    FuncDecl& decl, const Expr& injectTo, Ptr<FuncDecl> declForInfo)
{
    static const std::string TYPE_PARAMS_PARAM_NAME_FOR_HANDLER_LAMBDA = "typeParams";
    static const std::string PARAMS_PARAM_NAME_FOR_HANDLER_LAMBDA = "args";
    static const std::string OBJECT_PARAM_NAME_FOR_HANDLER_LAMBDA = "obj";
    static const auto NOTHING_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_NOTHING);

    auto isMethod = !decl.IsStaticOrGlobal();

    auto handlerRetTy = typeManager.GetAnyTy();
    auto optionFuncRetTy = typeManager.GetEnumTy(*mockUtils->optionDecl, { handlerRetTy });
    auto arrayTy = typeManager.GetStructTy(*mockUtils->arrayDecl, { typeManager.GetAnyTy() });
    auto toStrArrayTy = typeManager.GetStructTy(*mockUtils->arrayDecl, { mockUtils->toStringDecl->ty });
    auto objectTy = typeManager.GetClassTy(*mockUtils->objectDecl, {});
    auto funcTy = isMethod
        ? typeManager.GetFunctionTy({objectTy, arrayTy, toStrArrayTy}, optionFuncRetTy)
        : typeManager.GetFunctionTy({arrayTy, toStrArrayTy}, optionFuncRetTy);

    auto funcParamObj = CreateFuncParam(OBJECT_PARAM_NAME_FOR_HANDLER_LAMBDA, nullptr, nullptr, objectTy);
    auto funcParamObjRef = MakeOwned<RefExpr>();
    funcParamObjRef->ref = Reference(OBJECT_PARAM_NAME_FOR_HANDLER_LAMBDA);
    funcParamObjRef->ref.target = funcParamObj.get();
    funcParamObjRef->ty = funcParamObj->ty;
    funcParamObjRef->curFile = injectTo.curFile;

    auto funcParam = CreateFuncParam(PARAMS_PARAM_NAME_FOR_HANDLER_LAMBDA, nullptr, nullptr, arrayTy);
    auto funcParamRef = MakeOwned<RefExpr>();
    funcParamRef->ref = Reference(PARAMS_PARAM_NAME_FOR_HANDLER_LAMBDA);
    funcParamRef->ref.target = funcParam.get();
    funcParamRef->ty = funcParam->ty;
    funcParamRef->curFile = injectTo.curFile;

    auto funcTypeParam = CreateFuncParam(TYPE_PARAMS_PARAM_NAME_FOR_HANDLER_LAMBDA, nullptr, nullptr, toStrArrayTy);
    auto funcTypeParamRef = MakeOwned<RefExpr>();
    funcTypeParamRef->ref = Reference(TYPE_PARAMS_PARAM_NAME_FOR_HANDLER_LAMBDA);
    funcTypeParamRef->ref.target = funcTypeParam.get();
    funcTypeParamRef->ty = funcTypeParam->ty;
    funcTypeParamRef->curFile = injectTo.curFile;

    std::vector<OwnedPtr<FuncParam>> handlerCallParams {};
    if (isMethod) {
        handlerCallParams.emplace_back(std::move(funcParamObj));
    }
    handlerCallParams.emplace_back(std::move(funcParam));
    handlerCallParams.emplace_back(std::move(funcTypeParam));

    std::vector<OwnedPtr<FuncParamList>> handlerCallParamLists {};
    handlerCallParamLists.emplace_back(CreateFuncParamList(std::move(handlerCallParams)));

    OwnedPtr<Expr> handler = nullptr;

    if (isMethod) {
        handler = GetMockedObjectHandler(std::move(funcParamObjRef), injectTo.curFile);
    } else {
        handler = GetCurrentStaticHandler(injectTo.curFile);
    }

    auto onCallDecl = declForInfo ? declForInfo.get() : &decl;
    auto onCallInvocation = CreateOnCallInvocation(
        std::move(funcParamRef), std::move(funcTypeParamRef), *onCallDecl, nullptr, std::move(handler));
    auto matchExpr = CreateMatchExpr(
        std::move(onCallInvocation), GenerateCallHandlerCases(decl, injectTo), optionFuncRetTy);

    std::vector<OwnedPtr<Node>> handlerBody {};
    auto handlerBodyReturn = CreateReturnExpr(std::move(matchExpr), nullptr);
    handlerBodyReturn->ty = NOTHING_TY;
    handlerBody.emplace_back(std::move(handlerBodyReturn));

    return CreateLambdaExpr(
        CreateFuncBody(
            std::move(handlerCallParamLists), MockUtils::CreateType<Type>(optionFuncRetTy),
            CreateBlock(std::move(handlerBody), NOTHING_TY), funcTy)
    );
}

std::tuple<Ptr<InterfaceDecl>, Ptr<FuncDecl>> MockManager::FindDefaultAccessorInterfaceAndFunction(
    Ptr<FuncDecl> original, Ptr<MemberAccess> baseFunc)
{
    auto interfaceDecl = DynamicCast<InterfaceDecl>(original->outerDecl);
    if (!interfaceDecl) {
        return {nullptr, nullptr};
    }

    if (!original->TestAttr(Attribute::DEFAULT)) {
        return {nullptr, nullptr};
    }

    Ptr<InterfaceDecl> accessorInterfaceDecl = MockUtils::FindGlobalDecl<InterfaceDecl>(
        interfaceDecl->curFile, interfaceDecl->identifier + MockUtils::defaultAccessorSuffix);
    if (!accessorInterfaceDecl) {
        return {nullptr, nullptr};
    }

    auto extendDecl = typeManager.GetExtendDeclByInterface(*baseFunc->baseExpr->ty, *accessorInterfaceDecl->ty);
    if (!extendDecl) {
        return {nullptr, nullptr};
    }

    Ptr<FuncDecl> accessorImplDecl = MockUtils::FindMemberDecl<FuncDecl>(
        *extendDecl.value(), mockUtils->Mangle(*original) + MockUtils::defaultAccessorSuffix);

    return {accessorInterfaceDecl, accessorImplDecl};
}

void MockManager::GenerateCallHandlerForMethodWithDefault(CallExpr& callExpr)
{
    auto maExpr = DynamicCast<MemberAccess>(callExpr.baseFunc.get());
    CJC_ASSERT(maExpr);

    auto funcDecl = callExpr.resolvedFunction;
    CJC_ASSERT(funcDecl);

    auto [accessorInterfaceDecl, accessorDecl] = FindDefaultAccessorInterfaceAndFunction(funcDecl, maExpr);

    if (!accessorInterfaceDecl) {
        if (funcDecl->TestAttr(Attribute::STATIC) ||
            (funcDecl->outerDecl && funcDecl->outerDecl->astKind == ASTKind::EXTEND_DECL)) {
            GenerateCallHandlerForStaticDecl(*funcDecl, callExpr);
        }
        return;
    }
    CJC_ASSERT(accessorDecl);

    auto accessorMa = CreateMemberAccess(ASTCloner::Clone(maExpr->baseExpr.get()), *accessorDecl);
    accessorMa->ty = typeManager.GetInstantiatedTy(
        accessorDecl->ty, GenerateTypeMapping(*accessorDecl, maExpr->instTys));
    accessorMa->instTys = maExpr->instTys;

    auto call = ASTCloner::Clone(Ptr(&callExpr));
    call->baseFunc = std::move(accessorMa);
    call->resolvedFunction = accessorDecl;

    OwnedPtr<Expr> selector;
    if (callExpr.resolvedFunction->TestAttr(Attribute::STATIC)) {
        GenerateCallHandlerForStaticDecl(*accessorDecl, *call);
    } else {
        GenerateCallHandlerForStaticDecl(*accessorDecl, *call, callExpr.resolvedFunction);
    }

    callExpr.desugarExpr = std::move(call);

    return;
}

void MockManager::GenerateCallHandlerForStaticDecl(FuncDecl& decl, Expr& injectTo, Ptr<FuncDecl> declForInfo)
{
    static const auto UNIT_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);

    if (!callHandlerDecl) { // no unittest dependency, then don't try to generate call handler
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
    auto optionFunc = mockUtils->GetInstantiatedDecl(
        optionFuncTy->decl, {optionFuncTy}, IS_GENERIC_INSTANTIATION_ENABLED);
    auto handlerSomeDecl = Sema::Desugar::AfterTypeCheck::LookupEnumMember(optionFunc, OPTION_VALUE_CTOR);
    auto handlerSomeRef = CreateRefExpr(*handlerSomeDecl);
    handlerSomeRef->ty = typeManager.GetFunctionTy({funcTy}, optionFuncTy);

    std::vector<OwnedPtr<FuncArg>> handlerSomeCallArgs {};
    handlerSomeCallArgs.emplace_back(CreateFuncArg(GenerateCallHandlerLambda(decl, injectTo, declForInfo)));
    auto handlerSomeCall = CreateCallExpr(std::move(handlerSomeRef), std::move(handlerSomeCallArgs));
    handlerSomeCall->resolvedFunction = As<ASTKind::FUNC_DECL>(handlerSomeDecl);
    handlerSomeCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    handlerSomeCall->ty = optionFuncTy;

    auto genericDecl = mockUtils->GetGenericDecl(Ptr(&decl));
    auto mangledName = mockUtils->Mangle(*genericDecl);
    Ptr<VarDecl> handlerDecl = As<ASTKind::VAR_DECL>(
        MockUtils::FindMockGlobalDecl(*genericDecl, mangledName));
    CJC_NULLPTR_CHECK(handlerDecl);
    auto handlerRef = CreateRefExpr(*handlerDecl);
    handlerRef->curFile = injectTo.curFile;
    auto handlerAssignExpr = CreateAssignExpr(std::move(handlerRef), std::move(handlerSomeCall), UNIT_TY);
    handlerAssignExpr->curFile = injectTo.curFile;
    handlerAssignExpr->EnableAttr(Attribute::IMPLICIT_ADD);

    std::vector<OwnedPtr<Node>> replacedExprs {};
    replacedExprs.emplace_back(std::move(handlerAssignExpr));
    replacedExprs.emplace_back(ASTCloner::Clone(Ptr(&injectTo)));

    injectTo.desugarExpr = CreateBlock(std::move(replacedExprs), injectTo.ty);
    mockUtils->Instantiate(*injectTo.desugarExpr);
}

void MockManager::HandleMockAnnotatedLambdaValue(Expr& expr)
{
    if (auto callExpr = As<ASTKind::CALL_EXPR>(&expr); callExpr) {
        HandleMockAnnotatedLambdaWithCall(*callExpr);
    } else if (auto ma = As<ASTKind::MEMBER_ACCESS>(&expr); ma) {
        HandleMockAnnotatedLambdaWithMemberAccess(*ma, *ma);
    } else if (auto refExpr = As<ASTKind::REF_EXPR>(&expr); refExpr) {
        HandleMockAnnotatedLambdaWithRefExpr(*refExpr, *refExpr);
    } else if (auto assignExpr = As<ASTKind::ASSIGN_EXPR>(&expr)) {
        HandleMockAnnotatedLambdaWithAssignExpr(*assignExpr);
    }
}

void MockManager::HandleMockAnnotatedLambdaWithCall(CallExpr& callExpr)
{
    auto target = callExpr.resolvedFunction;

    bool isInInterfaceWithDefault =
        target->outerDecl && target->outerDecl->astKind == ASTKind::INTERFACE_DECL &&
        target->TestAttr(Attribute::DEFAULT);

    CJC_ASSERT(isInInterfaceWithDefault || target->TestAttr(Attribute::IN_EXTEND) || target->IsStaticOrGlobal());
    Ptr<FuncDecl> funcDecl = mockUtils->GetInstantiatedDecl(
        target, StaticCast<NameReferenceExpr*>(callExpr.baseFunc.get())->instTys, IS_GENERIC_INSTANTIATION_ENABLED);

    if (isInInterfaceWithDefault) {
        GenerateCallHandlerForMethodWithDefault(callExpr);
        return;
    }
    GenerateCallHandlerForStaticDecl(*funcDecl, callExpr);
}

void MockManager::HandleMockAnnotatedLambdaWithMemberAccess(MemberAccess& ma, Expr& injectTo)
{
    Ptr<FuncDecl> getterDecl;

    auto target = StaticCast<VarDeclAbstract>(ma.target);
    CJC_ASSERT(target->IsStaticOrGlobal());

    if (auto propDecl = As<ASTKind::PROP_DECL>(target); propDecl) {
        getterDecl = GetUsableGetterForProperty(*propDecl);
    } else {
        getterDecl = mockUtils->FindAccessor(&ma, target, AccessorKind::FIELD_GETTER);
    }

    GenerateCallHandlerForStaticDecl(*getterDecl, injectTo);

    if (target->isVar) {
        Ptr<FuncDecl> setter;
        if (auto propDecl = As<ASTKind::PROP_DECL>(ma.target); propDecl) {
            setter = GetUsableSetterForProperty(*As<ASTKind::PROP_DECL>(ma.target));
        } else {
            setter = mockUtils->FindAccessor(&ma, ma.target, AccessorKind::FIELD_SETTER);
        }
        GenerateCallHandlerForStaticDecl(*setter, injectTo);
    }
}

void MockManager::HandleMockAnnotatedLambdaWithRefExpr(const RefExpr& refExpr, Expr& injectTo)
{
    Ptr<FuncDecl> getterDecl;

    auto target = As<ASTKind::VAR_DECL>(refExpr.GetTarget());
    CJC_NULLPTR_CHECK(target);
    CJC_ASSERT(target->IsStaticOrGlobal());
    getterDecl = mockUtils->FindAccessor(nullptr, target, AccessorKind::TOP_LEVEL_VARIABLE_GETTER);
    CJC_NULLPTR_CHECK(getterDecl);

    GenerateCallHandlerForStaticDecl(*getterDecl, injectTo);

    if (target->isVar) {
        Ptr<FuncDecl> setter = mockUtils->FindAccessor(nullptr, target, AccessorKind::TOP_LEVEL_VARIABLE_SETTER);
        CJC_NULLPTR_CHECK(setter);
        GenerateCallHandlerForStaticDecl(*setter, injectTo);
    }
}

void MockManager::HandleMockAnnotatedLambdaWithAssignExpr(AssignExpr& assignExpr)
{
    if (auto refExpr = As<ASTKind::REF_EXPR>(assignExpr.leftValue)) {
        HandleMockAnnotatedLambdaWithRefExpr(*refExpr, assignExpr);
    } else if (auto maExpr = As<ASTKind::MEMBER_ACCESS>(assignExpr.leftValue)) {
        HandleMockAnnotatedLambdaWithMemberAccess(*maExpr, assignExpr);
    }
}
}
