// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/DeadCodeElimination.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Visitor/Visitor.h"

#include <future>
#include <queue>

using namespace Cangjie::CHIR;

DeadCodeElimination::DeadCodeElimination(CHIRBuilder& builder, DiagAdapter& diag,
    const std::string& packageName)
    : builder(builder), diag(diag), currentPackageName(packageName)
{
}

namespace {
void DumpForDebug(const Ptr<Expression> expr, const Ptr<Func> func, bool isDebug)
{
    if (!isDebug) {
        return;
    }
    std::string message;
    if (expr != nullptr) {
        message = "[DCE] The " + expr->GetExprKindName() + " expression" + ToPosInfo(expr->GetDebugLocation()) +
            " has been deleted\n";
    }
    if (func != nullptr) {
        message = "[DCE] The " + func->GetIdentifier() + " function of " + func->GetPackageName() + " package" +
            ToPosInfo(func->GetDebugLocation()) + " has been deleted\n";
    }
    std::cout << message;
}

bool AllUsersIsExprKind(const std::vector<Expression*>& users, const ExprKind& kind)
{
    return std::all_of(users.begin(), users.end(), [&kind](auto user) {
        auto res = user->GetExprKind() == kind;
        auto args = user->GetOperands();
        auto it = std::find_if(args.begin(), args.end(), [](auto item) { return item->GetType()->IsNothing(); });
        if (res && (it != args.end())) {
            return true;
        }
        return false;
    });
}

bool CheckUsersOfExpr(const Expression& expr)
{
    // If the expression is the arg of apply, we should use the pos of this expression, not pos of apply.
    // Example code:
    // var a = 2
    // A(1, 2,return A(), a + 2)
    if (expr.GetResult()) {
        auto users = expr.GetResult()->GetUsers();
        if (AllUsersIsExprKind(users, ExprKind::TUPLE)) {
            return true;
        }
        if (AllUsersIsExprKind(users, ExprKind::APPLY)) {
            return true;
        }
        if (AllUsersIsExprKind(users, ExprKind::RAW_ARRAY_LITERAL_INIT)) {
            return true;
        }
        /*  If expr has user of store, but the store has zero position, then DCE use the position of this expr.

            func foo() {
                return 1
                1       // have user of store
            }
        */
        if (expr.GetResult()->GetUsers().size() == 1 && // 1 denote that if epxr has one user of store.
            expr.GetResult()->GetUsers()[0]->GetExprKind() == ExprKind::STORE) {
            auto storeNode = expr.GetResult()->GetUsers()[0];
            auto [res, nodeRange] = ToRangeIfNotZero(storeNode->GetDebugLocation());
            if (!res) {
                return true;
            }
        }
        return false;
    }
    return true;
}

std::string GetFuncIdent(const Func& func)
{
    if (func.GetFuncKind() == FuncKind::GETTER) {
        return "get";
    } else if (func.GetFuncKind() == FuncKind::SETTER) {
        return "set";
    }
    return func.GetSrcCodeIdentifier();
}

bool ShouldSkipUselessFuncElimination(const Package& package, const Cangjie::GlobalOptions& opts)
{
    if (opts.enableCompileDebug || opts.enableCoverage || opts.enIncrementalCompilation ||
        package.GetName() == Cangjie::REFLECT_PACKAGE_NAME) {
        return true;
    }
    return false;
}
} // namespace

static inline const std::unordered_map<ExprKind, std::string> EXPR_KIND_TO_STR = {
    {ExprKind::NEG, "-"},
    {ExprKind::NOT, "!"},
    {ExprKind::BITNOT, "~"},
    {ExprKind::ADD, "+"},
    {ExprKind::SUB, "-"},
    {ExprKind::MUL, "*"},
    {ExprKind::DIV, "/"},
    {ExprKind::MOD, "%"},
    {ExprKind::EXP, "**"},
    {ExprKind::LSHIFT, "<<"},
    {ExprKind::RSHIFT, ">>"},
    {ExprKind::BITAND, "&"},
    {ExprKind::BITOR, "|"},
    {ExprKind::BITXOR, "^"},
    {ExprKind::LT, "<"},
    {ExprKind::GT, ">"},
    {ExprKind::LE, "<="},
    {ExprKind::GE, ">="},
    {ExprKind::EQUAL, "=="},
    {ExprKind::NOTEQUAL, "!="},
    {ExprKind::AND, "&&"},
    {ExprKind::OR, "||"},
};

std::string DeadCodeElimination::GetLiteralFromExprKind(const ExprKind& kind) const
{
    auto it = EXPR_KIND_TO_STR.find(kind);
    CJC_ASSERT(it != EXPR_KIND_TO_STR.end());
    return it->second;
}

void DeadCodeElimination::UselessFuncElimination(Package& package, const GlobalOptions& opts)
{
    if (ShouldSkipUselessFuncElimination(package, opts)) {
        return;
    }
    auto allFuncs = package.GetGlobalFuncs();
    std::vector<Func*> funcsToBeRemoved;
    do {
        funcsToBeRemoved.clear();
        auto it = allFuncs.begin();
        while (it != allFuncs.end()) {
            if ((*it)->TestAttr(Attribute::IMPORTED)) {
                ++it;
                continue;
            }
            if (CheckUselessFunc(**it, opts)) {
                funcsToBeRemoved.emplace_back(*it);
                DumpForDebug(nullptr, *it, opts.chirDebugOptimizer);
                it = allFuncs.erase(it);
            } else {
                ++it;
            }
        }
        for (auto func : funcsToBeRemoved) {
            func->DestroySelf();
        }
    } while (!funcsToBeRemoved.empty());
    package.SetGlobalFuncs(allFuncs);
}

void DeadCodeElimination::ReportUnusedCode(const Package& package, const GlobalOptions& opts)
{
    for (auto globalVar : package.GetGlobalVars()) {
        ReportUnusedGlobalVar(*globalVar);
    }

    for (auto func : package.GetGlobalFuncs()) {
        ReportUnusedFunc(*func, opts);
        bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
        if (isCommonFunctionWithoutBody) {
            continue; // Nothing to visit
        }
        ReportUnusedCodeInFunc(*func->GetBody(), opts);
    }
}

void DeadCodeElimination::TryReportUnusedOnExpr(Expression& expr, const GlobalOptions& opts, bool blockUsed)
{
    ReportUnusedLocalVariable(expr, opts.enableCompileDebug);
    if (blockUsed) {
        ReportUnusedExpression(expr);
    }
    if (expr.GetExprKind() == ExprKind::LAMBDA) {
        ReportUnusedCodeInFunc(*StaticCast<const Lambda>(expr).GetBody(), opts);
    }
}

void DeadCodeElimination::ReportUnusedCodeInFunc(const BlockGroup& body, const GlobalOptions& opts)
{
    for (auto block : body.GetBlocks()) {
        auto blockUsed = !CheckUselessBlock(*block);
        for (auto expr : block->GetExpressions()) {
            TryReportUnusedOnExpr(*expr, opts, blockUsed);
        }
    }
}

void DeadCodeElimination::ReportUnusedFunc(const Func& func, const GlobalOptions& opts)
{
    if (func.Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING) {
        return;
    }

    // generic func check will implement in future
    if (func.TestAttr(Attribute::GENERIC_INSTANTIATED) || func.TestAttr(Attribute::GENERIC)) {
        return;
    }

    if (func.GetParentCustomTypeDef() != nullptr) {
        if (func.GetParentCustomTypeDef()->TestAttr(Attribute::GENERIC_INSTANTIATED) ||
            func.GetParentCustomTypeDef()->TestAttr(Attribute::GENERIC)) {
            return;
        }
    }

    // report unreachable function when functiong has nothing parameter
    // case like: func foo(a : Nothing){0}
    for (auto param : func.GetParams()) {
        if (param->GetType()->IsNothing()) {
            auto expr = param->GetDebugExpr();
            if (!expr) {
                continue;
            }
            auto [res, nodeRange] = GetDebugPos(*expr);
            if (res && !IsCrossPackage(nodeRange.begin, currentPackageName, diag)) {
                diag.DiagnoseRefactor(DiagKindRefactor::chir_dce_unreachable_function, nodeRange);
            }
            continue;
        }
    }
    if (auto funcKind = func.GetFuncKind();
        funcKind == FuncKind::DEFAULT_PARAMETER_FUNC || funcKind == FuncKind::FINALIZER) {
        return;
    }
    // check unused function
    if (!func.TestAttr(Attribute::COMPILER_ADD) && CheckUselessFunc(func, opts)) {
        auto ident = GetFuncIdent(func);
        auto debugPos = GetDebugPos(func);
        if (ident == "main") {
            DiagUnusedCode(debugPos, DiagKindRefactor::chir_dce_unused_function_main);
        } else {
            DiagUnusedCode(debugPos, DiagKindRefactor::chir_dce_unused_function, ident);
        }
    }
}

static bool IsExternalDecl(const Value& v)
{
    using namespace Cangjie;
    if (v.Get<LinkTypeInfo>() == Linkage::EXTERNAL) {
        return true;
    }
    // const var/func never have external linkage
    if (!v.TestAttr(Attribute::PRIVATE) && v.TestAttr(Attribute::CONST)) {
        if (auto gv = DynamicCast<GlobalVar>(&v)) {
            return !gv->IsLocalConst();
        }
        return true;
    }
    return false;
}

void DeadCodeElimination::ReportUnusedGlobalVar(const GlobalVar& globalVar)
{
    if (IsExternalDecl(globalVar)) {
        return;
    }
    if (globalVar.Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING) {
        return;
    }
    auto gvUsers = globalVar.GetUsers();
    // 1、unused const may have 0 or 1 user.
    // 2、unused 'let' global variable may have 0 or 1 user.
    // 3、unused 'var' global variable has one user and this user must be store
    if (globalVar.TestAttr(Attribute::CONST) || globalVar.TestAttr(Attribute::READONLY)) {
        if (gvUsers.empty()) {
            DiagUnusedCode(
                GetDebugPos(globalVar), DiagKindRefactor::chir_dce_unused_variable, globalVar.GetSrcCodeIdentifier());
        }
    }
    if (gvUsers.size() == 1 && gvUsers[0]->GetExprKind() == ExprKind::STORE) {
        DiagUnusedCode(
            GetDebugPos(globalVar), DiagKindRefactor::chir_dce_unused_variable, globalVar.GetSrcCodeIdentifier());
        return;
    }
}

void DeadCodeElimination::DiagUnusedVariableForParam(const Debug& expr)
{
    auto operand = expr.GetOperand(0);
    auto users = operand->GetUsers();
    // "_" param should not check
    if (operand->GetSrcCodeIdentifier() == "_") {
        return;
    }
    // unused parameter just has 1 user: debug
    if (CheckOneUsers(users)) {
        DiagUnusedVariable(expr);
    }
}

void DeadCodeElimination::DiagUnusedLambdaVariable(const Debug& expr)
{
    auto closureExpr = StaticCast<LocalVar*>(expr.GetOperand(0));
    if (closureExpr->GetExpr()->GetExprKind() == ExprKind::TUPLE) {
        auto realVar = StaticCast<LocalVar*>(closureExpr->GetExpr()->GetOperand(1));
        if (realVar->GetExpr()->GetExprKind() == ExprKind::TYPECAST) {
            auto typecastVar = StaticCast<LocalVar*>(realVar->GetExpr()->GetOperand(0));
            auto users = typecastVar->GetUsers();
            if (users.back() == realVar->GetExpr()) {
                DiagUnusedVariable(expr);
            }
        } else {
            auto users = realVar->GetUsers();
            if (users.back() == closureExpr->GetExpr()) {
                DiagUnusedVariable(expr);
            }
        }
    }
}
void DeadCodeElimination::DiagUnusedVariableForLocalVar(const Debug& expr, bool isDebug)
{
    auto operand = expr.GetOperand(0);
    auto users = operand->GetUsers();
    // unused 'var' variable have 2 situtation
    // when it has init value, 'var' has 2 users: debug, store
    // when it does not has init value, 'var' has 1 users: debug
    if (!expr.GetResult()->TestAttr(Attribute::READONLY)) {
        if (CheckOneUsers(users) || CheckTwoUsers(users) ||
            (StaticCast<LocalVar*>(operand)->GetDebugExpr() == &expr && CheckAllUsersIsNotUse(*operand, users))) {
            DiagUnusedVariable(expr);
            return;
        }
    } else {
        if (isDebug && CheckTwoUsers(users)) {
            // when use debug option, 'let' variable have 2 users if they are unused：debug, store
            DiagUnusedVariable(expr);
        } else {
            if (StaticCast<LocalVar*>(operand)->GetDebugExpr() == &expr && CheckAllUsersIsNotUse(*operand, users)) {
                // when not use debug option, 'let' variable have 1 users if they are unused：debug, or two 1 debug and
                // 1 store if it is let variable and has an initializer
                DiagUnusedVariable(expr);
            }
        }
    }
}

void DeadCodeElimination::DiagUnusedVariable(const Debug& expr)
{
    auto nodeRange = GetDebugPos(expr);
    if (nodeRange.first && !IsCrossPackage(nodeRange.second.begin, currentPackageName, diag)) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::chir_dce_unused_variable, nodeRange.second, expr.GetSrcCodeIdentifier());
    }
}

void DeadCodeElimination::ReportUnusedLocalVariable(const Expression& expr, bool isDebug)
{
    auto kind = expr.GetExprKind();
    if (kind != ExprKind::DEBUGEXPR) {
        return;
    }
    auto& debugExpr = *StaticCast<Debug*>(&expr);
    auto operand = debugExpr.GetOperand(0);
    if (operand->Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING ||
        debugExpr.Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING) {
        return;
    }
    // Can not tell if a Varray variable is unused by the number of users.
    // for example code:
    // let arr9: VArray<Int64, $6> = [0, 1, 2, 3, 4, 5]
    // let size: Int64 = arr9.size
    // The corresponding chir is:
    // %1: VArray<Int64, $6> = VArray(0, 1, 2, 3, 4, 5)
    // [readOnly] %2: Unit = Debug(%1, arr9)
    // %3: Int64 = Constant(6)
    // [readOnly] %4: Unit = Debug(%3, size)
    if (operand->GetType()->IsVArray()) {
        return;
    }
    auto users = operand->GetUsers();
    if (operand->IsParameter()) {
        DiagUnusedVariableForParam(debugExpr);
    } else if (operand->IsLocalVar()) {
        DiagUnusedVariableForLocalVar(debugExpr, isDebug);
    }
}

bool DeadCodeElimination::CheckOneUsers(const std::vector<Expression*>& users) const
{
    // 1 denote that has one user
    return users.size() == 1 && users[0]->GetExprKind() == ExprKind::DEBUGEXPR;
}

bool DeadCodeElimination::CheckTwoUsers(const std::vector<Expression*>& users) const
{
    // 2 denote that has two user
    return users.size() == 2 && users[0]->GetExprKind() == ExprKind::DEBUGEXPR &&
        users[1]->GetExprKind() == ExprKind::STORE;
}

bool DeadCodeElimination::CheckAllUsersIsNotUse(const Value& value, const std::vector<Expression*>& users)
{
    return std::all_of(users.begin(), users.end(), [&value](auto user) {
        if (user->GetExprKind() == ExprKind::LOAD) {
            return user->GetResult()->GetUsers().empty();
        }
        // store is not a use; load is.
        // not including storeelementref here because that may lead to a large scale of testcases adjustment
        if (Store* store = DynamicCast<Store>(user)) {
            if (auto var = DynamicCast<LocalVar>(store->GetLocation())) {
                // while store to function return value is a real usage
                if (var->IsRetValue()) {
                    return false;
                }
            }
            // the stored value is used, only the location is not used by Store
            return store->GetValue() != &value;
        }
        return user->GetExprKind() == ExprKind::DEBUGEXPR;
    });
}

void DeadCodeElimination::ReportUnusedExpression(Expression& expr)
{
    if (CheckUselessExpr(expr, true)) {
        // Some special expression has no users, but should not print warning.
        // for example cangjie code:
        // let x = match (true) {   --------------->`true` has no users, but shoud not print warning.
        //      case _ : (Bool,Int64) => 0
        // }
        if (expr.Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING) {
            return;
        }
        if (expr.GetResult()->Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING) {
            return;
        }
        auto res = GetDebugPos(expr);
        if (res.first) {
            if (IsCrossPackage(res.second.begin, currentPackageName, diag)) {
                return;
            }
            if (expr.GetExprKind() == ExprKind::LAMBDA) {
                DiagUnusedCode(res, DiagKindRefactor::chir_dce_unused_function,
                    StaticCast<const Lambda&>(expr).GetSrcCodeIdentifier());
            } else if (expr.IsUnaryExpr() || expr.IsBinaryExpr() || expr.IsIntOpWithException()) {
                auto kind = expr.GetExprKind() == ExprKind::INT_OP_WITH_EXCEPTION
                    ? GetLiteralFromExprKind(StaticCast<IntOpWithException*>(&expr)->GetOpKind())
                    : GetLiteralFromExprKind(expr.GetExprKind());
                diag.DiagnoseRefactor(DiagKindRefactor::chir_dce_unused_operator, res.second, kind);
            } else {
                diag.DiagnoseRefactor(DiagKindRefactor::chir_dce_unused_expression, res.second);
            }
        }
    }
}

void DeadCodeElimination::UselessExprElimination(const Package& package, bool isDebug) const
{
    for (auto func : package.GetGlobalFuncs()) {
        UselessExprEliminationForFunc(*func, isDebug);
    }
}

void DeadCodeElimination::UselessExprEliminationForFunc(const Func& func, bool isDebug) const
{
    std::queue<Expression*> worklist;
    std::unordered_set<Expression*> worklistSet;
    for (auto block : func.GetBody()->GetBlocks()) {
        for (auto expr : block->GetExpressions()) {
            if (CheckUselessExpr(*expr)) {
                worklist.push(expr);
                worklistSet.emplace(expr);
            }
        }
    }

    while (!worklist.empty()) {
        auto expr = worklist.front();
        worklist.pop();
        worklistSet.erase(expr);

        auto ops = expr->GetOperands();
        DumpForDebug(expr, nullptr, isDebug);
        expr->RemoveSelfFromBlock();

        for (auto op : ops) {
            if (op->IsLocalVar()) {
                auto opExpr = StaticCast<LocalVar*>(op)->GetExpr();
                if (CheckUselessExpr(*opExpr) && worklistSet.find(opExpr) == worklistSet.end()) {
                    worklist.push(opExpr);
                    worklistSet.emplace(opExpr);
                }
            }
        }
    }
}

void DeadCodeElimination::NothingTypeExprElimination(const Package& package, bool isDebug)
{
    for (auto func : package.GetGlobalFuncs()) {
        bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
        if (isCommonFunctionWithoutBody) {
            continue; // Nothing to visit
        }
        NothingTypeExprEliminationForFunc(*func->GetBody(), isDebug);
    }
}

namespace {
void CreateNullReturnValue(CHIRBuilder& builder, Block* parent, const BlockGroup& funcBody)
{
    if (auto retVal = GetReturnValue(funcBody)) {
        auto retValTy = GetFuncType(funcBody)->GetReturnType();
        auto null = builder.CreateConstantExpression<NullLiteral>(retValTy, parent);
        parent->AppendExpression(null);
        auto store = builder.CreateExpression<Store>(builder.GetUnitTy(), null->GetResult(), retVal, parent);
        parent->AppendExpression(store);
    }
}

// Return true if there exists a parameter of Nothing type; false otherwise.
bool HandleFuncWithNothingParamter(CHIRBuilder& builder, const BlockGroup& funcBody)
{
    const auto& params = GetFuncParams(funcBody);
    for (auto param : params) {
        if (!param->GetType()->IsNothing()) {
            continue;
        }
        // If the type of parameter is Nothing, this function can never be called.
        // However, we can not delete the function declaration and the function body directly.
        // We choose to modify the function body to exit the function directly.
        // The return value will be set to constant null if the return type is not Void.
        // e.g.
        // func @foo(..., %1: Nothing, ...) : Int64 {
        //      [ret] %0 = Allocate(Int64)
        //      %1: Int64 = Constant(Null)
        //      Store(%1, %0)
        //      Exit()
        // }
        auto entry = funcBody.GetEntryBlock();
        for (auto block : funcBody.GetBlocks()) {
            if (block != entry) {
                block->RemoveSelfFromBlockGroup();
            }
        }
        for (auto expr : entry->GetExpressions()) {
            if (auto debug = Cangjie::DynamicCast<Debug*>(expr);
                debug && debug->GetValue()->IsParameter()) {
                continue;
            }
            if (auto res = expr->GetResult(); res && res == GetReturnValue(funcBody)) {
                continue;
            }
            expr->RemoveSelfFromBlock();
        }
        CreateNullReturnValue(builder, entry, funcBody);
        entry->AppendExpression(builder.CreateTerminator<Exit>(entry));
        if (auto func = param->GetTopLevelFunc()) {
            func->Set<SkipCheck>(SkipKind::SKIP_VIC);
        }
        return true;
    }
    return false;
}

template <typename TApply> void HandleNothingTerminator(CHIRBuilder& builder, TApply* expr, BlockGroup& funcBody)
{
    CJC_ASSERT(Cangjie::Utils::In(expr->GetExprKind(),
        {ExprKind::APPLY_WITH_EXCEPTION, ExprKind::INVOKE_WITH_EXCEPTION, ExprKind::INVOKESTATIC_WITH_EXCEPTION}));
    auto newSuccessBlock = builder.CreateBlock(&funcBody);
    CreateNullReturnValue(builder, newSuccessBlock, funcBody);
    newSuccessBlock->AppendExpression(builder.CreateTerminator<Exit>(newSuccessBlock));

    auto oldSuccessBlock = expr->GetSuccessBlock();
    expr->ReplaceSuccessor(*oldSuccessBlock, *newSuccessBlock);
}
} // namespace

void DeadCodeElimination::NothingTypeExprEliminationForFunc(BlockGroup& funcBody, bool isDebug)
{
    if (HandleFuncWithNothingParamter(builder, funcBody)) {
        return;
    }
    for (auto block : funcBody.GetBlocks()) {
        bool isNothingIr = false;
        for (auto expr : block->GetExpressions()) {
            if (expr->GetExprKind() == ExprKind::LAMBDA) {
                NothingTypeExprEliminationForFunc(*StaticCast<Lambda*>(expr)->GetBody(), isDebug);
            }
            if (!isNothingIr && expr->GetResult() && expr->GetResultType()->IsNothing() && !expr->IsConstantNull()) {
                if (!expr->IsTerminator()) {
                    isNothingIr = true;
                    continue;
                } else {
                    switch (expr->GetExprKind()) {
                        case ExprKind::APPLY_WITH_EXCEPTION:
                            HandleNothingTerminator(builder, StaticCast<ApplyWithException*>(expr), funcBody);
                            break;
                        case ExprKind::INVOKE_WITH_EXCEPTION:
                            HandleNothingTerminator(builder, StaticCast<InvokeWithException*>(expr), funcBody);
                            break;
                        case ExprKind::INVOKESTATIC_WITH_EXCEPTION:
                            HandleNothingTerminator(builder, StaticCast<InvokeStaticWithException*>(expr), funcBody);
                            break;
                        default:
#ifndef NDEBUG
                            CJC_ABORT();
#else
                            return;
#endif
                    }
                }
            }
            if (isNothingIr) {
                expr->RemoveSelfFromBlock();
                DumpForDebug(expr, nullptr, isDebug);
            }
        }
        if (isNothingIr) {
            CreateNullReturnValue(builder, block, funcBody);
            block->AppendExpression(builder.CreateTerminator<Exit>(block));
        }
    }
}

void DeadCodeElimination::UnreachableBlockElimination(const Package& package, bool isDebug) const
{
    for (auto func : package.GetGlobalFuncs()) {
        bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
        if (isCommonFunctionWithoutBody) {
            continue; // Nothing to visit
        }
        UnreachableBlockEliminationForFunc(*func->GetBody(), isDebug);
    }
}

void DeadCodeElimination::UnreachableBlockElimination(const std::vector<const Func*>& funcs, bool isDebug) const
{
    for (auto& func : funcs) {
        bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
        if (isCommonFunctionWithoutBody) {
            continue; // Nothing to visit
        }
        UnreachableBlockEliminationForFunc(*func->GetBody(), isDebug);
    }
}

void DeadCodeElimination::UnreachableBlockWarningReporter(const Package& package,
    size_t threadsNum, const std::unordered_map<Block*, Terminator*>& maybeUnreachableBlocks)
{
    if (threadsNum == 1) {
        UnreachableBlockWarningReporterInSerial(package, maybeUnreachableBlocks);
    } else {
        UnreachableBlockWarningReporterInParallel(package, threadsNum, maybeUnreachableBlocks);
    }
}

void DeadCodeElimination::UnreachableBlockWarningReporterInSerial(
    const Package& package, const std::unordered_map<Block*, Terminator*>& maybeUnreachableBlocks)
{
    for (auto func : package.GetGlobalFuncs()) {
        bool isPrinted = false;
        Visitor::Visit(*func, [this, &isPrinted, &maybeUnreachableBlocks](Block& block) {
            auto it = maybeUnreachableBlocks.find(&block);
            if (it != maybeUnreachableBlocks.end()) {
                auto terminator = it->second;
                if (CheckUselessBlock(block)) {
                    PrintUnreachableBlockWarning(block, *terminator, isPrinted);
                }
            }
            return VisitResult::CONTINUE;
        });
    }
}

void DeadCodeElimination::UnreachableBlockWarningReporterInParallel(const Package& package,
    size_t threadsNum, const std::unordered_map<Block*, Terminator*>& maybeUnreachableBlocks)
{
    Utils::TaskQueue taskQueue(threadsNum);
    for (auto func : package.GetGlobalFuncs()) {
        bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
        if (isCommonFunctionWithoutBody) {
            continue; // Nothing to visit
        }

        taskQueue.AddTask<void>([this, func, &maybeUnreachableBlocks]() {
            bool isPrinted = false;
            Visitor::Visit(*func, [this, &isPrinted, &maybeUnreachableBlocks](Block& block) {
                auto it = maybeUnreachableBlocks.find(&block);
                if (it != maybeUnreachableBlocks.end()) {
                    auto terminator = it->second;
                    if (CheckUselessBlock(block)) {
                        PrintUnreachableBlockWarning(block, *terminator, isPrinted);
                    }
                }
                return VisitResult::CONTINUE;
            });
        });
    }
    taskQueue.RunAndWaitForAllTasksCompleted();
}

namespace {
std::optional<Block*> GetBranchTargetSucc(const Branch& branch)
{
    if (!branch.GetCondition()->IsLocalVar()) {
        return std::nullopt;
    }
    auto condExpr = Cangjie::StaticCast<LocalVar*>(branch.GetCondition())->GetExpr();
    if (condExpr->GetExprKind() != ExprKind::CONSTANT) {
        return std::nullopt;
    }
    return Cangjie::StaticCast<Constant*>(condExpr)->GetBoolLitVal() ? branch.GetTrueBlock() : branch.GetFalseBlock();
}

std::optional<Block*> GetMultiBranchTargetSucc(const MultiBranch& branch)
{
    if (!branch.GetCondition()->IsLocalVar()) {
        return std::nullopt;
    }
    auto condExpr = Cangjie::StaticCast<LocalVar*>(branch.GetCondition())->GetExpr();
    if (condExpr->GetExprKind() == ExprKind::TYPECAST) {
        if (!condExpr->GetOperand(0)->IsLocalVar()) {
            return std::nullopt;
        }
        condExpr = Cangjie::StaticCast<LocalVar*>(condExpr->GetOperand(0))->GetExpr();
    }
    if (condExpr->GetExprKind() != ExprKind::CONSTANT) {
        return std::nullopt;
    }

    auto constant = Cangjie::StaticCast<Constant*>(condExpr);
    uint64_t condVal = 0;
    if (constant->IsBoolLit()) {
        condVal = static_cast<uint64_t>(constant->GetBoolLitVal());
    } else if (constant->IsIntLit()) {
        condVal = constant->GetUnsignedIntLitVal();
    } else if (constant->IsRuneLit()) {
        condVal = static_cast<uint64_t>(constant->GetRuneLitVal());
    } else {
        Cangjie::InternalError("Unexpected const val kind");
    }
    auto cases = branch.GetCaseVals();
    for (size_t i = 0; i < cases.size(); ++i) {
        if (condVal == cases[i]) {
            return branch.GetSuccessor(i + 1);
        }
    }
    return branch.GetDefaultBlock();
}
} // namespace

void DeadCodeElimination::UnreachableBlockEliminationForFunc(const BlockGroup& body, bool isDebug) const
{
    auto blocks = body.GetBlocks();
    std::unordered_set<Block*> isUnreachable(blocks.begin(), blocks.end());
    std::queue<Block*> workList;

    auto entryBlock = body.GetEntryBlock();
    workList.push(entryBlock);
    isUnreachable.erase(entryBlock);

    while (!workList.empty()) {
        auto block = workList.front();
        workList.pop();

        for (auto expr : block->GetExpressions()) {
            if (expr->GetExprKind() == ExprKind::LAMBDA) {
                UnreachableBlockEliminationForFunc(*StaticCast<const Lambda*>(expr)->GetBody(), isDebug);
            }
        }

        auto terminator = block->GetTerminator();
        CJC_NULLPTR_CHECK(terminator);

        std::optional<Block*> targetSucc = std::nullopt;
        if (terminator->GetExprKind() == ExprKind::BRANCH) {
            targetSucc = GetBranchTargetSucc(*StaticCast<Branch*>(terminator));
        } else if (terminator->GetExprKind() == ExprKind::MULTIBRANCH) {
            targetSucc = GetMultiBranchTargetSucc(*StaticCast<MultiBranch*>(terminator));
        }
        if (targetSucc.has_value()) {
            auto cond = terminator->GetOperand(0);
            terminator->RemoveSelfFromBlock();
            if (cond->IsLocalVar() && cond->GetUsers().empty()) {
                auto expr = StaticCast<LocalVar*>(cond)->GetExpr();
                expr->RemoveSelfFromBlock();
            }
            DumpForDebug(terminator, nullptr, isDebug);
            block->AppendExpression(builder.CreateTerminator<GoTo>(targetSucc.value(), block));
        }

        auto succs = block->GetSuccessors();
        for (auto succ : succs) {
            if (isUnreachable.find(succ) != isUnreachable.end()) {
                isUnreachable.erase(succ);
                workList.push(succ);
            }
        }
    }

    for (auto block : isUnreachable) {
        block->RemoveSelfFromBlockGroup();
    }
}

void DeadCodeElimination::BreakBranchConnection(const Block& block) const
{
    for (auto pred : block.GetPredecessors()) {
        auto predTerminator = pred->GetTerminator();
        CJC_ASSERT(predTerminator);
        if (predTerminator->GetExprKind() == ExprKind::BRANCH) {
            auto oprand = predTerminator->GetOperand(0);
            auto branch = StaticCast<Branch*>(predTerminator);
            CJC_ASSERT(&block == branch->GetFalseBlock());
            auto target = branch->GetTrueBlock();
            branch->RemoveSelfFromBlock();
            if (oprand->GetUsers().empty()) {
                auto expr = StaticCast<LocalVar*>(oprand)->GetExpr();
                expr->RemoveSelfFromBlock();
            }
            pred->AppendExpression(builder.CreateTerminator<GoTo>(target, pred));
        } else {
            auto multi = StaticCast<MultiBranch*>(predTerminator);
            CJC_ASSERT(&block == multi->GetDefaultBlock());
            multi->ReplaceSuccessor(0, *multi->GetSuccessor(1));
        }
    }
}

void DeadCodeElimination::ClearUnreachableMarkBlock(const Package& package) const
{
    for (auto func : package.GetGlobalFuncs()) {
        bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
        if (isCommonFunctionWithoutBody) {
            continue; // Nothing to visit
        }
        ClearUnreachableMarkBlockForFunc(*func->GetBody());
    }
}

void DeadCodeElimination::ClearUnreachableMarkBlockForFunc(const BlockGroup& body) const
{
    for (auto block : body.GetBlocks()) {
        if (!block->TestAttr(Attribute::UNREACHABLE)) {
            for (auto expr : block->GetExpressions()) {
                if (expr->GetExprKind() == ExprKind::LAMBDA) {
                    ClearUnreachableMarkBlockForFunc(*StaticCast<const Lambda*>(expr)->GetBody());
                }
            }
            continue;
        }
        BreakBranchConnection(*block);
        block->RemoveSelfFromBlockGroup();
    }
}

bool DeadCodeElimination::CheckUselessFunc(const Func& func, const GlobalOptions& opts)
{
    if (!func.GetUsers().empty()) {
        return false;
    }
    if (func.GetIdentifierWithoutPrefix() == USER_MAIN_MANGLED_NAME) {
        return false;
    }
    // All func names contains "*_global_init*" is global variables initial function.
    if (func.GetIdentifier().find(GLOBAL_INIT_MANGLED_NAME) != std::string::npos) {
        return false;
    }
    if (func.GetIdentifier().find(STD_CORE_FUTURE_MANGLED_NAME) != std::string::npos) {
        // All func names contains "*_CNat6Future*" is Future related series functions.
        return false;
    }
    if (func.GetIdentifier().find(ANNOTATION_VAR_POSTFIX) != std::string::npos) {
        // annotation var init functions are to be evaluated are removed by const eval
        return false;
    }
    if (func.GetFuncKind() == Cangjie::CHIR::MAIN_ENTRY && !opts.interpreter) {
        // if output is lib (static or shared), Main method should delete
        // when use chir-interpreter execute cjc-frontend ('cjc-frontend --chir-interpreter ...'), we should not
        // delete main method
        return opts.outputMode == GlobalOptions::OutputMode::STATIC_LIB ||
            opts.outputMode == GlobalOptions::OutputMode::SHARED_LIB;
    }
    // Do not check the functions that can be exported.
    if (IsExternalDecl(func)) {
        return false;
    }
    // The func is in vtable.
    if (func.IsVirtualFunc()) {
        return false;
    }
    // should be revised
    if (func.GetFuncKind() == Cangjie::CHIR::CLASS_CONSTRUCTOR) {
        return false;
    }
    // The Finalizer func of a class, can not be removed.
    if (func.GetFuncKind() == Cangjie::CHIR::FINALIZER) {
        return false;
    }
    if (!opts.disableReflection) {
        // enable reflect
        return !func.TestAttr(Attribute::PUBLIC) || func.TestAttr(Attribute::NO_REFLECT_INFO);
    }
    return true;
}

// Check whether the block is unreachable.
bool DeadCodeElimination::CheckUselessBlock(const Block& block) const
{
    return !block.IsEntry() && block.GetPredecessors().empty() && !block.TestAttr(Attribute::UNREACHABLE);
}

Ptr<Expression> DeadCodeElimination::GetUnreachableExpression(const CHIR::Block& block, bool& isNormal) const
{
    auto expressions = block.GetExpressions();
    Ptr<Expression> resExpression = nullptr;
    auto it = std::find_if(expressions.begin(), expressions.end(), [&isNormal, &resExpression, this](auto expression) {
        auto posForWarning = expression->template Get<DebugLocationInfoForWarning>();
        auto [res, warningRange] = ToRangeIfNotZero(posForWarning);
        if (res) {
            // when report deadcode in BinaryExpr, it has 4 situations in tests/LLT/CHIR/DCE/testUnusedOp03.cj
            // if operand in binaryExpr is nothing, use warninglocation as report position
            // if all operand in binaryExpr is normal, use binaryExpr location as report position
            if (expression->IsBinaryExpr() || expression->IsIntOpWithException()) {
                auto args = expression->GetOperands();
                auto it =
                    std::find_if(args.begin(), args.end(), [](auto item) { return item->GetType()->IsNothing(); });
                if (it == args.end()) {
                    resExpression = expression;
                    isNormal = true;
                    return true;
                }
            }
            if (expression->GetExprKind() != ExprKind::DEBUGEXPR) {
                // variable debug node has warn position, but we want get it's normal position.
                resExpression = expression;
                isNormal = false;
                return true;
            }
        }
        if (!CheckUsersOfExpr(*expression)) {
            return false;
        }
        auto& debugInfo = expression->GetDebugLocation();
        auto [newResult, debugRange] = ToRangeIfNotZero(debugInfo);
        if (newResult && !IsCrossPackage(debugRange.begin, currentPackageName, diag)) {
            resExpression = expression;
            isNormal = true;
            return true;
        }
        return false;
    });
    if (it != expressions.end()) {
        return resExpression;
    }
    return nullptr;
}

void DeadCodeElimination::PrintUnreachableBlockWarning(
    const CHIR::Block& block, const CHIR::Terminator& terminator, bool& isPrinted)
{
    // Get position of nothing type node.
    auto& debugInfo = terminator.GetDebugLocation();
    auto [res, terminalNodeRange] = ToRangeIfNotZero(debugInfo);

    if (res && !isPrinted) {
        auto expressions = block.GetExpressions();
        bool isNormal = true;
        if (auto unreableExpr = GetUnreachableExpression(block, isNormal)) {
            auto range = isNormal ? ToRange(unreableExpr->GetDebugLocation())
                : ToRange(unreableExpr->Get<DebugLocationInfoForWarning>());
            // Need to remove after find the cause.
            if (terminalNodeRange.begin == range.begin) {
                return;
            }
            if (IsCrossPackage(terminalNodeRange.begin, currentPackageName, diag)) {
                return;
            }
            if (unreableExpr->Get<SkipCheck>() == SkipKind::SKIP_DCE_WARNING) {
                return;
            }
            // Note: For the code:`if (let a <- 1) {return 1}`
            // the position of the Unit node is the position of the ifExpr,so it will generate a wrong
            // warnging,we should skip this scenario.
            if (unreableExpr->GetExprKind() == ExprKind::CONSTANT &&
                StaticCast<Constant*>(unreableExpr)->IsUnitLit()) {
                if (unreableExpr->GetResult()->TestAttr(Attribute::COMPILER_ADD)) {
                    return;
                }
            }
            if (!isNormal && (unreableExpr->IsBinaryExpr() || unreableExpr->IsUnaryExpr())) {
                auto diagBuilder = diag.DiagnoseRefactor(DiagKindRefactor::chir_dce_unreachable, range, "operator");
                diagBuilder.AddMainHintArguments("operator");
                diagBuilder.AddHint(terminalNodeRange);
            } else if (!isNormal && unreableExpr->GetExprKind() == ExprKind::APPLY) {
                auto diagBuilder = diag.DiagnoseRefactor(DiagKindRefactor::chir_dce_unreachable, range, "call");
                diagBuilder.AddMainHintArguments("call");
                diagBuilder.AddHint(terminalNodeRange);
            } else {
                auto diagBuilder =
                    diag.DiagnoseRefactor(DiagKindRefactor::chir_dce_unreachable_expression_hint, range);
                diagBuilder.AddHint(terminalNodeRange);
            }
            isPrinted = true;
        }
    }
}

bool DeadCodeElimination::CheckUselessExpr(const Expression& expr, bool isReportWarning) const
{
    if (expr.GetResult() && !expr.GetResult()->GetUsers().empty()) {
        return false;
    }
    if (expr.GetExprKind() == ExprKind::INT_OP_WITH_EXCEPTION && expr.Get<NeverOverflowInfo>() && isReportWarning) {
        return true;
    }
    if (expr.IsTerminator()) {
        return false;
    }
    if (expr.Get<NeverOverflowInfo>()) {
        return true;
    }
    auto kind = expr.GetExprKind();
    switch (expr.GetExprMajorKind()) {
        case ExprMajorKind::UNARY_EXPR: {
            // Theoretically, this condition can be revised to `unary->GetOverflowStrategy() !=
            // OverflowStrategy::THROWING;` However, we are not sure if there are some redundant THROWING attribute.
            auto unary = StaticCast<const UnaryExpression*>(&expr);
            return kind != ExprKind::NEG || unary->GetOverflowStrategy() != OverflowStrategy::THROWING;
        }
        case ExprMajorKind::BINARY_EXPR: {
            if (kind == ExprKind::MOD || kind == ExprKind::DIV || kind == ExprKind::LSHIFT ||
                kind == ExprKind::RSHIFT) {
                // MOD/DIV may throw div_by_zero exception; LSHIFT/RSHIFT may throw overshift exception.
                return false;
            } else if (kind >= ExprKind::ADD && kind <= ExprKind::EXP) {
                // all the other arithemetic operations and bitwise shift operations.
                auto binary = StaticCast<const BinaryExpression*>(&expr);
                return binary->GetOverflowStrategy() != OverflowStrategy::THROWING;
            } else {
                return true;
            }
        }
        case ExprMajorKind::MEMORY_EXPR: {
            return kind == ExprKind::LOAD || kind == ExprKind::GET_ELEMENT_REF;
        }
        case ExprMajorKind::STRUCTURED_CTRL_FLOW_EXPR: {
            return kind == ExprKind::LAMBDA;
        }
        case ExprMajorKind::OTHERS: {
            return kind == ExprKind::CONSTANT || kind == ExprKind::TUPLE || kind == ExprKind::FIELD ||
                kind == ExprKind::VARRAY;
        }
        default: {
            CJC_ABORT();
            return false;
        }
    }
}

template <typename... Args>
void DeadCodeElimination::DiagUnusedCode(
    const std::pair<bool, Cangjie::Range>& nodeRange, DiagKindRefactor diagKind, Args&&... args)
{
    if (nodeRange.first && !IsCrossPackage(nodeRange.second.begin, currentPackageName, diag)) {
        diag.DiagnoseRefactor(diagKind, nodeRange.second, args...);
    }
}
