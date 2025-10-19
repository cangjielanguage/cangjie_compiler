// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Basic/SourceManager.h"

#include "cangjie/Macro/TestEntryConstructor.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"

using namespace Cangjie;
using namespace AST;

namespace {

const std::string TEST_REGISTER_ATTR = "TEST_REGISTER";

OwnedPtr<CallExpr> CreateMethodCall(
    const std::string& refName, Ptr<FuncDecl> decl, std::vector<OwnedPtr<FuncArg>>&& args)
{
    auto method = MakeOwnedNode<MemberAccess>();
    method->baseExpr = CreateRefExpr(refName);
    method->field = decl->identifier;
    method->target = decl;
    method->targets.emplace_back(decl);
    return CreateCallExpr(std::move(method), std::move(args), decl);
}

OwnedPtr<VarDecl> CreateSuiteVarDecl(
    const std::string& varName, const std::string& suiteName, const std::string& suiteKind)
{
    auto vd = MakeOwnedNode<VarDecl>();
    vd->identifier = varName;
    vd->fullPackageName = suiteName;
    vd->type = CreateRefType(suiteKind);
    auto fa = MakeOwnedNode<FuncArg>();
    auto litExpr = MakeOwnedNode<LitConstExpr>();
    litExpr->kind = LitConstKind::STRING;
    litExpr->stringValue = suiteName;
    fa->expr = std::move(litExpr);
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(std::move(fa));
    vd->initializer = CreateCallExpr(CreateRefExpr(suiteKind), std::move(args));
    return vd;
}

bool IsValidTestSuiteRegisterFunction(Ptr<Decl> decl)
{
    auto fun = DynamicCast<FuncDecl*>(decl);
    if (fun == nullptr) {
        return false;
    }
    const auto& paramList = fun->funcBody->paramLists;

    if (paramList.size() > 1) {
        return false;
    }

    if (paramList.size() == 1 && paramList[0]->params.size() > 0) {
        return false;
    }

    for (const auto& anno : fun->annotations) {
        for (const auto& attr : anno->attrs) {
            if (attr == TEST_REGISTER_ATTR) {
                return true;
            }
        }
    }

    return false;
}

std::vector<Ptr<FuncDecl>> GetTestSuiteRegisterFunctions(Node& pkg)
{
    std::vector<Ptr<FuncDecl>> results;
    std::function<VisitAction(Ptr<Node>)> preVisit = [&results](Ptr<Node> node) -> VisitAction {
        if (node->TestAttr(Attribute::IS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->astKind == ASTKind::EXTEND_DECL) {
            auto ed = StaticAs<ASTKind::EXTEND_DECL>(node);
            // TEMPORARY should change to 'unittest.TestPackage'
            if (auto rt = DynamicCast<RefType*>(ed->extendedType.get()); rt && rt->ref.identifier == "TestPackage") {
                for (const auto& member : ed->members) {
                    if (auto memberFunc = As<ASTKind::FUNC_DECL>(member);
                        memberFunc && IsValidTestSuiteRegisterFunction(member.get())
                    ) {
                        results.emplace_back(memberFunc);
                    }
                }
            }
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&pkg, preVisit);
    walker.Walk();
    return results;
}

Ptr<Node> GetDeclNode(const MacroExpandDecl& med, ASTKind dclKind)
{
    if (med.invocation.decl == nullptr) {
        return nullptr;
    }
    if (med.invocation.decl->astKind == dclKind) {
        return med.invocation.decl.get();
    }
    if (med.invocation.decl->astKind == ASTKind::MACRO_EXPAND_DECL) {
        Ptr<MacroExpandDecl> nextMed = StaticAs<ASTKind::MACRO_EXPAND_DECL>(med.invocation.decl.get());
        return GetDeclNode(*nextMed, dclKind);
    }
    return nullptr;
}

Ptr<MacroExpandDecl> GetMacroDecl(MacroExpandDecl& med, const std::string& target)
{
    if (med.identifier == target || med.invocation.decl == nullptr) {
        return &med;
    }
    if (med.invocation.decl->astKind == ASTKind::MACRO_EXPAND_DECL) {
        Ptr<MacroExpandDecl> nextMed = StaticAs<ASTKind::MACRO_EXPAND_DECL>(med.invocation.decl.get());
        return GetMacroDecl(*nextMed, target);
    }
    return &med;
}

void GetTopLevelFuncDcl(Node& pkg, std::vector<Ptr<AST::FuncDecl>>& results)
{
    std::function<VisitAction(Ptr<Node>)> preVisit = [&results](Ptr<Node> node) -> VisitAction {
        if (node->TestAttr(Attribute::IS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->astKind == ASTKind::MACRO_EXPAND_DECL) {
            Ptr<MacroExpandDecl> med = StaticAs<ASTKind::MACRO_EXPAND_DECL>(node);
            auto fd = GetDeclNode(*med, ASTKind::FUNC_DECL);
            if (fd != nullptr) {
                results.push_back(StaticAs<ASTKind::FUNC_DECL>(fd));
            }
            return VisitAction::SKIP_CHILDREN;
        } else if (node->astKind == ASTKind::FUNC_DECL) {
            Ptr<FuncDecl> fd = StaticAs<ASTKind::FUNC_DECL>(node);
            results.push_back(fd);
            return VisitAction::SKIP_CHILDREN;
        } else if (node->IsNominalDecl()) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&pkg, preVisit);
    walker.Walk();
}

bool FindTopLevelFunc(const std::vector<Ptr<AST::FuncDecl>>& funcs, Ptr<const FuncDecl> fd)
{
    if (funcs.empty()) {
        return false;
    }
    if (std::find(funcs.begin(), funcs.end(), fd) != funcs.end()) {
        return true;
    }
    return false;
}

bool FindFuncModifier(const FuncDecl& fd, const std::string& target)
{
    if (fd.modifiers.empty()) {
        return false;
    }
    for (auto& i : fd.modifiers) {
        if (std::string(TOKENS[static_cast<int>(i.modifier)]) == target) {
            return true;
        }
    }
    return false;
}

bool FindClassModifier(const ClassDecl& cd, const std::string& target)
{
    if (cd.modifiers.empty()) {
        return false;
    }
    for (auto& i : cd.modifiers) {
        if (std::string(TOKENS[static_cast<int>(i.modifier)]) == target) {
            return true;
        }
    }
    return false;
}

bool FindMacro(const MacroExpandDecl& med, const std::string& target)
{
    if (med.identifier == target) {
        return true;
    }
    if (med.invocation.decl != nullptr && med.invocation.decl->astKind == ASTKind::MACRO_EXPAND_DECL) {
        Ptr<MacroExpandDecl> nextMed = StaticAs<ASTKind::MACRO_EXPAND_DECL>(med.invocation.decl.get());
        return FindMacro(*nextMed, target);
    }
    return false;
}

bool IsRightClassConstructor(const ClassDecl& cd)
{
    bool hasConstructorWithInput = false;
    bool hasConstructorWithoutInput = false;
    const std::string primaryConstructor = cd.identifier;
    std::function<VisitAction(Ptr<Node>)> preVisit = [&hasConstructorWithInput, &hasConstructorWithoutInput,
                                                     &primaryConstructor](Ptr<Node> node) -> VisitAction {
        if (node->TestAttr(Attribute::IS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->astKind == ASTKind::FUNC_DECL) {
            Ptr<FuncDecl> fd = StaticAs<ASTKind::FUNC_DECL>(node);
            if (fd->TestAttr(Attribute::CONSTRUCTOR) && !fd->funcBody->paramLists.empty() &&
                fd->funcBody->paramLists[0]->params.empty()) {
                hasConstructorWithoutInput = true;
                return VisitAction::STOP_NOW;
            }
            if (fd->TestAttr(Attribute::CONSTRUCTOR) && !fd->funcBody->paramLists.empty() &&
                !fd->funcBody->paramLists[0]->params.empty()) {
                hasConstructorWithInput = true;
            }
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            Ptr<PrimaryCtorDecl> pcd = StaticAs<ASTKind::PRIMARY_CTOR_DECL>(node);
            if (pcd->identifier == primaryConstructor && !pcd->funcBody->paramLists.empty() &&
                pcd->funcBody->paramLists[0]->params.empty()) {
                hasConstructorWithoutInput = true;
                return VisitAction::STOP_NOW;
            }
            if (pcd->identifier == primaryConstructor && !pcd->funcBody->paramLists.empty() &&
                !pcd->funcBody->paramLists[0]->params.empty()) {
                hasConstructorWithInput = true;
            }
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(cd.body.get(), preVisit);
    walker.Walk();
    if (hasConstructorWithoutInput) {
        return true;
    }
    if (hasConstructorWithInput) {
        return false;
    }
    return true;
}

bool IsUnitFunction(const FuncDecl& fd)
{
    if (auto pt = DynamicCast<PrimitiveType*>(fd.funcBody->retType.get()); pt && pt->kind == AST::TypeKind::TYPE_UNIT) {
        return true;
    }
    if (fd.funcBody->retType == nullptr) {
        return true;
    }
    return false;
}

} // namespace

void TestEntryConstructor::CheckFunctionWithAtTest(
    MacroExpandDecl& med, const std::vector<Ptr<AST::FuncDecl>>& funcs, const std::string& macroName)
{
    if (!FindMacro(med, macroName) || med.invocation.decl == nullptr) {
        return;
    }
    auto node = GetDeclNode(med, ASTKind::FUNC_DECL);
    if (node == nullptr) {
        return;
    }
    Ptr<FuncDecl> fd = StaticAs<ASTKind::FUNC_DECL>(node);
    if (macroName == "Test" && FindMacro(med, macroName) && !FindTopLevelFunc(funcs, fd)) {
        diag.Diagnose(*GetMacroDecl(med, macroName), DiagKind::macro_using_error,
            "@" + macroName + " modified function is not top level");
    }
    if (macroName == "TestCase" && FindMacro(med, macroName) && FindTopLevelFunc(funcs, fd)) {
        diag.Diagnose(*GetMacroDecl(med, macroName), DiagKind::macro_using_error,
            "@" + macroName + " modified top level function");
    }
    if (FindMacro(med, macroName) && !IsUnitFunction(*fd)) {
        diag.Diagnose(*GetMacroDecl(med, macroName), DiagKind::macro_using_error,
            "@" + macroName + " modified function type is not ()->Unit");
    }
    if (FindMacro(med, macroName) && FindFuncModifier(*fd, "foreign")) {
        diag.Diagnose(
            *GetMacroDecl(med, macroName), DiagKind::macro_using_error, "@" + macroName + " modified foreign function");
    }
}

void TestEntryConstructor::CheckClassWithMacro(MacroExpandDecl& med)
{
    if ((!FindMacro(med, "Test") && !FindMacro(med, "TestCase")) || med.invocation.decl == nullptr) {
        return;
    }
    auto node = GetDeclNode(med, ASTKind::CLASS_DECL);
    if (node == nullptr) {
        return;
    }
    Ptr<ClassDecl> cd = StaticAs<ASTKind::CLASS_DECL>(node);
    if (FindMacro(med, "Test") && FindClassModifier(*cd, "foreign")) {
        diag.Diagnose(*GetMacroDecl(med, "Test"), DiagKind::macro_using_error, "@Test modified foreign class");
    }
    if (FindMacro(med, "TestCase") && FindClassModifier(*cd, "foreign")) {
        diag.Diagnose(*GetMacroDecl(med, "TestCase"), DiagKind::macro_using_error, "@TestCase modified foreign class");
    }
    if (FindMacro(med, "Test") && !IsRightClassConstructor(*cd)) {
        diag.Diagnose(*GetMacroDecl(med, "Test"), DiagKind::macro_using_error,
            "Class modified by @Test must contain a parameterless construction.");
    }
}

void TestEntryConstructor::CheckTestSuiteConstraints(Node& root, const std::vector<Ptr<AST::FuncDecl>>& funcs)
{
    std::function<VisitAction(Ptr<Node>)> preVisit = [&funcs, this](Ptr<Node> node) -> VisitAction {
        if (node->TestAttr(Attribute::IS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->astKind != ASTKind::MACRO_EXPAND_DECL) {
            return VisitAction::WALK_CHILDREN;
        }
        Ptr<AST::MacroExpandDecl> med = StaticAs<ASTKind::MACRO_EXPAND_DECL>(node);
        CheckFunctionWithAtTest(*med, funcs, "Test");
        CheckFunctionWithAtTest(*med, funcs, "TestCase");
        CheckClassWithMacro(*med);
        if (med->invocation.decl != nullptr && med->invocation.decl->astKind == ASTKind::CLASS_DECL) {
            CheckTestSuiteConstraints(*(med->invocation.decl.get()), funcs);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&root, preVisit);
    walker.Walk();
}

void TestEntryConstructor::CheckTestSuite(const std::vector<OwnedPtr<Package>>& packages)
{
    // get top level dcl
    std::vector<Ptr<AST::FuncDecl>> topFuncDcl;
    for (auto& pkg : packages) {
        GetTopLevelFuncDcl(*pkg, topFuncDcl);
    }
    // check constraints
    for (auto& pkg : packages) {
        CheckTestSuiteConstraints(*pkg, topFuncDcl);
    }
}

void TestEntryConstructor::ConstructTestImports(AST::Package& pkg, TestModule& module)
{
    unsigned long i = 0;
    for (auto& it : module.testPackages) {
        if (it->packageName != pkg.fullPackageName && i < module.testPackages.size() - 1) {
            auto import = MakeOwned<struct ImportSpec>();
            import->content.kind = ImportKind::IMPORT_ALL;
            import->content.prefixPaths.emplace_back(it->packageName);
            import->content.identifier = "*";
            pkg.files[0]->imports.emplace_back(std::move(import));
        }
        i++;
    }
}

void TestEntryConstructor::ConstructTestEntry(AST::Package& pkg, TestModule& module, bool mockEnabled)
{
    // Construct test_entry function.
    auto entryFunc = MakeOwnedNode<FuncDecl>();
    entryFunc->identifier = TEST_ENTRY_NAME;

    auto packageName = ImportManager::IsTestPackage(pkg.fullPackageName) ?
        ImportManager::GetMainPartPkgNameForTestPkg(pkg.fullPackageName) :
        pkg.fullPackageName;

    entryFunc->fullPackageName = packageName;
    entryFunc->EnableAttr(Attribute::GLOBAL);
    // Set curFile in file 0 which should conform rules below that send this func to file 0.
    entryFunc->curFile = pkg.files[0].get();
    // Construct test_entry function body.
    auto funcBody = MakeOwnedNode<FuncBody>();
    auto type = MakeOwnedNode<PrimitiveType>();
    type->kind = TypeKind::TYPE_INT64;
    funcBody->retType = std::move(type);
    funcBody->paramLists.emplace_back(MakeOwnedNode<FuncParamList>());
    funcBody->funcDecl = entryFunc.get();
    funcBody->body = MakeOwnedNode<Block>();
    entryFunc->funcBody = std::move(funcBody);
    // Construct test_entry function body decls.
    // 1. declare testsuite creation.
    auto pk = CreateSuiteVarDecl("testPkg", packageName, "TestPackage");
    entryFunc->funcBody->body->body.emplace_back(std::move(pk));
    // 2. enable mock optimization for benchmarks if needed
    if (mockEnabled) {
        entryFunc->funcBody->body->body.emplace_back(
            CreateCallExpr(CreateMemberAccess(CreateRefExpr("testPkg"), "enableOptimizedMockForBench"), {}));
    }
    // 3. call add for each testcase decl.
    for (auto& testPackage : module.testPackages) {
        for (auto& it : testPackage->testRegisterFunctions) {
            entryFunc->funcBody->body->body.emplace_back(CreateMethodCall("testPkg", it, {}));
        }
    }
    // 4. call testsuite execution, print result.
    auto fa = MakeOwnedNode<FuncArg>();
    fa->expr = CreateRefExpr("testPkg");
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(std::move(fa));
    // If a test case fails, a non-zero value is returned. To avoid conflict with other return codes, 1 is returned.
    // if(entryMain(cases) > 0){
    //    return 1
    // }
    auto entryMainCall = CreateCallExpr(CreateRefExpr("entryMain"), std::move(args));
    auto litExprZero = MakeOwnedNode<LitConstExpr>();
    litExprZero->kind = LitConstKind::INTEGER;
    litExprZero->stringValue = "0";
    auto binaryExpr = CreateBinaryExpr(std::move(entryMainCall), std::move(litExprZero), TokenKind::GT);
    auto thenExprBlock = MakeOwnedNode<Block>();
    auto retExprOne = MakeOwnedNode<ReturnExpr>();
    auto litExprOne = MakeOwnedNode<LitConstExpr>();
    litExprOne->kind = LitConstKind::INTEGER;
    litExprOne->stringValue = "1";
    retExprOne->expr = std::move(litExprOne);
    thenExprBlock->body.push_back(std::move(retExprOne));
    // In the current implementation, two methods are used to determine whether elseBlock exists: null and hasElse.
    // In the future, the two methods should be unified.
    auto ifExpr = CreateIfExpr(std::move(binaryExpr), std::move(thenExprBlock));
    ifExpr->hasElse = false;
    entryFunc->funcBody->body->body.emplace_back(std::move(ifExpr));
    // 5. Create result return, return 0.
    auto retExpr = MakeOwnedNode<ReturnExpr>();
    auto litExpr = MakeOwnedNode<LitConstExpr>();
    litExpr->kind = LitConstKind::INTEGER;
    litExpr->stringValue = "0";
    retExpr->expr = std::move(litExpr);
    entryFunc->funcBody->body->body.emplace_back(std::move(retExpr));
    // Set funcdecl, synthesis it.
    pkg.files[0]->decls.emplace_back(std::move(entryFunc));
}

void TestEntryConstructor::ConstructTestSuite(const std::string& moduleName,
    std::vector<OwnedPtr<Package>>& srcPkgs, const std::vector<Ptr<PackageDecl>> importedPkgs,
    bool compileTestsOnly, bool mockEnabled)
{
    auto currentPkg = srcPkgs.back().get();
    auto importedMainPartPkgForTestPkg =
        compileTestsOnly && ImportManager::IsTestPackage(currentPkg->fullPackageName) ?
        FindMainPartPkgForTestPkg(currentPkg, importedPkgs) : nullptr;
    auto testMod = TestModule(moduleName);
    for (auto& pkg : srcPkgs) {
        auto testPkg = MakeOwned<TestPackage>(pkg->fullPackageName);
        testPkg->testRegisterFunctions = GetTestSuiteRegisterFunctions(*pkg);
        testMod.testPackages.emplace_back(std::move(testPkg));
    }

    if (importedMainPartPkgForTestPkg) {
        auto testPkg = MakeOwned<TestPackage>(importedMainPartPkgForTestPkg->fullPackageName);
        testPkg->testRegisterFunctions = GetTestSuiteRegisterFunctions(*importedMainPartPkgForTestPkg);
        testMod.testPackages.emplace_back(std::move(testPkg));
    }
    CJC_ASSERT(!srcPkgs.empty());
    if (srcPkgs[0]->files.empty()) {
        return;
    }
    ConstructTestImports(*srcPkgs[0], testMod);
    ConstructTestEntry(*srcPkgs[0], testMod, mockEnabled);
}

Ptr<Package> TestEntryConstructor::FindMainPartPkgForTestPkg(
    const Ptr<Package> testPackage, std::vector<Ptr<PackageDecl>> importedPkgs)
{
    CJC_ASSERT(ImportManager::IsTestPackage(testPackage->fullPackageName));

    auto mainPartPkgName = ImportManager::GetMainPartPkgNameForTestPkg(testPackage->fullPackageName);
    for (auto& package : importedPkgs) {
        if (package->srcPackage && package->srcPackage->fullPackageName == mainPartPkgName) {
            return package->srcPackage;
        }
    }

    return nullptr;
}

bool TestEntryConstructor::IsTestRegistrationFunction(const Ptr<Decl> decl)
{
    if (!Is<FuncDecl*>(decl)) {
        return false;
    }

    for (const auto& anno : decl->annotations) {
        for (const auto& attr : anno->attrs) {
            if (attr == TEST_REGISTER_ATTR) {
                return true;
            }
        }
    }

    return false;
}
