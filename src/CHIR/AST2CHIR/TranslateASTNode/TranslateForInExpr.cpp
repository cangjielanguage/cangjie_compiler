// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
// ============CFG for ForInExpr============
// func foo() {
//     for (i in 0..100) {
//         break
//         //return
//         //continue
//     }
//     println("dd")
// }
// ------------------------
// forDelayExitLevel = 0
// ForIn(
// {
//     forDelayExitLevel = 1  //break
//     // forDelayExitLevel = 2  //return
//     // forDelayExitLevel = 0  //continue
// },
// {
//     if (forDelayExitLevel) {
//         Exit()
//     } else {
//         i++
//     }
// },
// {
//     if (forDelayExitLevel) {
//         --forDelayExitLevel
//         cond = false
//         Exit()
//     } else {
//         i < stopVar
//     }
// })
// if (forDelayExitLevel > 0) {
//     --forDelayExitLevel
//     Exit()
// } else {
//     println("dd")
// }
// ========================ForInExpr embeded====================
// func foo() {
//     for (i in 0..100) {
//         for (j in 0..100) {
//             break
//             //return
//             //continue
//         }
//         println("cc")
//     }
//     println("dd")
// }
// ------------------------
// forDelayExitLevel1 = 0
// ForIn(
// {
//     ForIn(
//     {
//         forDelayExitLevel1 = 1  //break
//         // forDelayExitLevel1 = 4  //return
//         // forDelayExitLevel1 = 0  //continue
//     },
//     {
//         if (forDelayExitLevel1) {
//             Exit()
//         } else {
//             j++
//         }
//     },
//     {
//         if (forDelayExitLevel1) {
//             --forDelayExitLevel1
//             cond = false
//             Exit()
//         } else {
//             j < stopVar
//         }
//     })
//     if (forDelayExitLevel1 > 0) {
//         --forDelayExitLevel1
//         Exit()
//     } else {
//         println("cc")
//     }
// },
// {
//     if (forDelayExitLevel1) {
//         Exit()
//     } else {
//         i++
//     }
// },
// {
//     if (forDelayExitLevel1) {
//         --forDelayExitLevel1
//         cond = false
//         Exit()
//     } else {
//         i < stopVar
//     }
// })
// if (forDelayExitLevel1 > 0) {
//     --forDelayExitLevel1
//     Exit()
// } else {
//     println("dd")
// }
// ========================IFExpr embeded====================
// func foo() {
//     for (i in 0..100) {
//         if (i < 10) {
//             break
//             //return
//             //continue
//         } else {
//             println("bb")
//         }
//         println("cc")
//     }
//     println("dd")
// }
// ------------------------
// forDelayExitLevel1 = 0
// ForIn(
// {
//     forDelayExitLevel1 = 0
//     IF(
//     {
//         forDelayExitLevel1 = 2  //break
//         // forDelayExitLevel1 = 3  //return
//         // forDelayExitLevel1 = 1  //continue
//     },
//     {
//         println("bb")
//     })
//     if (forDelayExitLevel1 > 0) {
//         --forDelayExitLevel1
//         Exit()
//     } else {
//         println("cc")
//     }
// },
// {
//     if (forDelayExitLevel1) {
//         Exit()
//     } else {
//         i++
//     }
// },
// {
//     if (forDelayExitLevel1) {
//         --forDelayExitLevel1
//         cond = false
//         Exit()
//     } else {
//         i < stopVar
//     }
// })
// if (forDelayExitLevel1 > 0) {
//     --forDelayExitLevel1
//     Exit()
// } else {
//     println("dd")
// }

#include "cangjie/AST/Node.h"
#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/GeneratedFromForIn.h"
#include "cangjie/CHIR/Package.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;
using namespace AST;
using namespace std;

namespace Cangjie::CHIR {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
static Ptr<Value> TranslateForInClosedRangeOne(Translator& tr, const ForInExpr& forInExpr);
#endif
Ptr<Value> Translator::Visit(const AST::ForInExpr& forInExpr)
{
    Ptr<Value> forIn = nullptr;
    if (forInExpr.forInKind == ForInKind::FORIN_ITER) {
        forIn = TranslateForInIter(forInExpr);
    } else if (forInExpr.forInKind == ForInKind::FORIN_STRING) {
        forIn = TranslateForInString(forInExpr);
    } else if (forInExpr.forInKind == ForInKind::FORIN_RANGE) {
        forIn = TranslateForInRange(forInExpr);
    } else {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        if (forInExpr.IsClosedRangeOne()) {
            return TranslateForInClosedRangeOne(*this, forInExpr);
        }
#endif
        InternalError("translating unsupported ForInExpr");
    }
    return forIn;
}

Ptr<Value> Translator::GetOuterMostExpr()
{
    Ptr<Value> finalValidExpr = nullptr;
    for (auto reverseBegin = blockGroupStack.crbegin(), reverseEnd = blockGroupStack.crend();
        reverseBegin != reverseEnd; ++reverseBegin) {
        Ptr<BlockGroup> bg = *reverseBegin;
        Expression* ownedExpr = bg->GetOwnerExpression();
        if ((ownedExpr && ownedExpr->GetExprKind() == ExprKind::LAMBDA) ||
            (currentBlock->GetTopLevelFunc() && bg == currentBlock->GetTopLevelFunc()->GetBody())) {
            return finalValidExpr != nullptr ? finalValidExpr : nullptr;
        }
        if (ownedExpr && (Is<ForIn>(ownedExpr) || ownedExpr->GetExprKind() == ExprKind::IF)) {
            finalValidExpr = ownedExpr->GetResult();
        }
    }
    return finalValidExpr;
}

void Translator::InitializeDelayExitSignal(const DebugLocation& loc)
{
    Ptr<Value> finalValidExpr = GetOuterMostExpr();
    if (finalValidExpr == nullptr) {
        auto allocSignal = CreateAndAppendExpression<Allocate>(loc,
            builder.GetType<RefType>(builder.GetInt64Ty()), builder.GetInt64Ty(), currentBlock);
        allocSignal->Set<GeneratedFromForIn>(true);
        delayExitSignal = allocSignal->GetResult();
        auto constZero =
            CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, 0UL)->GetResult();
        CreateWrappedStore(constZero, delayExitSignal, currentBlock);
    }
}

Ptr<Value> Translator::InitializeCondVar(const DebugLocation& loc)
{
    auto condVar = CreateAndAppendExpression<Allocate>(loc,
        builder.GetType<RefType>(builder.GetBoolTy()), builder.GetBoolTy(), currentBlock);
    condVar->Set<GeneratedFromForIn>(true);
    auto constantTrue =
        CreateAndAppendConstantExpression<BoolLiteral>(builder.GetBoolTy(), *currentBlock, static_cast<bool>(1));
    constantTrue->Set<GeneratedFromForIn>(true);
    CreateWrappedStore(loc, constantTrue->GetResult(), condVar->GetResult(), currentBlock);
    auto res = condVar->GetResult();
    return res;
}

Ptr<Value> Translator::GenerateForInRetValLocation(const DebugLocation& loc)
{
    // gc issue, gc team can not solve it, so here is a workAround.
    // CFG in chir is no problem, but there is a CFG path in llvm ir,
    // no store value operation to func return var in this path, but
    // still need to load return value from func return var.
    // the reason for setUp DelayExitReturnVal is listed in issue:
    // Cangjie-manifest/issues/2442
    Ptr<Value> funcRetValLocation = GetOuterBlockGroupReturnValLocation();
    if (funcRetValLocation != nullptr) {
        Ptr<Type> forInType = funcRetValLocation->GetType();
        CJC_ASSERT(forInType->IsRef());
        forInType = Cangjie::StaticCast<RefType*>(forInType)->GetBaseType();
        // store expression has NothingType var and all belowed chir will be eliminated by NothingTypeElimination pass,
        // it will result to an empty body of function.
        if (!forInType->IsNothing() && !forInType->IsUnit()) {
            auto forInRetValLocation =
                CreateAndAppendExpression<Allocate>(loc, builder.GetType<RefType>(forInType), forInType, currentBlock);
            forInRetValLocation->Set<GeneratedFromForIn>(true);
            Ptr<Value> forInBodyVal =
                CreateAndAppendConstantExpression<NullLiteral>(loc, forInType, *currentBlock)->GetResult();
            CreateWrappedStore(loc, forInBodyVal, forInRetValLocation->GetResult(), currentBlock);
            return forInRetValLocation->GetResult();
        }
    }
    return nullptr;
}

ForIn* Translator::InitForInExprSkeleton(const AST::ForInExpr& forInExpr, Ptr<Value>& inductiveVar, Ptr<Value>& condVar)
{
    auto forInloc = TranslateLocation(forInExpr);
    auto forInType = TranslateType(*forInExpr.ty);
    Func* parentFunc = currentBlock->GetTopLevelFunc();
    CJC_NULLPTR_CHECK(parentFunc);
    BlockGroup* bodyBlockGrp = builder.CreateBlockGroup(*parentFunc);
    BlockGroup* latchBlockGrp = builder.CreateBlockGroup(*parentFunc);
    BlockGroup* condBlockGrp = builder.CreateBlockGroup(*parentFunc);
    ForIn* forIn;
    if (forInExpr.forInKind == ForInKind::FORIN_ITER) {
        forIn = CreateAndAppendExpression<ForInIter>(
            forInloc, forInType, inductiveVar, condVar, currentBlock);
    } else {
        forIn = CreateAndAppendExpression<ForInRange>(
            forInloc, forInType, inductiveVar, condVar, currentBlock);
    }
    CJC_ASSERT(forIn);
    CJC_NULLPTR_CHECK(bodyBlockGrp);
    CJC_NULLPTR_CHECK(latchBlockGrp);
    CJC_NULLPTR_CHECK(condBlockGrp);
    forIn->InitBlockGroups(*bodyBlockGrp, *latchBlockGrp, *condBlockGrp);

    // 1. Initialize body blockGroup
    Ptr<Block> bodyEntry = builder.CreateBlock(bodyBlockGrp);
    bodyEntry->SetDebugLocation(TranslateLocation(*forInExpr.body));
    bodyBlockGrp->SetEntryBlock(bodyEntry);

    // 2. Initialize latch blockGroup
    Ptr<Block> latchEntry = builder.CreateBlock(latchBlockGrp);
    latchEntry->SetDebugLocation(inductiveVar->GetDebugLocation());
    latchBlockGrp->SetEntryBlock(latchEntry);

    // 3. Initialize cond blockGroup
    Ptr<Block> condEntry = builder.CreateBlock(condBlockGrp);
    condBlockGrp->SetEntryBlock(condEntry);
    condEntry->SetDebugLocation(TranslateLocation(*forInExpr.body));
    return forIn;
}

void Translator::TranslateForInBodyBlockGroup(const AST::ForInExpr& forInExpr)
{
    Ptr<Block> recordBlock = currentBlock;
    // Translate guard var
    Ptr<Value> guardVar = nullptr;
    if (forInExpr.patternGuard.get() != nullptr) {
        guardVar = TranslateExprArg(*forInExpr.patternGuard);
        recordBlock = currentBlock;
    }
    // Translate body of forIn expr
    TranslateSubExprToDiscarded(*forInExpr.body);
    auto bodyBlock = GetBlockByAST(*forInExpr.body);
    auto forInBodyLoc = TranslateLocation(*forInExpr.body);
    recordBlock->SetDebugLocation(forInBodyLoc);
    CreateAndAppendTerminator<Exit>(currentBlock);
    if (guardVar != nullptr) {
        // Create continue block for translate guard.
        Ptr<Block> continueBlock = CreateBlock();
        const auto& guardVarLoc = TranslateLocation(*forInExpr.patternGuard);
        CreateAndAppendTerminator<Exit>(guardVarLoc, continueBlock);
        CreateWrappedBranch(SourceExpr::FOR_IN_EXPR, guardVarLoc, guardVar, bodyBlock, continueBlock, recordBlock);
    } else {
        CreateAndAppendTerminator<GoTo>(forInBodyLoc, bodyBlock, recordBlock);
    }
}

void Translator::TranslateForInRangeLatchBlockGroup(const AST::Node& node)
{
    Ptr<Block> delayExitTrueBlock = CreateBlock();
    Ptr<Block> delayExitFalseBlock = CreateBlock();

    // if delayExitSignal value is greater than 0, it must be break or return happened.
    // Now we will go out for-in loop
    // Block #5: // preds:
    //   %50: Int64 = Load(%16)
    //   %51: Int64 = Constant(0)
    //   %52: Bool = GT(%50, %51)
    //   Branch(%52, #16, #17)
    auto loc = TranslateLocation(node);
    auto delayExitSignalVal =
        CreateAndAppendExpression<Load>(builder.GetInt64Ty(), delayExitSignal, currentBlock);
    delayExitSignalVal->Set<GeneratedFromForIn>(true);
    auto constZero = CreateAndAppendConstantExpression<IntLiteral>(
        builder.GetInt64Ty(), *currentBlock, 0UL)->GetResult();
    auto isNeedExit = CreateAndAppendExpression<BinaryExpression>(
        builder.GetBoolTy(), ExprKind::GT, delayExitSignalVal->GetResult(), constZero, currentBlock);
    isNeedExit->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    isNeedExit->Set<GeneratedFromForIn>(true);
    CreateAndAppendTerminator<Branch>(loc, isNeedExit->GetResult(),
        delayExitTrueBlock, delayExitFalseBlock, currentBlock)->SetSourceExpr(SourceExpr::FOR_IN_EXPR);

    // before go out for-in loop, we only exit() from latch blockGroup,
    // as we still need to enter the next CFG: cond blockGroup.
    // Block #16: // preds: #5
    //   Exit()
    currentBlock = delayExitTrueBlock;
    CreateAndAppendTerminator<Exit>(loc, currentBlock);

    // if delayExitSignal value is equal to 0, we translate latch as normal.
    // Block #17: // preds: #5
    //   try {
    //       i += 1
    //   } catch（_: Exception) {
    //        break;
    //   }
    currentBlock = delayExitFalseBlock;
    TranslateSubExprToDiscarded(node);
    CreateAndAppendTerminator<Exit>(loc, currentBlock);
}

void Translator::TranslateForInStringLatchBlockGroup(Ptr<Value>& inductiveVar)
{
    Ptr<Block> delayExitTrueBlock = CreateBlock();
    Ptr<Block> delayExitFalseBlock = CreateBlock();
    auto loc = inductiveVar->GetDebugLocation();

    auto delayExitSignalVal =
        CreateAndAppendExpression<Load>(builder.GetInt64Ty(), delayExitSignal, currentBlock);
    delayExitSignalVal->Set<GeneratedFromForIn>(true);
    auto constZero = CreateAndAppendConstantExpression<IntLiteral>(
        builder.GetInt64Ty(), *currentBlock, 0UL)->GetResult();
    auto isNeedExit = CreateAndAppendExpression<BinaryExpression>(
        builder.GetBoolTy(), ExprKind::GT, delayExitSignalVal->GetResult(), constZero, currentBlock);
    isNeedExit->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    isNeedExit->Set<GeneratedFromForIn>(true);
    CreateAndAppendTerminator<Branch>(loc, isNeedExit->GetResult(), delayExitTrueBlock,
        delayExitFalseBlock, currentBlock)->SetSourceExpr(SourceExpr::FOR_IN_EXPR);

    currentBlock = delayExitTrueBlock;
    CreateAndAppendTerminator<Exit>(loc, currentBlock);

    currentBlock = delayExitFalseBlock;
    Ptr<Value> inductiveVal = GetDerefedValue(inductiveVar);
    auto constOne =
        CreateAndAppendConstantExpression<IntLiteral>(inductiveVal->GetType(), *currentBlock, 1UL);
    constOne->Set<GeneratedFromForIn>(true);
    auto plusOne = CreateAndAppendExpression<BinaryExpression>(loc, inductiveVal->GetType(),
        ExprKind::ADD, inductiveVal, constOne->GetResult(), OverflowStrategy::WRAPPING, currentBlock);
    plusOne->Set<GeneratedFromForIn>(true);
    CreateWrappedStore(loc, plusOne->GetResult(), inductiveVar, currentBlock);
    CreateAndAppendTerminator<Exit>(loc, currentBlock);
}

void Translator::TranslateForInCondControlFlow(Ptr<Value>& condVar)
{
    Ptr<Block> delayExitTrueBlock = CreateBlock();
    Ptr<Block> delayExitFalseBlock = CreateBlock();

    // if delayExitSignal value is greater than 0, it must be break or return happened.
    // Now we will go out for-in loop
    // Block #6: // preds:
    //   %19: Int64 = Load(%16)
    //   %20: Int64 = Constant(0)
    //   %21: Bool = GT(%19, %20)
    //   Branch(%21, #7, #8)
    auto& loc = condVar->GetDebugLocation();
    auto delayExitSignalVal =
        CreateAndAppendExpression<Load>(builder.GetInt64Ty(), delayExitSignal, currentBlock);
    delayExitSignalVal->Set<GeneratedFromForIn>(true);
    auto constZero = CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, 0UL);
    constZero->Set<GeneratedFromForIn>(true);
    auto isNeedExit = CreateAndAppendExpression<BinaryExpression>(
        builder.GetBoolTy(), ExprKind::GT, delayExitSignalVal->GetResult(), constZero->GetResult(), currentBlock);
    isNeedExit->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    isNeedExit->Set<GeneratedFromForIn>(true);
    CreateAndAppendTerminator<Branch>(loc, isNeedExit->GetResult(), delayExitTrueBlock,
        delayExitFalseBlock, currentBlock)->SetSourceExpr(SourceExpr::FOR_IN_EXPR);

    // before go out for-in loop, let --delayExitSignalVal, and clear cond var to false
    // Block #7: // preds: #6
    //   %22: Int64 = Load(%16)
    //   %23: Int64 = Constant(1)
    //   %24: Int64 = Sub(%22, %23)
    //   %25: Unit = Store(%24, %16)
    //   %26: Bool = Constant(false)
    //   %27: Unit = Store(%26, %14)
    //   Exit()
    currentBlock = delayExitTrueBlock;
    delayExitSignalVal =
        CreateAndAppendExpression<Load>(builder.GetInt64Ty(), delayExitSignal, currentBlock);
    delayExitSignalVal->Set<GeneratedFromForIn>(true);
    auto constOne = CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, 1UL);
    constOne->Set<GeneratedFromForIn>(true);
    auto decreaseOne = CreateAndAppendExpression<BinaryExpression>(builder.GetInt64Ty(), ExprKind::SUB,
        delayExitSignalVal->GetResult(), constOne->GetResult(), OverflowStrategy::WRAPPING, currentBlock);
    decreaseOne->Set<GeneratedFromForIn>(true);
    CreateWrappedStore(decreaseOne->GetResult(), delayExitSignal, currentBlock);
    auto constantFalse =
        CreateAndAppendConstantExpression<BoolLiteral>(builder.GetBoolTy(), *currentBlock, false);
    constantFalse->Set<GeneratedFromForIn>(true);
    CreateWrappedStore(constantFalse->GetResult(), condVar, currentBlock);
    CreateAndAppendTerminator<Exit>(loc, currentBlock);

    // if delayExitSignal value is equal to 0, we translate condition as normal,
    // and update the conditon value to cond var.
    currentBlock = delayExitFalseBlock;
}

void Translator::UpdateDelayExitSignalInForInEnd(const ForIn& forIn)
{
    Ptr<Block> delayExitTrueBlock = CreateBlock();
    Ptr<Block> delayExitFalseBlock = CreateBlock();
    const auto& loc = forIn.GetDebugLocation();

    // if delayExitSignal value is greater than 0, it must be return happened.
    // Now we will exit() the func
    // Block #endBlock: // preds: #2
    //     %69: Int64 = Constant(0)
    //     %70: Int64 = Load(%16)
    //     %71: Bool = GT(%70, %69)
    //     Branch(%71, #26, #27)
    auto constZero = CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, 0UL);
    constZero->Set<GeneratedFromForIn>(true);
    auto delayExitSignalVal =
        CreateAndAppendExpression<Load>(builder.GetInt64Ty(), delayExitSignal, currentBlock);
    delayExitSignalVal->Set<GeneratedFromForIn>(true);
    auto isNeedExit = CreateAndAppendExpression<BinaryExpression>(
        builder.GetBoolTy(), ExprKind::GT, delayExitSignalVal->GetResult(), constZero->GetResult(), currentBlock);
    isNeedExit->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    isNeedExit->Set<GeneratedFromForIn>(true);
    CreateAndAppendTerminator<Branch>(loc, isNeedExit->GetResult(),
        delayExitTrueBlock, delayExitFalseBlock, currentBlock)->SetSourceExpr(SourceExpr::FOR_IN_EXPR);

    // before we exit() the func, let --delayExitSignalVal, and if the current forIn expr has forInRetValLocation,
    // we shold load forInRetVal from it, and restore forInRetVal to outer expr or func RetValLocation.
    // Block #26: // preds: #3
    //     %72: Int64 = Load(%16)
    //     %73: Int64 = Constant(1)
    //     %74: Int64 = Sub(%72, %73)
    //     %75: Unit = Store(%74, %16)
    //     %76: Int64 = Load(%7)
    //     %77: Unit = Store(%76, %2)
    //     Exit()
    currentBlock = delayExitTrueBlock;
    delayExitSignalVal =
        CreateAndAppendExpression<Load>(builder.GetInt64Ty(), delayExitSignal, currentBlock);
    delayExitSignalVal->Set<GeneratedFromForIn>(true);
    auto constOne = CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, 1UL);
    constOne->Set<GeneratedFromForIn>(true);
    auto decreaseOne = CreateAndAppendExpression<BinaryExpression>(builder.GetInt64Ty(), ExprKind::SUB,
        delayExitSignalVal->GetResult(), constOne->GetResult(), OverflowStrategy::WRAPPING, currentBlock);
    CreateWrappedStore(decreaseOne->GetResult(), delayExitSignal, currentBlock);
    Ptr<Value> funcRetValLocation = GetOuterBlockGroupReturnValLocation();
    if (funcRetValLocation != nullptr && forInExprReturnMap.count(forIn.GetResult()) != 0) {
        Ptr<Value> forInRetValLocation = forInExprReturnMap[forIn.GetResult()];
        Ptr<Value> forInRetVal = CreateAndAppendExpression<Load>(
            loc, Cangjie::StaticCast<RefType*>(forInRetValLocation->GetType())->GetBaseType(), forInRetValLocation,
            currentBlock)->GetResult();
        CreateWrappedStore(loc, forInRetVal, funcRetValLocation, currentBlock);
    }
    if (!tryCatchContext.empty()) {
        GenerateSignalCheckForThrow();
    }
    auto term = CreateAndAppendTerminator<Exit>(currentBlock);
    term->Set<SkipCheck>(SkipKind::SKIP_FORIN_EXIT);

    // if delayExitSignal value is equal to 0, we still need to translate the rest cangjie code as normal.
    currentBlock = delayExitFalseBlock;
}

void Translator::GenerateSignalCheckForThrow()
{
    auto constZero = CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, 0UL);
    constZero->Set<GeneratedFromForIn>(true);
    auto delayExitSignalVal =
        CreateAndAppendExpression<Load>(builder.GetInt64Ty(), delayExitSignal, currentBlock);
    delayExitSignalVal->Set<GeneratedFromForIn>(true);
    auto hasThrow = CreateAndAppendExpression<BinaryExpression>(
        builder.GetBoolTy(), ExprKind::GT, delayExitSignalVal->GetResult(), constZero->GetResult(), currentBlock);
    auto throwBB = CreateBlock(); // if throw check is true, rethrow it to outer Exception block
    auto exception = CreateAndAppendExpression<GetException>(builder.GetType<RefType>(builder.GetObjectTy()), throwBB);
    auto baseETy = builder.GetType<RefType>(builder.GetObjectTy());
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    IntrisicCallContext callContext{IntrinsicKind::BEGIN_CATCH, {exception->GetResult()}};
    auto eVal = CreateAndAppendExpression<Intrinsic>(baseETy, callContext, throwBB);
#else
    auto eVal = CreateAndAppendExpression<Intrinsic>(
        baseETy, IntrinsicKind::BEGIN_CATCH, std::vector<Value*>{exception->GetResult()}, throwBB);
#endif
    CreateAndAppendTerminator<RaiseException>(eVal->GetResult(), tryCatchContext.top(), throwBB);
    auto returnBB = CreateBlock(); // if throw check is false, return from cur func
    auto br = CreateAndAppendTerminator<Branch>(hasThrow->GetResult(), throwBB, returnBB, currentBlock);
    br->Set<GeneratedFromForIn>(true);
    currentBlock = returnBB; // contents of returnBB is filled on the caller side
}

Ptr<Value> Translator::TranslateForInIterCondition(Ptr<Value>& iterNextLocation, Ptr<AST::Ty>& astTy)
{
    // Block #8: // preds: #6
    //   %51: Enum-_CNat6OptionIRNat6StringEE<Struct-Nat6StringE> = Load(%34)
    //   %52: Bool = Field(%51, 0)
    //   %53: Bool = Not(%52)
    //   %54: Unit = Store(%53, %37)
    //   Exit()
    // Field(%xx, 0)
    auto enumIdx = GetEnumIDValue(astTy, GetDerefedValue(iterNextLocation, iterNextLocation->GetDebugLocation()));
    // bool == 0 euqals !bool
    return CreateAndAppendExpression<UnaryExpression>(iterNextLocation->GetDebugLocation(),
        builder.GetBoolTy(), ExprKind::NOT, enumIdx, Cangjie::OverflowStrategy::NA, currentBlock)->GetResult();
}

void Translator::TranslateForInIterPattern(const AST::ForInExpr& forInExpr, Ptr<Value>& iterNextLocation)
{
    // Translate forInExpr.pattern of for-in node
    // { // Block Group: 1
    // Block #4: // preds:
    //   %56: Enum-_CNat6OptionIRNat6StringEE<Struct-_CNat6StringE> = Load(%34)
    //   %57: Tuple<UInt64, Struct-_CNat6StringE> = TypeCast(%56, Tuple<UInt64, Struct-_CNat6StringE>)
    //   %58: Struct-_CNat6StringE = Field(%57, 1)
    //   %59: Struct-_CNat6StringE& = Allocate(Struct-_CNat6StringE)
    //   %60: Unit = Debug(%59, i)
    //   %61: Unit = Store(%58, %59)
    //   %62: Struct-_CNat6StringE = Load(%59)
    //   ......
    // }
    auto loadIterNext = GetDerefedValue(iterNextLocation, iterNextLocation->GetDebugLocation());
    CJC_ASSERT(loadIterNext->GetType()->IsEnum());
    CJC_ASSERT(forInExpr.pattern->astKind == AST::ASTKind::ENUM_PATTERN);
    auto enumPattern = StaticCast<const AST::EnumPattern*>(forInExpr.pattern.get());
    HandleVarWithTupleAndEnumPattern(*enumPattern, enumPattern->patterns, loadIterNext, true);
}

void Translator::TranslateForInIterLatchBlockGroup(
    const MatchExpr& matchExpr, Ptr<Value>& iterNextLocation)
{
    Ptr<Block> delayExitTrueBlock = CreateBlock();
    Ptr<Block> delayExitFalseBlock = CreateBlock();

    // if delayExitSignal value is greater than 0, it must be break or return happened.
    // Now we will go out for-in loop
    // Block #5: // preds:
    //   %78: Int64 = Load(%39)
    //   %79: Int64 = Constant(0)
    //   %80: Bool = GT(%78, %79)
    //   Branch(%80, #14, #15)
    auto& loc = iterNextLocation->GetDebugLocation();
    auto delayExitSignalVal =
        CreateAndAppendExpression<Load>(builder.GetInt64Ty(), delayExitSignal, currentBlock);
    delayExitSignalVal->Set<GeneratedFromForIn>(true);
    auto constZero = CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, 0UL);
    auto isNeedExit = CreateAndAppendExpression<BinaryExpression>(
        builder.GetBoolTy(), ExprKind::GT, delayExitSignalVal->GetResult(), constZero->GetResult(), currentBlock);
    isNeedExit->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    isNeedExit->Set<GeneratedFromForIn>(true);
    CreateAndAppendTerminator<Branch>(loc, isNeedExit->GetResult(), delayExitTrueBlock,
        delayExitFalseBlock, currentBlock)->SetSourceExpr(SourceExpr::FOR_IN_EXPR);

    // before go out for-in loop, we only exit() from latch blockGroup,
    // as we still need to enter the next CFG: cond blockGroup.
    // Block #14: // preds: #5
    //   Exit()
    currentBlock = delayExitTrueBlock;
    CreateAndAppendTerminator<Exit>(loc, currentBlock);

    // if delayExitSignal value is equal to 0, we translate latch as normal.
    // Block #15: // preds: #5
    //   %81: Enum-_CNat6OptionIRNat6StringEE<Struct-_CNat6StringE> =
    //   Invoke(next: (Class-_CNat8IteratorIRNat6StringEE<Struct-_CNat6StringE>&), %33)
    //   %82: Unit = Store(%81, %34)
    //   Exit()
    currentBlock = delayExitFalseBlock;
    Ptr<Value> iterNext = TranslateExprArg(*matchExpr.selector);
    CreateWrappedStore(loc, iterNext, iterNextLocation, currentBlock);
    CreateAndAppendTerminator<Exit>(loc, currentBlock);
}

Ptr<Value> Translator::TranslateForInRange(const AST::ForInExpr& forInExpr)
{
    AST::Block* inExpression = StaticCast<AST::Block>(forInExpr.inExpression.get());
    auto forInExprLoc = TranslateLocation(forInExpr);
    Ptr<Value> forInRetValLocation = GenerateForInRetValLocation(forInExprLoc);
    // this map is used for check if jump expr is inside try catch scope.
    forInExprAST2DebugLocMap[&forInExpr] = &forInExprLoc;
    // there are four nodes in forInExpr.inExpression:
    // nodes[0] is inductive var,
    // nodes[1] is stopExpr var,
    // nodes[2] is step update
    // nodes[3] is condition var,
    // 1. Translate inductive var of for-in node
    Ptr<Value> inductiveVar = TranslateExprArg(*inExpression->body[0]);
    StaticCast<LocalVar>(inductiveVar)->GetExpr()->Set<GeneratedFromForIn>(true);
    // 2. Translate stopExpr var
    Ptr<Value> stopExprVar = TranslateExprArg(*inExpression->body[1]);
    if (auto stopExpr = DynamicCast<LocalVar>(stopExprVar)) {
        stopExpr->GetExpr()->Set<GeneratedFromForIn>(true);
    }
    // 3. Translate condition
    auto condLoc = TranslateLocation(*inExpression->body[3]);
    Ptr<Value> condition = TranslateExprArg(*inExpression->body[3u]);
    StaticCast<LocalVar>(condition)->GetExpr()->Set<GeneratedFromForIn>(true);
    auto recordBlock = currentBlock;
    // Create forInBody block.
    Ptr<Block> forInBodyBlock = CreateBlock();
    forInBodyBlock->SetDebugLocation(TranslateLocation(forInExpr.body->begin, forInExpr.body->end));
    currentBlock = forInBodyBlock;
    // 4. initialize cond var
    Ptr<Value> condVar = InitializeCondVar(condLoc);
    // 5. initialize delayExitSig
    InitializeDelayExitSignal(condLoc);

    // 6. Generate for-in expression.
    //  ForIn(%10, %13, {bodyBlockGroup}, {latchBlockGroup}, {condBlockGroup})
    ForIn* forIn = InitForInExprSkeleton(forInExpr, inductiveVar, condVar);
    if (forInRetValLocation != nullptr) {
        forInExprReturnMap.insert(std::make_pair(forIn->GetResult(), forInRetValLocation));
    }

    // 7. Translate body blockGroup
    blockGroupStack.emplace_back(forIn->GetBody());
    currentBlock = forIn->GetBody()->GetEntryBlock();
    ScopeContext contextInExpression(*this);
    contextInExpression.ScopePlus();
    // Translate forInExpr.pattern
    if (auto vp = DynamicCast<VarPattern*>(forInExpr.pattern.get())) {
        TranslateExprArg(*vp->varDecl);
    } else {
        CJC_ASSERT(forInExpr.pattern->astKind == AST::ASTKind::WILDCARD_PATTERN);
    }
    ScopeContext contextBody(*this);
    contextBody.ScopePlus();
    TranslateForInBodyBlockGroup(forInExpr);
    blockGroupStack.pop_back();

    // 8. Translate latch blockGroup
    blockGroupStack.emplace_back(forIn->GetLatch());
    currentBlock = forIn->GetLatch()->GetEntryBlock();
    TranslateForInRangeLatchBlockGroup(*inExpression->body[2u]);
    blockGroupStack.pop_back();

    // 9. Translate cond blockGroup
    blockGroupStack.emplace_back(forIn->GetCond());
    currentBlock = forIn->GetCond()->GetEntryBlock();
    ScopeContext context(*this);
    context.ScopePlus();
    LocalVar* localConditionVal = StaticCast<LocalVar>(condition);
    auto conditionExprKind = localConditionVal->GetExpr()->GetExprKind();
    TranslateForInCondControlFlow(condVar);
    auto conditionInCondBG = CreateAndAppendExpression<BinaryExpression>(condLoc,
        builder.GetBoolTy(), conditionExprKind, GetDerefedValue(inductiveVar, condLoc), GetDerefedValue(stopExprVar),
        currentBlock)->GetResult();
    CreateWrappedStore(condLoc, conditionInCondBG, condVar, currentBlock);
    CreateAndAppendTerminator<Exit>(condLoc, currentBlock);
    blockGroupStack.pop_back();

    // Create end branch
    Ptr<Block> endBlock = CreateBlock();
    //  GoTo #endBlock
    CreateAndAppendTerminator<GoTo>(condLoc, endBlock, forInBodyBlock);

    // 10. Update DelayExitSignal
    currentBlock = endBlock;
    UpdateDelayExitSignalInForInEnd(*forIn);
    auto br = CreateAndAppendTerminator<Branch>(forInExprLoc, condition, forInBodyBlock, currentBlock, recordBlock);
    br->SetSourceExpr(SourceExpr::FOR_IN_EXPR);
    // when the range is made of constants (i.e. value can be computed by CA), this branch generates an unreachable
    // warning on the false branch.
    currentBlock->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    return nullptr;
}

Ptr<Value> Translator::TranslateForInString(const AST::ForInExpr& forInExpr)
{
    auto forInExprBlock = currentBlock;
    auto forInExprLoc = TranslateLocation(forInExpr);
    Ptr<Value> forInRetValLocation = GenerateForInRetValLocation(forInExprLoc);
    // this map is used for check if jump expr is inside try catch scope.
    forInExprAST2DebugLocMap[&forInExpr] = &forInExprLoc;

    // 1. Translate inExpression of for-in node
    TranslateExprArg(*forInExpr.inExpression);
    AST::Block* inExpression = StaticCast<AST::Block>(forInExpr.inExpression.get());
    auto inExpressionBlock = GetBlockByAST(*inExpression);
    CreateAndAppendTerminator<GoTo>(forInExprLoc, inExpressionBlock, forInExprBlock);

    // 2. Translate inductive var of inExpression
    // there are three nodes in forInExpr.inExpression, nodes[0] is inductive var
    Ptr<Value> inductiveVar = GetSymbolTable(*inExpression->body[0]);
    StaticCast<LocalVar>(inductiveVar)->GetExpr()->Set<GeneratedFromForIn>(true);

    // 3. Translate loop condition var of for-in node
    // there are three nodes in forInExpr.inExpression, nodes[2] is sizeget func
    auto condLoc = TranslateLocation(*inExpression->body[2U]);
    auto stringSize = GetDerefedValue(GetSymbolTable(*inExpression->body[2U]), condLoc);
    auto condition = CreateAndAppendExpression<BinaryExpression>(condLoc, builder.GetBoolTy(),
        ExprKind::LT, GetDerefedValue(inductiveVar, condLoc), stringSize, currentBlock)->GetResult();
    condition->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    condition->GetExpr()->Set<GeneratedFromForIn>(true);
    auto recordBlock = currentBlock;
    // Create forInBody block.
    Ptr<Block> forInBodyBlock = CreateBlock();
    forInBodyBlock->SetDebugLocation(TranslateLocation(*forInExpr.body));
    currentBlock = forInBodyBlock;
    // 4. initialize cond var
    Ptr<Value> condVar = InitializeCondVar(condLoc);
    // 5. initialize delayExitSig
    InitializeDelayExitSignal(condLoc);

    // 6. Generate for-in expression.
    //  ForIn(%33, %37, {bodyBlockGroup}, {latchBlockGroup}, {condBlockGroup})
    ForIn* forIn = InitForInExprSkeleton(forInExpr, inductiveVar, condVar);
    if (forInRetValLocation != nullptr) {
        forInExprReturnMap.insert(std::make_pair(forIn->GetResult(), forInRetValLocation));
    }

    // 7. Translate body blockGroup
    blockGroupStack.emplace_back(forIn->GetBody());
    currentBlock = forIn->GetBody()->GetEntryBlock();
    ScopeContext contextInExpression(*this);
    contextInExpression.ScopePlus();
    // Translate forInExpr.pattern of for-in node
    if (auto vp = DynamicCast<VarPattern*>(forInExpr.pattern.get()); vp) {
        TranslateSubExprToDiscarded(*vp->varDecl);
    }
    ScopeContext contextBody(*this);
    contextBody.ScopePlus();
    TranslateForInBodyBlockGroup(forInExpr);
    blockGroupStack.pop_back();

    // 8. Translate latch blockGroup
    blockGroupStack.emplace_back(forIn->GetLatch());
    currentBlock = forIn->GetLatch()->GetEntryBlock();
    TranslateForInStringLatchBlockGroup(inductiveVar);
    blockGroupStack.pop_back();

    // 9. Translate cond blockGroup
    blockGroupStack.emplace_back(forIn->GetCond());
    currentBlock = forIn->GetCond()->GetEntryBlock();
    ScopeContext context(*this);
    context.ScopePlus();
    TranslateForInCondControlFlow(condVar);
    auto conditionInCondBG = CreateAndAppendExpression<BinaryExpression>(
        condLoc, builder.GetBoolTy(), ExprKind::LT, GetDerefedValue(inductiveVar, condLoc), stringSize, currentBlock)
                                 ->GetResult();
    CreateWrappedStore(condLoc, conditionInCondBG, condVar, currentBlock);
    CreateAndAppendTerminator<Exit>(condLoc, currentBlock);
    blockGroupStack.pop_back();

    // Create end branch
    Ptr<Block> endBlock = CreateBlock();
    //  GoTo #endBlock
    CreateAndAppendTerminator<GoTo>(condLoc, endBlock, forInBodyBlock);

    // 10. Update DelayExitLevel
    currentBlock = endBlock;
    UpdateDelayExitSignalInForInEnd(*forIn);
    auto br = CreateAndAppendTerminator<Branch>(forInExprLoc, condition, forInBodyBlock, currentBlock, recordBlock);
    br->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    br->SetSourceExpr(SourceExpr::FOR_IN_EXPR);
    return nullptr;
}

Ptr<Value> Translator::MakeNone(Type& optionType, const DebugLocation& loc)
{
    auto one = CreateAndAppendConstantExpression<BoolLiteral>(builder.GetBoolTy(), *currentBlock, true);
    one->Set<GeneratedFromForIn>(true);
    auto enumTuple = CreateAndAppendExpression<Tuple>(
        loc, &optionType, std::vector<Value*>{one->GetResult()}, currentBlock);
    enumTuple->Set<GeneratedFromForIn>(true);
    return enumTuple->GetResult();
}

Ptr<Value> Translator::TranslateForInIter(const AST::ForInExpr& forInExpr)
{
    AST::Block* inExpression = StaticCast<AST::Block>(forInExpr.inExpression.get());
    auto forInExprLoc = TranslateLocation(forInExpr);
    Ptr<Value> forInRetValLocation = GenerateForInRetValLocation(forInExprLoc);
    // this map is used for check if jump expr is inside try catch scope.
    forInExprAST2DebugLocMap[&forInExpr] = &forInExprLoc;

    // 1. Translate inductive var
    // there are two nodes in forInExpr.inExpression, nodes[0] is inductive var
    Ptr<Value> inductiveVar = TranslateSubExprAsValue(*inExpression->body[0]);

    // 2. Translate iterNext var
    // nodes[1] is match expr, it triggers iterator next operation
    AST::MatchExpr* matchExpr = StaticCast<AST::MatchExpr>(inExpression->body[1].get());
    auto matchTy = TranslateType(*matchExpr->ty);
    auto condLoc = TranslateLocation(*inExpression->body[1]);
    Ptr<Value> iterNextLocation = CreateAndAppendExpression<Allocate>(condLoc,
        builder.GetType<RefType>(matchTy), matchTy, currentBlock)->GetResult();
    Ptr<Value> iterInit = MakeNone(*matchTy, condLoc);
    CreateWrappedStore(condLoc, iterInit, iterNextLocation, currentBlock);
    SetSkipPrintWarning(iterInit);

    // 3. Translate condition var
    auto recordBlock = currentBlock;
    // Create forInBody block.
    Ptr<Block> forInBodyBlock = CreateBlock();
    forInBodyBlock->SetDebugLocation(TranslateLocation(*forInExpr.body));
    currentBlock = forInBodyBlock;
    // 4. initialize cond var
    Ptr<Value> condVar = InitializeCondVar(condLoc);
    // 5. initialize delayExitSig
    InitializeDelayExitSignal(condLoc);

    // 6. Generate for-in expression.
    //  ForIn(%33, %37, {bodyBlockGroup}, {latchBlockGroup}, {condBlockGroup})
    ForIn* forIn = InitForInExprSkeleton(forInExpr, inductiveVar, condVar);
    if (forInRetValLocation != nullptr) {
        forInExprReturnMap.insert(std::make_pair(forIn->GetResult(), forInRetValLocation));
    }

    // 7. Translate body blockGroup
    blockGroupStack.emplace_back(forIn->GetBody());
    currentBlock = forIn->GetBody()->GetEntryBlock();
    ScopeContext contextIterPattern(*this);
    contextIterPattern.ScopePlus();
    contextIterPattern.ScopePlus();
    TranslateForInIterPattern(forInExpr, iterNextLocation);
    ScopeContext contextBody(*this);
    contextBody.ScopePlus();
    TranslateForInBodyBlockGroup(forInExpr);
    blockGroupStack.pop_back();

    // 8. Translate latch blockGroup
    blockGroupStack.emplace_back(forIn->GetLatch());
    currentBlock = forIn->GetLatch()->GetEntryBlock();
    TranslateForInIterLatchBlockGroup(*matchExpr, iterNextLocation);
    blockGroupStack.pop_back();

    // 9. Translate cond blockGroup
    blockGroupStack.emplace_back(forIn->GetCond());
    currentBlock = forIn->GetCond()->GetEntryBlock();
    ScopeContext context(*this);
    context.ScopePlus();
    TranslateForInCondControlFlow(condVar);
    auto conditionInCondBG = TranslateForInIterCondition(iterNextLocation, matchExpr->ty);
    CreateWrappedStore(condLoc, conditionInCondBG, condVar, currentBlock);
    CreateAndAppendTerminator<Exit>(condLoc, currentBlock);
    blockGroupStack.pop_back();

    // Create end branch
    Ptr<Block> endBlock = CreateBlock();
    //  GoTo #endBlock
    CreateAndAppendTerminator<GoTo>(condLoc, endBlock, forInBodyBlock);

    // 10. Update DelayExitLevel
    currentBlock = endBlock;
    UpdateDelayExitSignalInForInEnd(*forIn);
    CreateAndAppendTerminator<GoTo>(forInExprLoc, forInBodyBlock, recordBlock);
    return nullptr;
}

/*
for (i in a..=b:1 where guard) { body }

translates into:

if a <= b {
    var iter = a
    var cond = true
    ForInClosedRange(iter, cond) {
        #body:
            let i = Load(iter)
            if guard {
                body
            }
            GoTo(#cond)
        #cond:
            // cond is true here
            cond = i != b
                // != instead of <= is where the optimisation goes
            GoTo(#latch)
        #latch:
            iter = i + 1
            GoTo(#body)
    }
}
()
*/
class TranslateForInExpr {
public:
    explicit TranslateForInExpr(Translator& translator) : tr{translator} {}

    Value* Translate(const ForInExpr& forinExpr)
    {
        forin = &forinExpr;
        range = forinExpr.inExpression.get();
        auto forinLoc = tr.TranslateLocation(forinExpr);
        tr.forInExprAST2DebugLocMap[forin] = &forinLoc;
        auto iterBegin = TranslateBegin();
        auto iterEnd = TranslateEnd();
        auto fb = TranslatePreCondition(*iterBegin, *iterEnd);
        iterVar = TranslateIterVar(*iterBegin);

        auto curBlock = tr.GetCurrentBlock();
        auto bodyBlock = CreateForInEntryBlock();
        tr.CreateAndAppendTerminator<GoTo>(bodyBlock, curBlock);
        auto condLoc = tr.TranslateLocation(*forinExpr.inExpression);
        condVar = tr.InitializeCondVar(condLoc);
        tr.InitializeDelayExitSignal(condLoc);
        auto retVal = tr.GenerateForInRetValLocation(forinLoc);
        auto [body, cond, latch] = CreateRetAndBGs();
        if (retVal) {
            tr.forInExprReturnMap.emplace(res->GetResult(), retVal);
        }

        TranslateBodyBG(*body);
        TranslateCondBG(*cond, *iterEnd);
        TranslateLatchBG(*latch);

        auto endBlock = tr.CreateBlock();
        tr.CreateAndAppendTerminator<GoTo>(endBlock, bodyBlock);
        tr.SetCurrentBlock(*endBlock);
        tr.UpdateDelayExitSignalInForInEnd(*res);

        tr.CreateAndAppendTerminator<GoTo>(fb, tr.GetCurrentBlock());
        tr.SetCurrentBlock(*fb);
        return nullptr;
    }

protected:
    Translator& tr;
    const ForInExpr* forin{nullptr};
    Expr* range{nullptr};
    Ptr<ForIn> res;
    Ptr<Value> iterVar;
    Ptr<Value> iterValue; // the value of loaded iterVar.
    Ptr<Value> condVar;

    Ptr<LocalVar> TranslateIterVar(Value& iterBegin)
    {
        auto loc = tr.TranslateLocation(*forin->pattern);
        auto intType = iterBegin.GetType();
        auto iterType = tr.builder.GetType<RefType>(intType);
        auto res1 = tr.CreateAndAppendExpression<Allocate>(loc, iterType, intType, tr.GetCurrentBlock());
        res1->Set<GeneratedFromForIn>(true);
        tr.CreateWrappedStore(loc, &iterBegin, res1->GetResult(), tr.GetCurrentBlock());
        return res1->GetResult();
    }

    Value* TranslateBegin()
    {
        auto& rangeBegin = Is<CallExpr>(range) ? *DynamicCast<CallExpr>(range)->args[0]->expr : range->desugarExpr ?
            *StaticCast<CallExpr>(*range->desugarExpr).args[0]->expr : *StaticCast<RangeExpr>(range)->startExpr;
        auto value = Translator::TranslateASTNode(rangeBegin, tr);
        return tr.GetDerefedValue(value, value->GetDebugLocation());
    }
    Value* TranslateEnd()
    {
        auto& rangeEnd = Is<CallExpr>(range) ? *DynamicCast<CallExpr>(range)->args[1]->expr : range->desugarExpr ?
            *StaticCast<CallExpr>(*range->desugarExpr).args[1]->expr : *StaticCast<RangeExpr>(range)->stopExpr;
        auto value = Translator::TranslateASTNode(rangeEnd, tr);
        return tr.GetDerefedValue(value, value->GetDebugLocation());
    }

    /// Create body block and set currentBlock to this
    Block* CreateForInEntryBlock()
    {
        auto bl = tr.CreateBlock();
        tr.SetCurrentBlock(*bl);
        return bl;
    }

    struct ForinBGs {
        BlockGroup* body;
        BlockGroup* cond;
        BlockGroup* latch;
    };

    ForinBGs CreateRetAndBGs()
    {
        Func* parentFunc = tr.currentBlock->GetTopLevelFunc();
        CJC_NULLPTR_CHECK(parentFunc);
        BlockGroup* bodyBlockGrp = tr.builder.CreateBlockGroup(*parentFunc);
        BlockGroup* condBlockGrp = tr.builder.CreateBlockGroup(*parentFunc);
        BlockGroup* latchBlockGrp = tr.builder.CreateBlockGroup(*parentFunc);
        ForinBGs ret{bodyBlockGrp, condBlockGrp, latchBlockGrp};
        
        res = CreateRes(ret);

        Ptr<Block> bodyEntry = tr.builder.CreateBlock(bodyBlockGrp);
        bodyBlockGrp->SetEntryBlock(bodyEntry);

        Ptr<Block> condEntry = tr.builder.CreateBlock(condBlockGrp);
        condBlockGrp->SetEntryBlock(condEntry);
        condEntry->Set<GeneratedFromForIn>(true);

        Ptr<Block> latchEntry = tr.builder.CreateBlock(latchBlockGrp);
        latchBlockGrp->SetEntryBlock(latchEntry);
        return ret;
    }

    ForIn* CreateRes(const ForinBGs& bgs)
    {
        auto loc = tr.TranslateLocation(*forin);
        auto forInClosedRange = tr.CreateAndAppendExpression<ForInClosedRange>(
            loc, tr.TranslateType(*forin->ty), iterVar, condVar, tr.GetCurrentBlock());
        CJC_NULLPTR_CHECK(forInClosedRange);
        CJC_NULLPTR_CHECK(bgs.body);
        CJC_NULLPTR_CHECK(bgs.latch);
        CJC_NULLPTR_CHECK(bgs.cond);
        forInClosedRange->InitBlockGroups(*bgs.body, *bgs.latch, *bgs.cond);
        return forInClosedRange;
    }

    void TranslateBodyBG(BlockGroup& body)
    {
        tr.blockGroupStack.emplace_back(&body);
        tr.SetCurrentBlock(*body.GetEntryBlock());
        Translator::ScopeContext contextInExpression(tr);
        contextInExpression.ScopePlus();
        // let i = Load(iter)
        // use member var iterValue to ensure iterVar is loaded only once per loop
        iterValue = tr.CreateAndAppendExpression<Load>(
            tr.TranslateLocation(*forin->pattern),
            StaticCast<RefType>(iterVar->GetType())->GetBaseType(), iterVar, tr.GetCurrentBlock())->GetResult();
        if (auto vp = DynamicCast<VarPattern>(forin->pattern.get())) {
            auto& var = *vp->varDecl;
            tr.SetSymbolTable(var, *iterValue);
        }
        // otherwise the pattern is wildcard. In this case, the value of iterVar need not load.
        Translator::ScopeContext contextBody(tr);
        contextBody.ScopePlus();
        tr.TranslateForInBodyBlockGroup(*forin);
        if (auto vp = DynamicCast<VarPattern>(forin->pattern.get())) {
            tr.localValSymbolTable.Erase(*vp->varDecl);
        }
        tr.blockGroupStack.pop_back();
    }

    void TranslateCondBG(BlockGroup& cond, Value& iterEnd)
    {
        tr.blockGroupStack.emplace_back(&cond);
        tr.SetCurrentBlock(*cond.GetEntryBlock());
        Translator::ScopeContext context(tr);
        context.ScopePlus();
        tr.TranslateForInCondControlFlow(condVar);
        // cond = i != b
        auto& iterVarLoc = iterVar->GetDebugLocation();
        auto conditionInCondBG = tr.CreateAndAppendExpression<BinaryExpression>(iterVarLoc, tr.builder.GetBoolTy(),
            ExprKind::NOTEQUAL, iterValue, &iterEnd, tr.GetCurrentBlock())->GetResult();
        tr.CreateWrappedStore(iterVarLoc, conditionInCondBG, condVar, tr.GetCurrentBlock());
        tr.CreateAndAppendTerminator<Exit>(tr.GetCurrentBlock());
        tr.blockGroupStack.pop_back();
    }

    void TranslateLatchBG(BlockGroup& latch)
    {
        tr.blockGroupStack.emplace_back(&latch);
        tr.SetCurrentBlock(*latch.GetEntryBlock());
        // iter = iter + 1
        DelayExitEquals0(&TranslateForInExpr::IncrementIterVarIfCond);
        tr.blockGroupStack.pop_back();
    }

    /// Check whether delayExitSignal equals 0, and create two blocks following the check. Do the usual logic in the
    /// true block, while the false block just exit from the current block group.
    /// \param b whatever you want to translate in the true block.
    void DelayExitEquals0(void (TranslateForInExpr::*b)())
    {
        Ptr<Block> delayExitTrueBlock = tr.CreateBlock();
        Ptr<Block> delayExitFalseBlock = tr.CreateBlock();
        auto delayExitValue = tr.CreateAndAppendExpression<Load>(tr.builder.GetInt64Ty(), tr.delayExitSignal,
            tr.GetCurrentBlock())->GetResult();
        auto constZero = tr.CreateAndAppendConstantExpression<IntLiteral>(tr.builder.GetInt64Ty(),
            *tr.GetCurrentBlock(), 0UL)->GetResult();
        auto needsExit = tr.CreateAndAppendExpression<BinaryExpression>(tr.builder.GetBoolTy(),
            ExprKind::EQUAL, delayExitValue, constZero, tr.GetCurrentBlock())->GetResult();
        auto br = CreateBranch(needsExit, delayExitTrueBlock, delayExitFalseBlock, tr.GetCurrentBlock());
        br->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);

        tr.SetCurrentBlock(*delayExitTrueBlock);
        (this->*b)();
        tr.CreateAndAppendTerminator<Exit>(tr.GetCurrentBlock());

        tr.SetCurrentBlock(*delayExitFalseBlock);
        auto exit = tr.CreateAndAppendTerminator<Exit>(tr.GetCurrentBlock());
        exit->Set<SkipCheck>(SkipKind::SKIP_FORIN_EXIT);
    }

    /// True if rangeExpr is of step 1. False if -1. No other cases exist.
    bool IsIncrement()
    {
        if (auto call = DynamicCast<CallExpr>(range)) {
            return call->args[2u]->expr->constNumValue.asInt.Int64() == 1;
        }
        if (range->desugarExpr) {
            return StaticCast<CallExpr>(*range->desugarExpr).args[2u]->expr->constNumValue.asInt.Int64() == 1;
        }
        return StaticCast<RangeExpr>(range)->stepExpr->constNumValue.asInt.Int64() == 1;
    }

    void IncrementIterVarIfCond()
    {
        auto& iterLoc = iterVar->GetDebugLocation();
        // if cond {
        //     iter = i ± 1
        // }
        auto ifCond = tr.CreateAndAppendExpression<Load>(
            iterLoc, tr.builder.GetBoolTy(), condVar, tr.GetCurrentBlock())->GetResult();
        auto tb = tr.CreateBlock();
        auto fb = tr.CreateBlock();
        auto br = CreateBranch(ifCond, tb, fb, tr.GetCurrentBlock());
        // this cond is computed from iterValue != iterEnd, which is false after CP if the close range is x..=x.
        // in this case the unreachable warning is unnecessary because this check is always copied into two, and the
        // first one (which is iterBegin <= iterEnd) returns true.
        tb->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        br->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        tr.SetCurrentBlock(*tb);
        auto type = StaticCast<RefType>(iterVar->GetType())->GetBaseType();
        auto one = tr.CreateAndAppendConstantExpression<IntLiteral>(type, *tr.GetCurrentBlock(), 1UL)->GetResult();
        // this add is guaranteed to be never throwing
        auto newValue = tr.CreateAndAppendExpression<BinaryExpression>(
            iterLoc, type, IsIncrement() ? ExprKind::ADD : ExprKind::SUB,
            iterValue, one, OverflowStrategy::WRAPPING, tr.GetCurrentBlock())->GetResult();
        tr.CreateWrappedStore(iterLoc, newValue, iterVar, tr.GetCurrentBlock());
        tr.CreateAndAppendTerminator<GoTo>(fb, tb);
        tr.SetCurrentBlock(*fb);
    }

    /// things to be translated before allocating condVar/iterVar. This differs among forin expression kinds.
    Block* TranslatePreCondition(Value& iterBegin, Value& iterEnd)
    {
        auto loc = tr.TranslateLocation(*range);
        auto le = tr.CreateAndAppendExpression<BinaryExpression>(loc, tr.builder.GetBoolTy(),
            IsIncrement() ? ExprKind::LE : ExprKind::GE, &iterBegin,
            &iterEnd, tr.GetCurrentBlock())->GetResult();
        auto tb = tr.CreateBlock();
        auto fb = tr.CreateBlock();
        // when the forin is guaranteed to be terminated inside the body block (e.g. through unconditional
        // break/return), the false block is unreachable. Do not issue a diag because the forin block is reachable
        fb->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        CreateBranch(le, tb, fb, tr.GetCurrentBlock());
        tr.SetCurrentBlock(*tb);
        return fb;
    }

    template <class... Args>
    Branch* CreateBranch(Args... args)
    {
        auto br = tr.CreateAndAppendTerminator<Branch>(args...);
        br->SetSourceExpr(SourceExpr::FOR_IN_EXPR);
        return br;
    }
};

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
Ptr<Value> TranslateForInClosedRangeOne(Translator& tr, const ForInExpr& forInExpr)
{
    TranslateForInExpr impl{tr};
    return impl.Translate(forInExpr);
}
#endif
}
