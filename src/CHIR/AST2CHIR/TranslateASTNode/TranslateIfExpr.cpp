// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::AST;

namespace Cangjie::CHIR {
bool Translator::CanOptimizeToSwitch(const LetPatternDestructor& let) const
{
    if (!opts.IsOptimizationExisted(FrontendOptions::OptimizationFlag::SWITCH_OPT)) {
        return false;
    }
    // A wildcard pattern renders cases following it unreachable.
    if (let.patterns[0]->astKind == ASTKind::WILDCARD_PATTERN) {
        return false;
    }
    const auto& type = let.initializer->ty;
    bool validSelectorTy = IsOptimizableTy(type) ||
        (type->IsEnum() && IsOptimizableEnumTy(type));
    if (!validSelectorTy) {
        return false;
    }

    for (auto& curPattern : let.patterns) {
        // patterns connected by '|' should be of the same kind, so when the first is not WILDCARD, others neither.
        CJC_ASSERT(curPattern->astKind != ASTKind::WILDCARD_PATTERN);
        if (curPattern->astKind == ASTKind::CONST_PATTERN) {
            continue;
        }
        auto vep = DynamicCast<VarOrEnumPattern>(curPattern.get());
        auto realPatternKind = vep ? vep->pattern->astKind : curPattern->astKind;
        if (realPatternKind != ASTKind::ENUM_PATTERN) {
            return false;
        } else if (vep) {
            continue;
        }
        auto enumPattern = StaticCast<EnumPattern*>(curPattern.get());
        if (enumPattern->patterns.empty()) {
            // enum patterns without params do not break any rule
            continue;
        }
        auto firstParamKind = enumPattern->patterns[0]->astKind;
        // Since types of all enum constructor have been checked, the type of constant pattern must be valid.
        if (firstParamKind == ASTKind::WILDCARD_PATTERN || firstParamKind == ASTKind::CONST_PATTERN) {
            continue;
        }
        return false;
    }
    return true;
}

class TranslateCondCtrlExpr {
public:
    virtual ~TranslateCondCtrlExpr() = default;

protected:
    struct DanglingBlock {
        Ptr<Block> tb; // dangling matching block
        Ptr<Block> fb; // dangling non-matching block. This is null when the pattern definitely matches.
    };

    /// \param isRightmost: Only the rightmost condition branches directly to the true block and false block of the
    /// original if expr, when considering a condition a binary tree formed by logical 'and' and 'or'. Non-rightmost
    /// shall have this parameter set to false during recursive translation and do not generate a branch at the end.
    /// The terminator is instead set by caller.
    ///
    /// Invariant:
    ///     If isRightmost is true, the generated block has a terminator;
    ///     otherwise, the generated block does not have a terminator after \ref TranslateCondition call, its terminator
    ///     is set by its caller.
    ///
    /// e.g. consider this condition (suppose all terminators are let patterns, cause normal expr is translated as
    /// normal expr node)
    /// if (a && (b || c)) {...}
    /// Will have this stack tree:
    /// DO(a && (b || c), true)
    ///     DO(a, false)
    ///     DO(b || c, true)
    ///         DO(b, false)
    ///         DO(c, true)
    ///         BRANCH(b, TRUE-BLOCK, c)
    ///     BRANCH(a, b || c, FALSE-BLOCK)
    DanglingBlock TranslateCondition(const Expr& cond, bool isRightmost)
    {
        if (auto p = DynamicCast<ParenExpr>(&cond)) {
            return TranslateCondition(*p->expr, isRightmost);
        }
        if (auto let = DynamicCast<LetPatternDestructor>(&cond)) {
            return TranslateLetPattern(*let, isRightmost);
        }
        if (auto bin = DynamicCast<BinaryExpr>(&cond); bin && (bin->op == TokenKind::AND || bin->op == TokenKind::OR) &&
            (IsCondition(*bin->leftExpr) || IsCondition(*bin->rightExpr))) {
            return TranslateBinary(*bin, isRightmost);
        }
        // normal expression as condition
        return TranslateNormalCondition(cond, isRightmost);
    }

    static bool HasTypeOfNothing(const AST::IfExpr& ifExpr)
    {
        // Type of if expression may be type of expected target,
        // we need to check all branch for real nothing typed control flow.
        return ifExpr.elseBody && ifExpr.thenBody->ty->IsNothing() && ifExpr.elseBody->ty->IsNothing();
    }

    DanglingBlock TranslateNormalCondition(const Expr& cond, bool isRightmost)
    {
        auto val = tr.TranslateExprArg(cond);
        auto loc = tr.TranslateLocation(cond.begin, cond.end);
        auto cur = tr.GetCurrentBlock();
        if (isRightmost) {
            auto tb = TranslateTrueBlock();
            auto fb = TranslateFalseBlock();
            tr.SetCurrentBlock(*cur);
            auto br = CreateBranch(*val, *cur, *tb, *fb);
            // set debug location to the whole expr for the outermost branch
            br->SetDebugLocation(tr.TranslateLocation(*GetExpr()));
            return {};
        }
        auto tb = CreateBlock();
        auto fb = CreateBlock();
        tr.SetCurrentBlock(*cur);
        auto br = CreateBranch(*tr.GetDerefedValue(val, loc), *cur, *tb, *fb);
        br->SetDebugLocation(loc);
        return {tb, fb};
    }

    /// Make \param left unconditinally goes to \param right and return \param right if both are non empty.
    /// If only one is non empty, returns it. If both are empty, returns null
    Block* MergeBlocks(CHIR::Block* left, CHIR::Block* right)
    {
        if (left) {
            if (right) {
                CreateGoto(*right, *left);
                return right;
            }
            return left;
        }
        return right;
    }

    DanglingBlock TranslateBinary(const BinaryExpr& bin, bool isRightmost)
    {
        auto left = TranslateCondition(*bin.leftExpr, false);
        auto bl = CreateBlock();
        tr.SetCurrentBlock(*bl); // false branch will be generated in this block
        auto right = TranslateCondition(*bin.rightExpr, isRightmost);
        auto loc = tr.TranslateLocation(bin);
        if (bin.op == TokenKind::AND) {
            CreateGoto(*bl, *left.tb);
            // only set to trueBlock/falseBlock of the if expression if this is not rightmost
            // left.fb is null when the left pattern match always returns true (i.e. the pattern is irrefutable)
            if (left.fb) {
                right.fb = MergeBlocks(left.fb, right.fb);
            }
        } else {
            if (left.fb) {
                CreateGoto(*bl, *left.fb);
            } else {
                bl->EnableAttr(Attribute::UNREACHABLE);
            }
            right.tb = MergeBlocks(left.tb, right.tb);
        }
        return right;
    }

    DanglingBlock TranslateLetPattern(const LetPatternDestructor& let, bool isRightmost)
    {
        if (tr.CanOptimizeToSwitch(let)) {
            return TranslateLetPatternAsTable(let, isRightmost);
        } else {
            return TranslateLetPatternAsMatch(let, isRightmost);
        }
    }

    DanglingBlock TranslateLetPatternAsMatch(const LetPatternDestructor& let, bool isRightmost)
    {
        // translate initializer
        auto selectorVal = tr.TranslateExprArg(*let.initializer);
        Translator::ScopeContext context{tr};
        auto originLoc = tr.TranslateLocation(let);
        {
            // translate let pattern
            context.ScopePlus();
            auto [patternFalseBlock, patternTrueBlock] = let.patterns.size() == 1
                ? tr.TranslateNestingCasePattern(*let.patterns[0], selectorVal, originLoc, SourceExpr::IF_EXPR)
                : tr.TranslateOrPattern(let.patterns, selectorVal, originLoc);

            // translate pattern not match block
            if (isRightmost) {
                auto tb = TranslateTrueBlock();
                auto fb = TranslateFalseBlock();
                CreateGoto(*tb, *patternTrueBlock);
                CreateGoto(*fb, *patternFalseBlock);
                return {};
            }
            return {patternTrueBlock, patternFalseBlock};
        }
    }

    DanglingBlock TranslateLetPatternAsTable(const LetPatternDestructor& let, bool isRightmost)
    {
        auto selectorTy = let.initializer->ty;
        auto selectorVal = tr.TranslateExprArg(*let.initializer);
        // Previously checked that selector ty is integer, char or enum.
        auto enumDecl = DynamicCast<AST::EnumDecl>(AST::Ty::GetDeclOfTy(selectorTy));
        DanglingBlock res;
        if (!enumDecl) {
            // NOTE: selector's ty is Rune, UInt, Int.
            res = TranslateTrivialPatternAsTable(let, *selectorVal, 0);
        } else if (!enumDecl->hasArguments || DoesNotHaveEnumSubpattern(let)) {
            // Note: selector'ty is enum, and no enumPattern with param.
            res = TranslateTrivialPatternAsTable(
                let, *GetEnumIDValue(*selectorTy, *selectorVal), enumDecl->constructors.size());
        } else {
            res = TranslateEnumPatternMatchAsTable(let, *selectorVal);
        }
        // translate pattern not match block
        if (isRightmost) {
            auto tb = TranslateTrueBlock();
            auto fb = TranslateFalseBlock();
            CreateGoto(*tb, *res.tb);
            if (res.fb) {
                CreateGoto(*fb, *res.fb);
            }
            return {};
        }
        return res;
    }
    
    DanglingBlock TranslateEnumPatternMatchAsTable(const LetPatternDestructor& let, Value& enumVal)
    {
        bool isStillReachable = true;
        auto baseBlock = tr.GetCurrentBlock();
        auto tb = CreateBlock();
        auto fb = CreateBlock();
        auto firstDefaultBlock = fb;
        auto firstSelectorVal = GetEnumIDValue(*let.initializer->ty, enumVal);
        Translator::EnumMatchInfo matchInfo;
        {
            Translator::ScopeContext context(tr);
            context.ScopePlus();
            if (let.patterns[0]->astKind == AST::ASTKind::WILDCARD_PATTERN) {
                // Wildcard pattern in current case can only exited on top level.
                isStillReachable = false;
                // NOTE: only should to collect first visited wildcard pattern.
                // The sub pattern may contains varPattern, translate when binding second level table.
                firstDefaultBlock = tb;
                const auto& loc = tr.TranslateLocation(*let.patterns[0]);
                firstDefaultBlock->SetDebugLocation(loc);
            }
            // Store pattern value to block relation.
            for (auto& pattern : let.patterns) {
                tr.CollectEnumPatternInfo(*pattern, matchInfo, 0);
            }
        }
        // Create branch to first level switch.
        std::vector<uint64_t> indices{};
        std::vector<CHIR::Block*> blocks{};
        for (auto& p : std::as_const(matchInfo.firstSwitchBlocks)) {
            indices.push_back(p.first);
            blocks.push_back(p.second);
        }
        tr.CreateAndAppendTerminator<MultiBranch>(firstSelectorVal, firstDefaultBlock, indices, blocks, baseBlock);

        // Create second switch block.
        auto branchInfos = tr.TranslateSecondLevelAsTable(matchInfo, &enumVal, firstDefaultBlock);
        // Finally create goto from case's true block to caseBody block.
        for (auto& block : std::as_const(branchInfos[0])) {
            CreateGoto(*tb, *block);
        }

        tr.SetCurrentBlock(*fb);
        return {tb, isStillReachable ? fb : nullptr};
    }

    DanglingBlock TranslateTrivialPatternAsTable(const LetPatternDestructor& let, Value& val, size_t numOfPatterns)
    {
        std::map<uint64_t, Block*> indexToBodies;
        auto baseBlock = tr.currentBlock;
        auto tb = CreateBlock(); // block to which all matching patterns unconditionally goes
        Block* defaultBlock{nullptr};
        bool isStillReachable = true;
        Translator::ScopeContext context{tr};
        context.ScopePlus();
        CJC_ASSERT(!let.patterns.empty());
        if (let.patterns[0]->astKind == ASTKind::WILDCARD_PATTERN) {
            auto loc = tr.TranslateLocation(*let.patterns[0]);
            auto patternBlock = CreateBlock();
            isStillReachable = false;
            defaultBlock = patternBlock;
        } else {
            // Collect mapping: patternVal -> match-block
            for (auto& pattern : let.patterns) {
                auto patternVal = Translator::GetJumpablePatternVal(*pattern);
                auto loc = tr.TranslateLocation(*pattern);
                auto patternBlock = CreateBlock();
                patternBlock->SetDebugLocation(loc);
                CreateGoto(*tb, *patternBlock);
                if (auto [_, success] = indexToBodies.emplace(patternVal, patternBlock); !success) {
                    continue;
                }
                // Enum has limited number of patterns when all of them have been filled, rest cases are unreachable.
                if (indexToBodies.size() == numOfPatterns) {
                    isStillReachable = false;
                    defaultBlock = patternBlock;
                }
            }
        }

        if (!defaultBlock) { // create an empty block to which the control flow goes when the if let pattern does not
            // match
            defaultBlock = CreateBlock();
        }
        std::vector<uint64_t> indices{};
        std::vector<CHIR::Block*> blocks{};
        for (auto& p : std::as_const(indexToBodies)) {
            indices.push_back(p.first);
            blocks.push_back(p.second);
        }
        tr.currentBlock = baseBlock;
        Type* targetType;
        if (auto enumTy = DynamicCast<AST::EnumTy>(let.initializer->ty)) {
            targetType = tr.GetSelectorType(*enumTy);
        } else {
            targetType = tr.builder.GetUInt64Ty();
        }
        auto castedVal = tr.TypeCastOrBoxIfNeeded(val, *targetType, {});
        auto loc = tr.TranslateLocation(let);
        auto multib = tr.CreateAndAppendTerminator<MultiBranch>(castedVal, defaultBlock, indices, blocks, baseBlock);
        multib->SetDebugLocation(loc);
        tr.currentBlock = endBlock;
        return {tb, isStillReachable ? defaultBlock : nullptr};
    }

    ///@{
    /// Encapsulation of Translator methods.
    virtual Branch* CreateBranch(Value& val, CHIR::Block& cur, CHIR::Block& tb, CHIR::Block& fb)
    {
        auto res = tr.CreateAndAppendTerminator<Branch>(&val, &tb, &fb, &cur);
        res->SetSourceExpr(GetSourceExpr());
        return res;
    }
    GoTo* CreateGoto(CHIR::Block& dest, CHIR::Block& from)
    {
        return tr.CreateAndAppendTerminator<GoTo>(&dest, &from);
    }
    Block* CreateBlock()
    {
        return tr.CreateBlock();
    }
    CHIRBuilder& Builder()
    {
        return tr.builder;
    }
    Ptr<Value> GetEnumIDValue(Ty& ty, Value& selectorVal)
    {
        return tr.GetEnumIDValue(&ty, &selectorVal);
    }
    Ptr<Block> GetBlockByAST(const AST::Block& block)
    {
        return tr.GetBlockByAST(block);
    }
    template <class... Args>
    void CreateWrappedStore(Args&&... args)
    {
        tr.CreateWrappedStore(std::forward<Args>(args)...);
    }
    ///@}

    Translator& tr;
    Ptr<Block> endBlock; // end block, the common successor of true block and false block
    const GlobalOptions& opts;

    explicit TranslateCondCtrlExpr(Translator& trs) : tr{trs}, opts{tr.opts} {}

    virtual Block* TranslateTrueBlock() = 0;
    virtual Block* TranslateFalseBlock() = 0;
    virtual SourceExpr GetSourceExpr() = 0;
    virtual const Expr* GetExpr() = 0;
};

class TranslateIfExpr final : private TranslateCondCtrlExpr {
    Ptr<CHIR::Block> trueBlock{}; // true block after translation. This is initialised to null, and set when a
        // terminator condition (i.e. no sub pattern) is first translated. Later trnaslated blocks will have their
        // branch target set to this pointer whenever a translated trueBlock is needed
    Ptr<CHIR::Block> falseBlock{};

    Ptr<Value> retVal{}; // location of value of this IfExpr

    Ptr<const IfExpr> e;

    // tests if both thenBody and elseBody of this IfExpr is empty
    static bool IsEmptyIf(const IfExpr& expr)
    {
        if (!IsEmptyBlock(*expr.thenBody)) {
            return false;
        }
        // thenBody empty
        if (expr.elseBody) {
            if (auto block = DynamicCast<AST::Block>(&*expr.elseBody)) {
                return IsEmptyBlock(*block);
            }
            if (auto elseIf = DynamicCast<AST::IfExpr>(&*expr.elseBody)) {
                return IsEmptyIf(*elseIf);
            }
            return false;
        }
        // no elseBody, thenBody empty, returns true
        return true;
    }

    static bool IsEmptyBlock(const AST::Block& block)
    {
        if (block.body.size() == 1) {
            if (auto lit = DynamicCast<LitConstExpr>(&*block.body[0])) {
                return lit->ty->IsUnit();
            } else {
                return false;
            }
        }
        return block.body.empty();
    }

public:
    explicit TranslateIfExpr(Translator& trs) : TranslateCondCtrlExpr{trs} {}

    Ptr<Value> Translate(const IfExpr& e1)
    {
        e = &e1;
        auto ifType = tr.TranslateType(*e1.ty);
        // generate an Allocate(Unit&) for debugging, so that the if expr can be stepped in
        bool forceGenerateUnit = opts.enableCompileDebug && IsEmptyIf(e1);
        if ((!HasTypeOfNothing(e1) && !ifType->IsUnit()) || forceGenerateUnit) {
            retVal = tr.CreateAndAppendExpression<Allocate>(tr.TranslateLocation(e1),
                Builder().GetType<RefType>(ifType), ifType, tr.GetCurrentBlock())->GetResult();
        }
        auto topRes = TranslateCondition(*e->condExpr, true);
        if (topRes.tb) {
            CreateGoto(*trueBlock, *topRes.tb);
        }
        if (topRes.fb) {
            CreateGoto(*falseBlock, *topRes.fb);
        }
        tr.SetCurrentBlock(*endBlock);
        if (retVal) {
            auto ret = tr.GetDerefedValue(retVal, tr.TranslateLocation(e1));
            if (forceGenerateUnit) {
                StaticCast<LocalVar>(ret)->GetExpr()->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
            }
            return ret;
        }
        return nullptr;
    }
    ~TranslateIfExpr() override = default;

protected:
    const Expr* GetExpr() override
    {
        return e;
    }
    SourceExpr GetSourceExpr() override
    {
        if (e->sugarKind == AST::Expr::SugarKind::FOR_IN_EXPR) {
            return SourceExpr::FOR_IN_EXPR;
        }
        return SourceExpr::IF_EXPR;
    }
    CHIR::Block* TranslateTrueBlock() override
    {
        CJC_ASSERT(!trueBlock);
        CJC_ASSERT(!endBlock);
        endBlock = CreateBlock();
        tr.TranslateSubExprToLoc(*e->thenBody, retVal);
        trueBlock = GetBlockByAST(*e->thenBody);
        trueBlock->SetDebugLocation(tr.TranslateLocation(*e->thenBody));
        if (e->TestAttr(AST::Attribute::IMPLICIT_ADD) &&
            e->condExpr->TestAttr(AST::Attribute::COMPILER_ADD)) {
            if (auto cond = DynamicCast<LitConstExpr>(&*e->condExpr); cond && cond->ty->IsBoolean() &&
                cond->constNumValue.asBoolean) {
                // generated from for in desugar, condition is a constant true
                trueBlock->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
            }
        }
        CreateGoto(*endBlock, *tr.GetCurrentBlock())->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        return trueBlock;
    }

    CHIR::Block* TranslateFalseBlock() override
    {
        CJC_ASSERT(!falseBlock);
        CJC_ASSERT(endBlock);
        if (e->elseBody) {
            auto loc = tr.TranslateLocation(*e);
            if (auto block = DynamicCast<AST::Block>(e->elseBody.get())) {
                tr.TranslateSubExprToLoc(*block, retVal);
                falseBlock = GetBlockByAST(*block);
            } else {
                falseBlock = CreateBlock();
                tr.SetCurrentBlock(*falseBlock);
                tr.TranslateSubExprToLoc(*e->elseBody, retVal);
            }
            falseBlock->SetDebugLocation(tr.TranslateLocation(*e->elseBody));
            CreateGoto(*endBlock, *tr.GetCurrentBlock())->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        } else {
            falseBlock = endBlock;
        }
        return falseBlock;
    }

    ///@{
    /// Encapsulation of Translator methods.
    Branch* CreateBranch(Value& val, CHIR::Block& cur, CHIR::Block& tb, CHIR::Block& fb) override
    {
        auto res = tr.CreateAndAppendTerminator<Branch>(&val, &tb, &fb, &cur);
        res->SetSourceExpr(GetSourceExpr());
        return res;
    }
    ///@}
};

Ptr<Value> Translator::Visit(const AST::IfExpr& ifExpr)
{
    CJC_NULLPTR_CHECK(ifExpr.thenBody);
    TranslateIfExpr impl{*this};
    return impl.Translate(ifExpr);
}

class TranslateWhileExpr final : public TranslateCondCtrlExpr {
public:
    ~TranslateWhileExpr() override = default;
    
    explicit TranslateWhileExpr(Translator& trs) : TranslateCondCtrlExpr{trs}
    {
    }

    Ptr<Value> Translate(const WhileExpr& e1)
    {
        e = &e1;
        TranslateConditionBlock();
        std::optional<Translator::ScopeContext> context;
        if (!e1.TestAttr(AST::Attribute::IMPLICIT_ADD)) {
            context.emplace(tr);
            context->ScopePlus();
        }

        endBlock = CreateBlock();
        // used for jump
        endBlock->SetDebugLocation(tr.TranslateLocation(e1));
        endBlock->Set<SkipCheck>(SkipCheck{SkipKind::SKIP_DCE_WARNING});
        auto topRes = TranslateCondition(*e->condExpr, true);
        if (topRes.tb) {
            CreateGoto(*bodyBlock, *topRes.tb);
        }
        if (topRes.fb) {
            CreateGoto(*endBlock, *topRes.fb);
        }
        tr.SetCurrentBlock(*endBlock);
        return nullptr;
    }

protected:
    void TranslateConditionBlock()
    {
        conditionBlock = CreateBlock();
        if (e->sugarKind == AST::Expr::SugarKind::FOR_IN_EXPR) {
            conditionBlock->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        }
        CreateGoto(*conditionBlock, *tr.GetCurrentBlock());
        tr.SetCurrentBlock(*conditionBlock);
    }

    CHIR::Block* TranslateTrueBlock() override
    {
        CJC_ASSERT(!bodyBlock);
        std::pair bls{conditionBlock, endBlock};
        tr.terminatorSymbolTable.Set(*e, bls);
        {
            std::optional<Translator::ScopeContext> context;
            if (!e->TestAttr(AST::Attribute::IMPLICIT_ADD)) {
                context.emplace(tr);
                context->ScopePlus();
            }

            tr.TranslateSubExprToDiscarded(*e->body); // value of while body is always unit
            bodyBlock = tr.GetBlockByAST(*e->body);
            bodyBlock->SetDebugLocation(tr.TranslateLocation(e->body->begin, e->body->end));
        }
        auto bodyBlockEnd = tr.GetCurrentBlock();
        auto br = CreateGoto(*conditionBlock, *bodyBlockEnd);
        br->SetDebugLocation(tr.TranslateLocation(*e->condExpr));
        br->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
        tr.terminatorSymbolTable.Erase(*e);
        return bodyBlock;
    }

    Ptr<CHIR::Block> conditionBlock{};
    Ptr<CHIR::Block> bodyBlock{};
    Ptr<const WhileExpr> e;

    Block* TranslateFalseBlock() override
    {
        return endBlock;
    }

    const Expr* GetExpr() override
    {
        return e;
    }

    SourceExpr GetSourceExpr() override
    {
        if (e->sugarKind == AST::Expr::SugarKind::FOR_IN_EXPR) {
            return SourceExpr::FOR_IN_EXPR;
        }
        return SourceExpr::WHILE_EXPR;
    }
};

Ptr<Value> Translator::Visit(const AST::WhileExpr& whileExpr)
{
    TranslateWhileExpr impl{*this};
    return impl.Translate(whileExpr);
}
}
