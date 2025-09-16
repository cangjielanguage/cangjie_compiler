// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

/**
 * Type of try expression may be type of expected target,
 * we need to check try body and all catch blocks for real nothing typed control flow.
 */
static inline bool HasTypeOfNothing(const AST::TryExpr& tryExpr)
{
    if (!tryExpr.tryBlock->ty->IsNothing()) {
        return false;
    }
    return std::all_of(
        tryExpr.catchBlocks.cbegin(), tryExpr.catchBlocks.cend(), [](auto& it) { return it->ty->IsNothing(); });
}

Ptr<Value> Translator::Visit(const AST::TryExpr& tryExpr)
{
    // Try must have at least one catch block or finally block.
    CJC_ASSERT(!tryExpr.catchBlocks.empty() || tryExpr.finallyBlock != nullptr);
    auto tryTy = TranslateType(*tryExpr.ty);
    auto loc = TranslateLocation(tryExpr.begin, tryExpr.end);
    auto retVal = tryTy->IsUnit() || tryTy->IsNothing() ? nullptr :
        CreateAndAppendExpression<Allocate>(loc, builder.GetType<RefType>(tryTy), tryTy, currentBlock)->GetResult();

    bool hasFinally = tryExpr.finallyBlock != nullptr;
    Ptr<Block> endBlock = CreateBlock();
    // Used for checking scope info with control flow. NOTE: record scope info before update the 'ScopeContext'
    endBlock->SetDebugLocation(loc);
    CreateFinallyExpr(hasFinally, endBlock);
    ScopeContext context(*this);
    context.ScopePlus();
    tryBodyContext.emplace_back(&tryExpr);
    // 1. Set context for try body & generate try.
    Ptr<Block> exceptionBlock = CreateBlock();
    {
        tryCatchContext.emplace(exceptionBlock);
        if (tryExpr.TestAttr(AST::Attribute::MACRO_INVOKE_BODY)) {
            auto pkgInit = builder.GetCurPackage()->GetPackageInitFunc();
            CJC_NULLPTR_CHECK(pkgInit);
            // ApplyWithException will create and update current block.
            GenerateFuncCall(*pkgInit, StaticCast<FuncType*>(pkgInit->GetType()),
                std::vector<Type*>{}, nullptr, std::vector<Value*>{}, INVALID_LOCATION);
        }
        auto baseBlock = currentBlock;
        auto tryVal = TranslateExprArg(*tryExpr.tryBlock);
        CreateAndAppendTerminator<GoTo>(loc, GetBlockByAST(*tryExpr.tryBlock), baseBlock);
        if (!HasTypeOfNothing(tryExpr)) {
            // Add location for print warning.
            // example code:
            // var a = try{throw A();1} catch (a:A){2}   ----> the const `1` have a user:`store`, so we shoule add loc
            // for `store`.
            DebugLocation valLoc;
            if (tryVal->IsLocalVar()) {
                valLoc = StaticCast<LocalVar*>(tryVal)->GetExpr()->GetDebugLocation();
            } else {
                valLoc = TranslateLocation(*tryExpr.tryBlock);
            }
            if (retVal) {
                CreateWrappedStore(valLoc, GetDerefedValue(tryVal, valLoc), retVal, currentBlock);
            }
        }
        CreateAndAppendTerminator<GoTo>(loc, endBlock, currentBlock)->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        tryCatchContext.pop();
    }

    // 2. Set branch for catching exception.
    if (!tryExpr.catchBlocks.empty()) {
        currentBlock = exceptionBlock;
        exceptionBlock->SetExceptions(GetExceptionsForTry(tryExpr));
        context.ScopePlus();
        exceptionBlock = TranslateCatchBlocks(tryExpr, retVal, endBlock);
    }

    // 3. Translate finally block with normal and exception cases.
    // NOTE: if finally is existed create 'endBlock' goto 'finally' and 'exceptionBlock' goto finally block,
    // then update endBlock with new end block of finally.
    if (hasFinally) {
        CJC_ASSERT(exceptionBlock);
        context.ScopePlus();
        endBlock = TranslateFinally(*tryExpr.finallyBlock, exceptionBlock);
    }
    tryBodyContext.pop_back();
    currentBlock = endBlock;
    if (HasTypeOfNothing(tryExpr)) {
        return nullptr;
    }
    if (!retVal) {
        return nullptr;
    } else {
        auto res = CreateAndAppendExpression<Load>(loc, tryTy, retVal, currentBlock);
        return res->GetResult();
    }
}

void Translator::CreateFinallyExpr(bool hasFinally, Ptr<Block> endBlock)
{
    if (hasFinally) {
        // NOTE: 'finallyContext' will be pop out inside 'TranslateFinally'.
        auto context = FinallyControlVal(endBlock);
        context.controlFlowBlocks[static_cast<uint8_t>(ControlType::NORMAL)].emplace_back(endBlock, CreateBlock());
        finallyContext.emplace(context);
    }
}

Ptr<Block> Translator::TranslateFinally(const AST::Block& finally, Ptr<Block> exceptionBlock)
{
    CJC_ASSERT(!finallyContext.empty());
    // 1. Generate rethrow finally block. NOTE: finally context should be kept for rethrow.
    currentBlock = exceptionBlock;
    TranslateFinallyRethrowFlows(finally);

    auto finallyControlInfo = finallyContext.top();
    finallyContext.pop();
    // 2. Generate normal finally block for different control flows.
    TranslateFinallyNormalFlows(finally, finallyControlInfo);
    // Return end block of normal flow.
    auto normalInfo = finallyControlInfo.controlFlowBlocks[static_cast<uint8_t>(ControlType::NORMAL)];
    CJC_ASSERT(normalInfo.size() == 1);
    return normalInfo[0].second;
}

void Translator::TranslateFinallyRethrowFlows(const AST::Block& finally)
{
    // NOTE: Intrinsic/beginCatch must be branched from exception,
    // so the normal block of try cannot branch to the block start with Intrinsic/beginCatch.
    // Then the body of finally must be generated twice.
    auto exceptionBlock = currentBlock;
    Translator exceptionTrans = *this; // Copy a translator for exception finally.
    CJC_ASSERT(!exceptionTrans.finallyContext.empty());
    // Clear control flows before generating finally for rethrow flow.
    for (auto& it : exceptionTrans.finallyContext.top().controlFlowBlocks) {
        it.clear();
    }
    currentBlock->SetExceptions({});
    auto baseETy = builder.GetType<RefType>(builder.GetObjectTy());
    auto exception = CreateAndAppendExpression<GetException>(baseETy, currentBlock)->GetResult();
    auto callContext = IntrisicCallContext {
        .kind = IntrinsicKind::BEGIN_CATCH,
        .args = std::vector<Value*>{exception}
    };
    auto eVal = CreateAndAppendExpression<Intrinsic>(baseETy, callContext, currentBlock)->GetResult();
    // Generate rethrow finally block.
    TranslateASTNode(finally, exceptionTrans);
    auto finallyBlock = exceptionTrans.GetBlockByAST(finally);
    CreateAndAppendTerminator<GoTo>(finallyBlock, exceptionBlock);
    auto endCatchBB = CreateBlock();
    CreateAndAppendTerminator<GoTo>(endCatchBB, exceptionTrans.currentBlock);
    if (tryCatchContext.empty()) {
        CreateAndAppendTerminator<RaiseException>(eVal, endCatchBB);
    } else {
        auto errBB = tryCatchContext.top();
        CreateAndAppendTerminator<RaiseException>(eVal, errBB, endCatchBB);
    }
    // The control flows recorded during generating rethrow finally should be redirected to end of rethrow.
    auto& [_, outerControlBlocks] = exceptionTrans.finallyContext.top();
    for (size_t i = 0; i < outerControlBlocks.size(); ++i) {
        auto& info = outerControlBlocks[i];
        // Generate goto rethrow exception for other type of control flows.
        for (auto [prevBlock, _1] : info) {
            CreateAndAppendTerminator<GoTo>(endCatchBB, prevBlock);
        }
    }
}

void Translator::TranslateFinallyNormalFlows(const AST::Block& finally, const FinallyControlVal& finallyControlInfo)
{
    for (size_t i = 0; i < finallyControlInfo.controlFlowBlocks.size(); ++i) {
        auto& info = finallyControlInfo.controlFlowBlocks[i];
        if (info.empty()) {
            continue;
        }
        for (auto [prevBlock, nextBlock] : info) {
            Translator normalTrans = *this; // Copy a translator for normal finally.
            TranslateASTNode(finally, normalTrans);
            auto finallyBlock = normalTrans.GetBlockByAST(finally);
            CreateAndAppendTerminator<GoTo>(finallyBlock, prevBlock);
            finallyBlock->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
            prevBlock->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
            bool isNormalCase = i == static_cast<uint8_t>(ControlType::NORMAL);
            if (normalTrans.finallyContext.empty() || isNormalCase) {
                CreateAndAppendTerminator<GoTo>(nextBlock, normalTrans.currentBlock);
            } else {
                // When there is another finally outside current tryExpr, generate control flow with outsider finally.
                auto& [outerBlock, outerControlBlocks] = normalTrans.finallyContext.top();
                auto outerTryScopeLevel = outerBlock->GetDebugLocation().GetScopeInfo().size();
                auto controlScopeLevel = nextBlock->GetDebugLocation().GetScopeInfo().size();
                // When control flow type is return or its scope level is outside(smaller) the outer try-finally,
                // generate 'goto new end Blo' for current control type and store the control kind value.
                if (static_cast<ControlType>(i) == ControlType::RETURN || controlScopeLevel < outerTryScopeLevel) {
                    outerControlBlocks[i].emplace_back(normalTrans.currentBlock, nextBlock);
                } else {
                    CreateAndAppendTerminator<GoTo>(nextBlock, normalTrans.currentBlock);
                }
            }
            finallyContext = normalTrans.finallyContext; // Update context info.
        }
    }
}

std::vector<ClassType*> Translator::GetExceptionsForTry(const AST::TryExpr& tryExpr)
{
    // Collect exception of outer try start from nearest.
    std::vector<Ptr<const AST::TryExpr>> allTryExprs = tryBodyContext;
    allTryExprs.emplace_back(&tryExpr);
    for (const auto& it : allTryExprs) {
        // catch all Exceptions if outer or this try has finally
        if (it->finallyBlock != nullptr) {
            return {};
        }
    }
    std::set<Ptr<AST::Ty>> exceptionTys;
    auto collectTys = [&exceptionTys](auto ty) {
        auto gty = DynamicCast<AST::GenericsTy>(ty);
        if (!gty) {
            exceptionTys.emplace(ty);
            return;
        }
        for (auto it : gty->upperBounds) {
            // Temporarily collecting all class upper bounds.
            if (it->IsClass()) {
                exceptionTys.emplace(it);
            }
        }
    };
    for (auto it = allTryExprs.crbegin(); it != allTryExprs.crend(); ++it) {
        auto currentTry = *it;
        for (auto& pattern : currentTry->catchPatterns) {
            if (pattern->astKind == AST::ASTKind::WILDCARD_PATTERN) {
                collectTys(pattern->ty);
                continue;
            }
            auto& exceptPattern = StaticCast<AST::ExceptTypePattern>(*pattern);
            for (auto& eType : exceptPattern.types) {
                collectTys(eType->ty);
            }
        }
    }

    auto cmp = [](const Ptr<ClassType>& ty1, const Ptr<ClassType>& ty2) { return ty1->ToString() < ty2->ToString(); };
    std::set<ClassType*, decltype(cmp)> exceptions(cmp);
    for (auto ty : exceptionTys) {
        auto classTypeRef = StaticCast<RefType*>(TranslateType(*ty));
        exceptions.emplace(StaticCast<ClassType*>(classTypeRef->GetBaseType()));
    }
    return std::vector<ClassType*>(exceptions.begin(), exceptions.end());
}

Ptr<Block> Translator::TranslateCatchBlocks(const AST::TryExpr& tryExpr, Ptr<Value> retVal, Ptr<Block> endBlock)
{
    bool hasFinally = tryExpr.finallyBlock != nullptr;
    // When finally is not existed, do not need to create re-throw block.
    Ptr<Block> exceptionBlock = hasFinally ? CreateBlock() : nullptr;
    if (hasFinally) {
        tryCatchContext.push(exceptionBlock);
    }
    auto baseETy = builder.GetType<RefType>(builder.GetObjectTy());
    auto catchLoc = TranslateLocation(*tryExpr.catchBlocks[0]);
    auto exception = CreateAndAppendExpression<GetException>(catchLoc, baseETy, currentBlock)->GetResult();
    auto callContext = IntrisicCallContext {
        .kind = IntrinsicKind::BEGIN_CATCH,
        .args = std::vector<Value*>{exception}
    };
    auto eVal = CreateAndAppendExpression<Intrinsic>(catchLoc, baseETy, callContext, currentBlock)->GetResult();
    auto catchSize = tryExpr.catchPatterns.size();
    CJC_ASSERT(tryExpr.catchBlocks.size() == catchSize);
    for (size_t i = 0; i < catchSize; ++i) {
        auto [falseBlock, trueBlock] = TranslateExceptionPattern(*tryExpr.catchPatterns[i], eVal);
        ScopeContext context(*this);
        context.ScopePlus();
        auto currentBody = TranslateMatchCaseBody(*tryExpr.catchBlocks[i], retVal, endBlock);
        CreateAndAppendTerminator<GoTo>(currentBody, trueBlock);
        currentBlock = falseBlock;
    }
    if (hasFinally) {
        CreateAndAppendTerminator<RaiseException>(eVal, exceptionBlock, currentBlock);
        tryCatchContext.pop();
    } else if (tryCatchContext.empty()) {
        // There exists rethrow context outside the current tryExpr.
        CreateAndAppendTerminator<RaiseException>(eVal, currentBlock);
    } else {
        auto errBB = tryCatchContext.top();
        CreateAndAppendTerminator<RaiseException>(eVal, errBB, currentBlock);
    }
    return exceptionBlock;
}

std::pair<Ptr<Block>, Ptr<Block>> Translator::TranslateExceptionPattern(const AST::Pattern& pattern, Ptr<Value> eVal)
{
    auto trueBlock = CreateBlock();
    const auto& loc = TranslateLocation(pattern);
    if (pattern.astKind == AST::ASTKind::WILDCARD_PATTERN) {
        auto typeTy = TranslateType(*pattern.ty);
        auto falseBlock = CreateBlock();
        auto cond =
            CreateAndAppendExpression<InstanceOf>(loc, builder.GetBoolTy(), eVal, typeTy, currentBlock)->GetResult();
        CreateAndAppendTerminator<Branch>(cond, trueBlock, falseBlock, currentBlock);
        return {falseBlock, trueBlock};
    }
    auto& exceptPattern = StaticCast<AST::ExceptTypePattern>(pattern);
    CJC_ASSERT(exceptPattern.pattern);
    if (exceptPattern.pattern->astKind == AST::ASTKind::VAR_PATTERN) {
        HandleVarPattern(StaticCast<AST::VarPattern>(*exceptPattern.pattern), eVal, trueBlock);
    }
    for (auto& eType : exceptPattern.types) {
        auto falseBlock = CreateBlock();
        auto ty = TranslateType(*eType->ty);
        auto cond =
            CreateAndAppendExpression<InstanceOf>(loc, builder.GetBoolTy(), eVal, ty, currentBlock)->GetResult();
        CreateAndAppendTerminator<Branch>(cond, trueBlock, falseBlock, currentBlock);
        currentBlock = falseBlock;
    }
    return {currentBlock, trueBlock};
}
