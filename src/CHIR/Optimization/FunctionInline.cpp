// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements function inline feature
 */

#include "cangjie/CHIR/Optimization/FunctionInline.h"

#include <list>

#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Type/PrivateTypeConverter.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"
#include "cangjie/CHIR/Optimization/BlockGroupCopyHelper.h"

using namespace Cangjie::CHIR;

// Set the threshold value of inline
constexpr static size_t INIT_INLINE_THRESHOLD = 20;
// Set the threshold value of inlined call in one function to avoid code expandsion
constexpr static size_t INLINED_COUNT_THRESHOLD = 20;
// This is for efficiency and avoid overflowing on `size`, we will
// terminate the counting if the size is already 3 times of the threshold
constexpr static size_t SEARCH_THRESHOLD = INIT_INLINE_THRESHOLD * 3;

// Increase the threshold value by 20%
constexpr static size_t INCREASE_THRESHOLD = 5;

// Step when the callee is with lambda parameter
constexpr static size_t INCREASE_WHEN_CALLEE_WITH_LAMBDA_ARG = 2;

// Set the threshold of CHIR in one block for inline
constexpr static size_t INLINED_BLOCKSIZE_THRESHOLD = 10000;

static const std::vector<FuncInfo> functionInlineWhiteList = {
    FuncInfo("get", "Array", {NOT_CARE}, ANY_TYPE, "std.core"),
    FuncInfo("set", "Array", {NOT_CARE}, ANY_TYPE, "std.core"),
    FuncInfo("[]", "Array", {NOT_CARE}, ANY_TYPE, "std.core"),
    FuncInfo("copyTo", "Array", {NOT_CARE}, ANY_TYPE, "std.core"),
    FuncInfo("utf8Size", "Rune", {NOT_CARE}, ANY_TYPE, "std.core"),
    FuncInfo("[]", "String", {NOT_CARE}, ANY_TYPE, "std.core"),
    FuncInfo("init", "ArrayList", {NOT_CARE}, ANY_TYPE, "std.collection"),
    FuncInfo("get", "ArrayList", {NOT_CARE}, ANY_TYPE, "std.collection"),
    FuncInfo("set", "ArrayList", {NOT_CARE}, ANY_TYPE, "std.collection"),
    FuncInfo("append", "ArrayList", {NOT_CARE}, ANY_TYPE, "std.collection"),
    FuncInfo("[]", "ArrayList", {NOT_CARE}, ANY_TYPE, "std.collection"),
    FuncInfo("checkRange", "ArrayList", {NOT_CARE}, ANY_TYPE, "std.collection"),
    FuncInfo("remove", "ArrayList", {NOT_CARE}, ANY_TYPE, "std.collection"),
    FuncInfo("==", "HashSet", {NOT_CARE}, ANY_TYPE, "std.collection"),
    FuncInfo("!=", "HashSet", {NOT_CARE}, ANY_TYPE, "std.collection"),
};

static const std::vector<FuncInfo> functionInlineBlackList = {
    // Do not inline the `Future::init` for the conveninence of redundant future removal optimization
    FuncInfo("init", "Future", {NOT_CARE}, ANY_TYPE, "std.core"),
    // Do not inline the `arrayInitByFunction` for the conveninence of array lambda optimization
    FuncInfo("arrayInitByFunction", "", {NOT_CARE}, NOT_CARE, "std.core"),
    // Temporary disable the inline of `callNativeFunc` due to a weird bug in Windows
    FuncInfo("callNativeFunc", NOT_CARE, {NOT_CARE}, NOT_CARE, "std.fs"),
    // inline wrappingShl in for loop can result in signficant performance hit
    FuncInfo("wrappingShl", NOT_CARE, {NOT_CARE}, ANY_TYPE, "std.overflow"),
    FuncInfo("wrappingShr", NOT_CARE, {NOT_CARE}, ANY_TYPE, "std.overflow"),
};

void FunctionInline::InlineImpl(BlockGroup& bg)
{
    auto postVisit = [this](Expression& e) {
        if (e.GetExprKind() == ExprKind::LAMBDA) {
            auto lambda = StaticCast<Lambda*>(&e);
            InlineImpl(*lambda->GetBody());
        }
        if (e.GetExprKind() != ExprKind::APPLY) {
            return VisitResult::CONTINUE;
        }

        auto& apply = StaticCast<Apply&>(e);
        if (CheckCanRewrite(apply)) {
            DoFunctionInline(apply, optName);
        }

        return VisitResult::CONTINUE;
    };
    Visitor::Visit(bg, [](Expression&) { return VisitResult::CONTINUE; }, postVisit);
}

void FunctionInline::Run(Func& func)
{
    globalFunc = &func;
    InlineImpl(*func.GetBody());
}

const OptEffectCHIRMap& FunctionInline::GetEffectMap() const
{
    return effectMap;
}

static bool InBlackList(const Func& func)
{
    if (func.GetFuncKind() == FuncKind::MACRO_FUNC || func.GetFuncKind() == FuncKind::GLOBALVAR_INIT ||
        func.GetFuncKind() == FuncKind::MAIN_ENTRY) {
        return true;
    }
    if (func.GetFuncType()->IsCFunc()) {
        return true;
    }
    if (func.TestAttr(Attribute::NO_INLINE)) {
        return true;
    }

    for (auto element : functionInlineBlackList) {
        if (IsExpectedFunction(func, element)) {
            return true;
        }
    }

    return false;
}

static bool InWhiteList(const Func& func)
{
    for (auto element : functionInlineWhiteList) {
        if (IsExpectedFunction(func, element)) {
            return true;
        }
    }
    return false;
}

static bool OnlyCalledOnce(const Func& func)
{
    bool alreadyHasUser = false;
    for (auto user : func.GetUsers()) {
        if (user->GetExprKind() == ExprKind::APPLY || user->GetExprKind() == ExprKind::APPLY_WITH_EXCEPTION) {
            if (alreadyHasUser) {
                return false;
            }
            alreadyHasUser = true;
        }
    }
    return true;
}

static bool IsHotSpotCall(const Func& callee)
{
    // Case A: the callee is operator overloading function like `[]`, `+`, `-`
    if (callee.TestAttr(Attribute::OPERATOR)) {
        return true;
    }
    // Case B: the call site is located in a loop
    // note: currently unsupported
    return false;
}

static bool FunctionWithLambdaArg(const Func& func)
{
    for (auto arg : func.GetParams()) {
        if (arg->GetType()->IsFunc()) {
            return true;
        }
    }
    return false;
}

static size_t CalculateThreshold(const Value& callee, const Cangjie::GlobalOptions::OptimizationLevel& optLevel)
{
    size_t realThreshold = INIT_INLINE_THRESHOLD;
    auto func = Cangjie::DynamicCast<const Func*>(&callee);
    CJC_NULLPTR_CHECK(func);
    if (OnlyCalledOnce(*func)) {
        // Increase the threshold value by 20%
        realThreshold += realThreshold / INCREASE_THRESHOLD;
    }
    if (optLevel < Cangjie::GlobalOptions::OptimizationLevel::Os) {
        if (IsHotSpotCall(*func)) {
            // Increase the threshold value by 20%
            realThreshold = INIT_INLINE_THRESHOLD + INIT_INLINE_THRESHOLD / INCREASE_THRESHOLD;
        }
        if (FunctionWithLambdaArg(*func)) {
            realThreshold = INIT_INLINE_THRESHOLD * INCREASE_WHEN_CALLEE_WITH_LAMBDA_ARG;
        }
    }
    return realThreshold;
}

static size_t GetExprSize(const Expression& expr)
{
    size_t exprSize = 0;
    if (expr.GetExprKind() != ExprKind::LAMBDA) {
        return ++exprSize;
    }
    auto postVisit = [&exprSize](Expression& e) {
        exprSize += GetExprSize(e);
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(*Cangjie::StaticCast<const Lambda&>(expr).GetBody(), postVisit);
    return exprSize;
}

static size_t CountFuncSize(const Func& func)
{
    size_t funcSize = 0;
    for (auto block : func.GetBody()->GetBlocks()) {
        for (auto e : block->GetExpressions()) {
            funcSize += GetExprSize(*e);
            if (funcSize >= SEARCH_THRESHOLD) {
                break;
            }
        }
    }
    return funcSize;
}

bool FunctionInline::CheckCanRewrite(const Apply& apply)
{
    auto callee = apply.GetCallee();
    // imported func decl, intrinsic func decl, foreign func decl are excluded
    if (!callee->IsFuncWithBody()) {
        return false;
    }
    auto func = VirtualCast<Func*>(callee);
    CJC_NULLPTR_CHECK(callee);

    // when the terminator of this block is RaiseException, do not inline this apply because it rarely happens
    if (auto block = apply.GetParentBlock(); Is<RaiseException>(block->GetTerminator())) {
        return false;
    }

    // Omit the function inline in block that exceed the Blocksize
    // threshold to avoid the huge time consume.
    auto block = apply.GetParentBlock();
    if (block->GetExpressions().size() >= INLINED_BLOCKSIZE_THRESHOLD) {
        return false;
    }

    // recursive function doesn't need to inline
    // if you really want to inline it, yes, you can, it won't cause a problem
    if (callee == globalFunc) {
        return false;
    }
    if (InBlackList(*func)) {
        return false;
    }
    if (InWhiteList(*func)) {
        return true;
    }
    if (func->GetFuncKind() == FuncKind::INSTANCEVAR_INIT) {
        return true;
    }
    // Determine if we can inline by checking the size of callee exceed the threshold
    if (inlinedCountMap[globalFunc] >= INLINED_COUNT_THRESHOLD) {
        return false;
    }
    size_t realThreshold = CalculateThreshold(*callee, optLevel);
    // `res` is std::pair<iterator, bool>, so
    // `res.second == true` means `callee` is emplaced successfully, then we must set correct function size
    // `res.second == false` means `callee` has already been emplaced before, we can use its size directly
    auto res = funcSizeMap.emplace(func, 0);
    if (res.second) {
        res.first->second = CountFuncSize(*func);
    }
    if (res.first->second <= realThreshold) {
        inlinedCountMap[globalFunc]++;
        return true;
    }
    return false;
}

static std::vector<Block*> GetExitBlocks(const BlockGroup& blockGroup)
{
    std::vector<Block*> exitBlocks;
    for (auto block : blockGroup.GetBlocks()) {
        auto term = block->GetTerminator();
        CJC_NULLPTR_CHECK(term);
        if (term->GetExprKind() == ExprKind::EXIT) {
            // Need refactor
            exitBlocks.emplace_back(block);
        }
    }
    // there may be not exit block, all block's terminator is `Raise`
    return exitBlocks;
}

static void ReplaceFuncArgs(std::vector<Parameter*>& src, std::vector<Value*>& dst, const BlockGroup& scope)
{
    CJC_ASSERT(src.size() == dst.size());
    for (size_t i = 0; i < src.size(); ++i) {
        src[i]->ReplaceWith(*dst[i], &scope);
    }
}

void FunctionInline::ReplaceFuncResult(LocalVar* resNew, LocalVar* resOld)
{
    auto valType = resNew->GetType();
    CJC_ASSERT(valType->IsRef());
    auto users = resOld->GetUsers();
    // Don't need to handle it when the result of apply is not used.
    if (users.size() == 0) {
        return;
    }
    valType = Cangjie::StaticCast<RefType*>(valType)->GetBaseType();
    // Insert a load from the new local variable of result at the end of Exit Block of Inlined function.
    // Replace the use of the old local variable of result with the result of load.
    for (auto user : users) {
        auto newLoad =
            builder.CreateExpression<Load>(user->GetDebugLocation(), valType, resNew, user->GetParentBlock());
        newLoad->MoveBefore(user);
        user->ReplaceOperand(resOld, newLoad->GetResult());
        auto cast = TypeCastOrBoxIfNeeded(
            *newLoad->GetResult(), *resOld->GetType(), builder, *newLoad->GetParentBlock(), INVALID_LOCATION);
        if (cast != newLoad->GetResult()) {
            CJC_ASSERT(cast->IsLocalVar());
            StaticCast<LocalVar*>(cast)->GetExpr()->MoveBefore(user);
            user->ReplaceOperand(newLoad->GetResult(), cast);
        }
    }
    return;
}

void FunctionInline::SetGroupDebugLocation(BlockGroup& group, const DebugLocation& loc)
{
    group.SetDebugLocation(loc);
    auto changeLoc = [&loc](Expression& expr) {
        expr.SetDebugLocation(loc);
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(group, changeLoc, []([[maybe_unused]] Expression& e) { return VisitResult::CONTINUE; });
}

void FunctionInline::DoFunctionInline(const Apply& apply, const std::string& name)
{
    RecordEffectMap(apply);
    PrintOptInfo(apply, debug, name);
    // inline foo1 to foo2, `apply` node belongs to foo2, apply's callee is foo1, for example:
    // func foo1(param: Int64) { return param }
    // func foo2() { foo1(2) }
    // CHIR graph is like:
    // Func foo1(%7: Int64) { // Block Group: 1
    // Block #7:
    //   %8: Unit = Debug(%7, param)
    //   [ret]%9: Int64& = Allocate(Int64)
    //   %10: Unit = Store(%7, %9)
    //   Exit()
    // }
    // Func foo2 { // Block Group: 2
    // Block #14:
    //   %[ret]%11: Int64& = Allocate(Int64)
    //   %12: Int64 = ConstantInt(2)
    //   %13: Int64 = Apply(@_CN7default4foo1El, %12)  // triggered function inline
    //   %14: Unit = Store(%13, %11)
    //   Exit()
    // }

    // step 1: clone function body
    // `oldFuncGroup` is `Block Group: 1`
    BlockGroup* oldFuncGroup = nullptr;
    std::vector<Parameter*> funcArgs;
    if (apply.GetCallee()->IsFuncWithBody()) {
        auto func = VirtualCast<Func*>(apply.GetCallee());
        oldFuncGroup = func->GetBody();
        funcArgs = func->GetParams();
    } else {
        CJC_ASSERT(apply.GetCallee()->IsLocalVar());
        auto lambda = DynamicCast<Lambda*>(StaticCast<LocalVar*>(apply.GetCallee())->GetExpr());
        CJC_NULLPTR_CHECK(lambda);
        oldFuncGroup = lambda->GetBody();
        funcArgs = lambda->GetParams();
    }
    CJC_NULLPTR_CHECK(oldFuncGroup->GetTopLevelFunc());
    // after cloned, we get a new block group, but func arg is value node, can't be cloned:
    // { // Block Group: 7
    // Block #27:
    //   %19: Unit = Debug(%7, param)
    //   %20: Int64& = Allocate(Int64)
    //   %21: Unit = Store(%7, %20)
    //   Exit()
    // }
    // `newFuncGroup` is `Block Group: 7`
    CJC_NULLPTR_CHECK(apply.GetTopLevelFunc());
    auto [newFuncGroup, returnVal] = CloneBlockGroupForInline(*oldFuncGroup, *apply.GetTopLevelFunc(), apply);
    SetGroupDebugLocation(*newFuncGroup, apply.GetDebugLocation());
    // `funcEntry` is `Block #27`
    auto funcEntry = newFuncGroup->GetEntryBlock();
    // `exitBlock` is `Block #27`, `returnVal` is `%20`
    auto exitBlocks = GetExitBlocks(*newFuncGroup);
    CJC_NULLPTR_CHECK(newFuncGroup->GetTopLevelFunc());

    // step 2: change connection of func args
    // Func foo2 { // Block Group: 4
    // Block #14:
    //   [ret] %11: Int64& = Allocate(Int64)
    //   %12: Int64 = Constant(2)
    //   %13: Int64 = Apply(@_CN7default4foo1El, %12)
    //   %14: Unit = Store(%13, %11)
    //   Exit()
    // Block #27:
    //   %19: Unit = Debug(%12, param)
    //   %20: Int64& = Allocate(Int64)
    //   %21: Unit = Store(%12, %20)    // func arg %7 -> %12
    //   Exit()
    // }
    auto applyArgs = apply.GetArgs();
    ReplaceFuncArgs(funcArgs, applyArgs, *newFuncGroup);

    FixCastProblemAfterInst(newFuncGroup, builder);

    // step 3: move copied blocks
    // Func foo2 { // Block Group: 4
    // Block #14:
    //   [ret] %11: Int64& = Allocate(Int64)
    //   %12: Int64 = Constant(2)
    //   %13: Int64 = Apply(@_CN7default4foo1El, %12)
    //   %14: Unit = Store(%13, %11)
    //   Exit()
    // Block #27:
    //   %19: Unit = Debug(%7, param)
    //   %20: Int64& = Allocate(Int64)
    //   %21: Unit = Store(%7, %20)
    //   Exit()
    // }
    // `applyGroup` is `Block Group: 2`
    auto applyGroup = apply.GetParentBlock()->GetParentBlockGroup();
    CJC_NULLPTR_CHECK(applyGroup->GetTopLevelFunc());
    for (auto block : newFuncGroup->GetBlocks()) {
        block->MoveTo(*applyGroup);
    }

    // if callee must throw exception, then we can't get return value
    // step 4 : Insert a load for return value of inlined function and replace the use
    // Func foo2 { // Block Group: 4
    // Block #14:
    //   [ret] %11: Int64& = Allocate(Int64)
    //   %12: Int64 = Constant(2)
    //   %13: Int64 = Apply(@_CN7default4foo1El, %12)
    //   %22: Int64 = Load(%20)      // Insert Load before the use of result
    //   %14: Unit = Store(%22, %11) // Replace the use of old return value %13 -> %22
    // Block #27:
    //   %19: Unit = Debug(%12, param)
    //   %20: Int64& = Allocate(Int64)
    //   Exit()                      // add terminator to goto block 2: #28
    // }
    if (returnVal != nullptr) {
        ReplaceFuncResult(returnVal, StaticCast<LocalVar*>(apply.GetResult()));
    }
    // step 5: split block to 2 pieces and remove `apply` node
    // Func foo2 { // Block Group: 4
    // Block #14:                       // `Block #14` is split to `Block #14` and `Block #28`
    //   [ret] %11: Int64& = Allocate(Int64)
    //   %12: Int64 = Constant(2)
    //   GoTo(#28)
    // Block #27:
    //   %19: Unit = Debug(%12, param)
    //   %20: Int64& = Allocate(Int64)
    //   %21: Unit = Store(%12, %20)
    //   Exit()
    // Block #28:
    //   %22: Int64 = Load(%20)
    //   %14: Unit = Store(%22, %11)
    //   Exit()
    // }
    auto [block1, block2] = builder.SplitBlock(apply);

    // step 6: change connection in `Block Group 4`
    // Func foo2 { // Block Group: 4
    // Block #21:
    //   [ret] %11: Int64& = Allocate(Int64)
    //   %12: Int64 = Constant(2)
    //   GoTo(#27)               // change successor #28 -> #27
    // Block #27:
    //   %19: Unit = Debug(%12, param)
    //   %20: Int64& = Allocate(Int64)
    //   %21: Unit = Store(%12, %20)
    //   Goto(#28)
    // Block #28:
    //   %22: Int64 = Load(%20)
    //   %14: Unit = Store(%22, %11)
    //   Exit()
    // }
    CJC_NULLPTR_CHECK(block1->GetTerminator());
    block1->GetTerminator()->ReplaceSuccessor(*block2, *funcEntry);
    // if callee must throw exception, then we can't get `exitBlock`
    for (auto exitBlock : exitBlocks) {
        if (exitBlock != nullptr) {
            CJC_NULLPTR_CHECK(exitBlock->GetTerminator());
            exitBlock->GetTerminator()->RemoveSelfFromBlock();
            auto term2 = builder.CreateTerminator<GoTo>(block2, exitBlock);
            exitBlock->AppendExpression(term2);
        }
    }
}

void FunctionInline::RecordEffectMap(const Apply& apply)
{
    auto callee = DynamicCast<CHIR::Func*>(apply.GetCallee());
    // `callee` may be a lambda, we only record global function
    if (callee == nullptr) {
        return;
    }
    auto parentFunc = apply.GetTopLevelFunc();
    CJC_NULLPTR_CHECK(parentFunc);
    if (!callee->IsLambda() && !parentFunc->IsLambda()) {
        effectMap[callee].emplace(parentFunc);
    }
}

// Clone a new block group for Function Inline and return the new local variableCHIRBuilder::CloneBlock
// for function result value.
std::pair<BlockGroup*, LocalVar*> FunctionInline::CloneBlockGroupForInline(
    const BlockGroup& other, Func& parentFunc, const Apply& apply)
{
    BlockGroupCopyHelper helper(builder);
    helper.GetInstMapFromApply(apply);
    auto [newGroup, newBlockGroupRetValue] = helper.CloneBlockGroup(other, parentFunc);
    return {newGroup, newBlockGroupRetValue};
}
