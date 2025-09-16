// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

bool Translator::CanOptimizeMatchToSwitch(const AST::MatchExpr& matchExpr)
{
    // The enum pattern and const pattern which match the rules below can be optimized using switch node:
    // 1. no pattern guard
    // 2. no varBindingPattern
    // 3. more than one case and first case is not wildCard pattern.
    // 4. For enum pattern, enum constructor with param that only support FIRST param is type of Int, Uint, Rune.
    //    NOTE: it will generate up to one level of nested switch table.
    //    NOTE: enum type is not std.core's Option.
    // 5. For const pattern, UInt64, UInt32, UInt16, UInt8, Int64, Int32, Int16, Int8 , Rune are supported.
    if (!opts.IsOptimizationExisted(FrontendOptions::OptimizationFlag::SWITCH_OPT)) {
        return false;
    }
    if (matchExpr.matchCases.size() <= 1 ||
        matchExpr.matchCases[0]->patterns[0]->astKind == AST::ASTKind::WILDCARD_PATTERN) {
        return false;
    }
    const auto& type = matchExpr.selector->ty;
    bool validSelectorTy = IsOptimizableTy(type) || (type->IsEnum() && IsOptimizableEnumTy(type));
    if (!validSelectorTy) {
        return false;
    }
    for (auto& curCase : matchExpr.matchCases) {
        // Rule1. no pattern guard
        if (curCase->patternGuard) {
            return false;
        }
        // Beware: case cond can have |. for example, case x1|x2:
        for (auto& curPattern : curCase->patterns) {
            // Any wildcard pattern will render cases following it unreachable.
            if (curPattern->astKind == AST::ASTKind::WILDCARD_PATTERN) {
                return true;
            } else if (curPattern->astKind == AST::ASTKind::CONST_PATTERN) {
                continue;
            }
            auto vep = DynamicCast<AST::VarOrEnumPattern>(curPattern.get());
            auto realPatternKind = vep ? vep->pattern->astKind : curPattern->astKind;
            if (realPatternKind != AST::ASTKind::ENUM_PATTERN) {
                return false;
            } else if (vep) {
                // When pattern is 'VarOrEnumPattern' which desugared to enum pattern, it must not have param.
                continue;
            }
            auto enumPattern = StaticCast<AST::EnumPattern*>(curPattern.get());
            if (enumPattern->patterns.empty()) {
                // enum patterns without params do not break any rule
                continue;
            }
            auto firstParamKind = enumPattern->patterns[0]->astKind;
            // Since types of all enum constructor have been checked, the type of constant pattern must be valid.
            if (firstParamKind == AST::ASTKind::WILDCARD_PATTERN || firstParamKind == AST::ASTKind::CONST_PATTERN) {
                continue;
            }
            return false;
        }
    }
    return true;
}

namespace {
void PrintDebugMessage(bool isDebug, const AST::Node& node)
{
    if (!isDebug) {
        return;
    }
    std::string fileStr = " file: " + (node.curFile ? node.curFile->fileName : std::to_string(node.begin.fileID));
    std::string message = "The matchExpr in the" + fileStr + " line:" + std::to_string(node.begin.line) +
        " and the column:" + std::to_string(node.begin.column) + " was optimized by The opt: switchOpt";
    std::cout << message << std::endl;
}

/**
 * Type of match expression may be type of expected target,
 * we need to check all branch for real nothing typed control flow.
 */
inline bool HasTypeOfNothing(const AST::MatchExpr& matchExpr)
{
    if (matchExpr.matchMode) {
        return std::all_of(
            matchExpr.matchCases.cbegin(), matchExpr.matchCases.cend(), [](auto& it) { return it->ty->IsNothing(); });
    } else {
        return std::all_of(matchExpr.matchCaseOthers.cbegin(), matchExpr.matchCaseOthers.cend(),
            [](auto& it) { return it->ty->IsNothing(); });
    }
}

inline SourceExpr GetSourceExprByMatchExpr(const AST::MatchExpr& matchExpr)
{
    if (matchExpr.sugarKind == AST::Expr::SugarKind::QUEST) {
        return SourceExpr::QUEST;
    }
    return SourceExpr::MATCH_EXPR;
}
} // namespace

Ptr<Value> Translator::Visit(const AST::MatchExpr& matchExpr)
{
    auto matchTy = TranslateType(*matchExpr.ty);
    Ptr<Value> retVal = HasTypeOfNothing(matchExpr) || matchTy->IsUnit() || matchTy->IsNothing() ? nullptr :
        CreateAndAppendExpression<Allocate>(builder.GetType<RefType>(matchTy), matchTy, currentBlock)->GetResult();
    if (matchExpr.matchMode) {
        if (CanOptimizeMatchToSwitch(matchExpr)) {
            PrintDebugMessage(opts.chirDebugOptimizer, matchExpr);
            TranslateMatchAsTable(matchExpr, retVal);
        } else {
            TranslateMatchWithSelector(matchExpr, retVal);
        }
    } else {
        TranslateConditionMatches(matchExpr, retVal);
    }
    // When match expr have type of 'Unit' or 'Nothing', the alloca can be ignored, directly return literal.
    // And all store to 'retVal' alloca is ignored.
    if (!retVal) {
        return nullptr;
    } else {
        return CreateAndAppendExpression<Load>(matchTy, retVal, currentBlock)->GetResult();
    }
}

void Translator::TranslateMatchWithSelector(const AST::MatchExpr& matchExpr, Ptr<Value> retVal)
{
    auto selectorVal = TranslateExprArg(*matchExpr.selector);
    SetSkipPrintWarning(selectorVal);
    auto endBlock = CreateBlock();
    size_t caseNum = matchExpr.matchCases.size();
    bool needScope = matchExpr.sugarKind != AST::Expr::SugarKind::IS && matchExpr.sugarKind != AST::Expr::SugarKind::AS;
    std::optional<ScopeContext> context;
    if (needScope) {
        context.emplace(*this);
    }
    const auto& originLoc = TranslateLocation(matchExpr);
    SourceExpr sourceExpr = GetSourceExprByMatchExpr(matchExpr);
    for (size_t i = 0; i < caseNum; ++i) {
        if (needScope) {
            context->ScopePlus();
        }
        auto current = matchExpr.matchCases[i].get();
        // Generate case pattern condition and set var pattern symbol.
        auto [falseBlock, trueBlock] = current->patterns.size() == 1
            ? TranslateNestingCasePattern(*current->patterns[0], selectorVal, originLoc, sourceExpr)
            : TranslateOrPattern(current->patterns, selectorVal, originLoc);
        TranslatePatternGuard(*current, falseBlock, trueBlock);
        if (sourceExpr == SourceExpr::QUEST && i == 0) {
            CJC_ASSERT(caseNum == 2); // 2 denote than QUEST has two cases.
            if (!matchExpr.matchCases[1]->exprOrDecls->body.empty()) {
                const auto& loc1 = TranslateLocation(matchExpr.matchCases[1]->exprOrDecls->body.front()->begin,
                    matchExpr.matchCases[1]->exprOrDecls->body.back()->end);
                falseBlock->SetDebugLocation(loc1);
            }
            CJC_ASSERT(trueBlock->GetPredecessors().size() == 1);
            const auto& loc2 = TranslateLocation(*matchExpr.matchCases[0]);
            trueBlock->GetPredecessors()[0]->SetDebugLocation(loc2);
        }
        auto currentBody = TranslateMatchCaseBody(*current->exprOrDecls, retVal, endBlock);
        CreateAndAppendTerminator<GoTo>(currentBody, trueBlock);
        if (i == caseNum - 1) {
            CreateAndAppendTerminator<GoTo>(endBlock, falseBlock);
            // Last falseBlocks for all cases is unreachable, marking for later analysis.
            falseBlock->EnableAttr(Attribute::UNREACHABLE);
        } else {
            currentBlock = falseBlock;
        }
    }
    // Update block at the end.
    currentBlock = endBlock;
}

/**
 * Translate pattern guard if existed, and update @p 'trueBlock' .
 */
void Translator::TranslatePatternGuard(const AST::MatchCase& matchCase, Ptr<Block> falseBlock, Ptr<Block>& trueBlock)
{
    if (!matchCase.patternGuard) {
        return;
    }
    currentBlock = trueBlock;
    auto newTrueBlock = CreateBlock();
    auto cond = TranslateExprArg(*matchCase.patternGuard);
    CreateAndAppendTerminator<Branch>(cond, newTrueBlock, falseBlock, currentBlock);
    trueBlock = newTrueBlock;
}

Block* Translator::TranslateMatchCaseBody(
    const AST::Block& caseBody, Ptr<Value> retVal, Ptr<Block> endBlock)
{
    auto loc = caseBody.rightCurlPos.IsZero() ? caseBody.body.empty() ? TranslateLocation(caseBody.end, caseBody.end)
        : TranslateLocation(*caseBody.body.back()) : TranslateLocation(caseBody);
    TranslateSubExprToLoc(caseBody, retVal, loc);
    auto currentBody = GetBlockByAST(caseBody);
    CreateAndAppendTerminator<GoTo>(endBlock, currentBlock);
    // Return the free block for connecting control flow at outside.
    return currentBody;
}

namespace {
uint64_t GetConstPatternVal(const AST::ConstPattern& constPattern)
{
    auto litExpr = StaticCast<AST::LitConstExpr*>(constPattern.literal.get());
    auto ty = litExpr->ty;
    uint64_t curVal = 0;
    if (ty->IsSignedInteger()) {
        // Support const pattern, include Int64, Int32, Int16, Int8
        curVal = static_cast<uint64_t>(litExpr->constNumValue.asInt.Int64());
    } else if (ty->IsUnsignedInteger()) {
        // Support const pattern, include UInt64, UInt32, UInt16, UInt8
        curVal = litExpr->constNumValue.asInt.Uint64();
    } else if (ty->IsRune()) {
        curVal = static_cast<uint64_t>(litExpr->codepoint[0]);
    } else if (ty->IsBoolean()) {
        curVal = static_cast<uint64_t>(litExpr->constNumValue.asBoolean);
    } else {
        // 'String' and floating type should not enter this function.
        InternalError("unsupported const pattern type: ", ty->String());
    }
    return curVal;
}

const AST::EnumPattern& GetRealEnumPattern(const AST::Pattern& pattern)
{
    CJC_ASSERT(pattern.astKind == AST::ASTKind::VAR_OR_ENUM_PATTERN || pattern.astKind == AST::ASTKind::ENUM_PATTERN);
    if (auto vep = DynamicCast<AST::VarOrEnumPattern>(&pattern)) {
        return StaticCast<AST::EnumPattern>(*vep->pattern);
    } else {
        return StaticCast<AST::EnumPattern>(pattern);
    }
}

bool IsOptimizableEnumPatterns(const std::vector<OwnedPtr<AST::Pattern>>& patterns)
{
    for (auto& it : patterns) {
        CJC_ASSERT(it->astKind == AST::ASTKind::VAR_OR_ENUM_PATTERN || it->astKind == AST::ASTKind::ENUM_PATTERN);
        auto ep = DynamicCast<AST::EnumPattern>(it.get());
        // When enum sub pattern contains any of non-wildcard pattern, current or-pattern cannot be optimized.
        if (ep && std::any_of(ep->patterns.cbegin(), ep->patterns.cend(), [](auto& sub) {
                return sub->astKind != AST::ASTKind::WILDCARD_PATTERN;
            })) {
            return false;
        }
    }
    return true;
}
} // namespace

uint64_t Translator::GetEnumPatternID(const AST::EnumPattern& enumPattern)
{
    auto target = enumPattern.constructor->GetTarget();
    return GetEnumCtorId(*target);
}

uint64_t Translator::GetEnumCtorId(const AST::Decl& target)
{
    CJC_ASSERT(target.outerDecl && target.outerDecl->astKind == AST::ASTKind::ENUM_DECL);
    auto enumDecl = StaticCast<AST::EnumDecl>(target.outerDecl);
    for (uint64_t i = 0; i < enumDecl->constructors.size(); ++i) {
        if (enumDecl->constructors[i].get() == &target) {
            return i;
        }
    }
    CJC_ABORT(); // Target must existed inside enum constructors.
    return 0;
}

// Sub-pattern of or-pattern must not introduce new variable.
std::pair<Ptr<Block>, Ptr<Block>> Translator::TranslateOrPattern(
    const std::vector<OwnedPtr<AST::Pattern>>& patterns, Ptr<Value> selectorVal, const DebugLocation& originLoc)
{
    CJC_ASSERT(patterns.size() > 1);
    // Or-pattern can only connect same type of patterns without nesting var pattern.
    auto patternKind = patterns[0]->astKind;
    if (patternKind == AST::ASTKind::WILDCARD_PATTERN) {
        auto trueBlock = CreateBlock();
        CreateAndAppendTerminator<GoTo>(trueBlock, currentBlock);
        return {CreateBlock(), trueBlock};
    } else if (patternKind == AST::ASTKind::CONST_PATTERN) {
        if (patterns[0]->ty->IsString() || patterns[0]->ty->IsFloating()) {
            return TranslateComplicatedOrPattern(patterns, selectorVal, originLoc);
        } else if (patterns[0]->ty->IsUnit()) {
            // unit literal is always match
            auto trueBlock = CreateBlock();
            CreateAndAppendTerminator<GoTo>(trueBlock, currentBlock);
            return {CreateBlock(), trueBlock};
        }
        std::set<uint64_t> values; // Switch case cannot be duplicated (llvm ir restriction).
        for (auto& it : patterns) {
            values.emplace(GetConstPatternVal(StaticCast<AST::ConstPattern>(*it)));
        }
        return TranslateConstantMultiOr(Utils::SetToVec<uint64_t>(values), selectorVal);
    } else if (patternKind == AST::ASTKind::TUPLE_PATTERN || patternKind == AST::ASTKind::TYPE_PATTERN) {
        return TranslateComplicatedOrPattern(patterns, selectorVal, originLoc);
    }
    CJC_ASSERT(patternKind == AST::ASTKind::VAR_OR_ENUM_PATTERN || patternKind == AST::ASTKind::ENUM_PATTERN);
    if (IsOptimizableEnumPatterns(patterns)) {
        std::set<uint64_t> values;
        for (auto& it : patterns) {
            values.emplace(GetEnumPatternID(GetRealEnumPattern(*it)));
        }
        return TranslateConstantMultiOr(
            Utils::SetToVec<uint64_t>(values), GetEnumIDValue(patterns[0]->ty, selectorVal));
    }
    return TranslateComplicatedOrPattern(patterns, selectorVal, originLoc);
}

Ptr<Value> Translator::GetEnumIDValue(Ptr<AST::Ty> ty, Ptr<Value> selectorVal)
{
    CJC_ASSERT(ty && ty->kind == AST::TypeKind::TYPE_ENUM);
    auto enumDecl = StaticCast<AST::EnumDecl>(AST::Ty::GetDeclOfTy(ty));
    auto selectorType = GetSelectorType(StaticCast<AST::EnumTy>(*ty));
    if (!enumDecl->hasArguments) {
        return CreateWrappedTypeCast(selectorType, GetDerefedValue(selectorVal), currentBlock)->GetResult();
    }
    return CreateAndAppendExpression<Field>(
        selectorType, selectorVal, std::vector<uint64_t>{0}, currentBlock)->GetResult();
}

std::pair<Ptr<Block>, Ptr<Block>> Translator::TranslateConstantMultiOr(
    const std::vector<uint64_t> values, Ptr<Value> value)
{
    auto falseBlock = CreateBlock();
    auto trueBlock = CreateBlock();
    std::vector<Block*> succs(values.size(), trueBlock);
    CreateAndAppendTerminator<MultiBranch>(
        TypeCastOrBoxIfNeeded(*value, *builder.GetUInt64Ty(), value->GetDebugLocation(), false),
        falseBlock, values, succs, currentBlock);
    return {falseBlock, trueBlock};
}

// Sub-pattern of or-pattern must not introduce new variable.
std::pair<Ptr<Block>, Ptr<Block>> Translator::TranslateComplicatedOrPattern(
    const std::vector<OwnedPtr<AST::Pattern>>& patterns, const Ptr<Value> selectorVal, const DebugLocation& originLoc)
{
    CJC_ASSERT(patterns.size() > 1);
    auto trueBlock = CreateBlock();
    for (auto& it : patterns) {
        auto [innerFalse, innerTrue] = TranslateNestingCasePattern(*it, selectorVal, originLoc);
        CreateAndAppendTerminator<GoTo>(trueBlock, innerTrue);
        currentBlock = innerFalse;
    }
    return {currentBlock, trueBlock};
}

// NOTE: 'blocks' is pair of {final matched block, next condition block}
// 'enum pattern''s element value should be generated in 'next condition block'.
// 'var pattern''s typecast should be generated in 'final matched block'.
Ptr<Value> Translator::DispatchingPattern(std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>>& queue,
    const std::pair<Ptr<Block>, Ptr<Block>>& blocks, const DebugLocation& originLoc)
{
    auto [pattern, value] = queue.front();
    queue.pop();
    Ptr<Value> cond = nullptr;
    switch (pattern->astKind) {
        case AST::ASTKind::VAR_OR_ENUM_PATTERN:
            queue.push(std::make_pair(StaticCast<AST::VarOrEnumPattern>(pattern)->pattern.get(), value));
            break;
        case AST::ASTKind::VAR_PATTERN:
            HandleVarPattern(*StaticCast<AST::VarPattern>(pattern), value, blocks.first);
            break;
        case AST::ASTKind::WILDCARD_PATTERN:
            break;
        case AST::ASTKind::TUPLE_PATTERN:
            CollectingSubPatterns(pattern->ty, StaticCast<AST::TuplePattern>(pattern)->patterns, value, queue);
            break;
        case AST::ASTKind::ENUM_PATTERN:
            cond = HandleEnumPattern(*StaticCast<AST::EnumPattern>(pattern), value, queue, blocks.second, originLoc);
            break;
        case AST::ASTKind::TYPE_PATTERN:
            cond = HandleTypePattern(*StaticCast<AST::TypePattern>(pattern), value, queue);
            break;
        case AST::ASTKind::CONST_PATTERN:
            cond = HandleConstPattern(*StaticCast<AST::ConstPattern>(pattern), value, originLoc);
            break;
        default: {
            InternalError("translating unsupported pattern");
        }
    }
    return cond;
}

std::pair<Ptr<Block>, Ptr<Block>> Translator::TranslateNestingCasePattern(const AST::Pattern& pattern,
    const Ptr<Value> selectorVal, const DebugLocation& originLoc, const SourceExpr& sourceExpr)
{
    auto falseBlock = CreateBlock();
    auto trueBlock = CreateBlock();
    std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>> queue;
    queue.push(std::make_pair(&pattern, selectorVal));
    auto newBlock = CreateBlock();
    const auto& patternLoc = TranslateLocation(pattern);
    if (sourceExpr != SourceExpr::QUEST) {
        currentBlock->SetDebugLocation(patternLoc);
    }
    while (!queue.empty()) {
        // Typecast of varPattern must be generated at the real trueBlock.
        auto cond = DispatchingPattern(queue, {trueBlock, newBlock}, originLoc);
        if (cond) {
            auto nextBlock = queue.empty() ? trueBlock : newBlock;
            if (sourceExpr != SourceExpr::QUEST) {
                nextBlock->SetDebugLocation(patternLoc);
            }
            CreateWrappedBranch(sourceExpr, GetDerefedValue(cond), nextBlock, falseBlock, currentBlock);
            currentBlock = nextBlock;
            if (!queue.empty()) {
                newBlock = CreateBlock();
            }
        } else if (queue.empty()) {
            if (sourceExpr != SourceExpr::QUEST) {
                currentBlock->SetDebugLocation(patternLoc);
            }
            CreateAndAppendTerminator<GoTo>(trueBlock, currentBlock);
        }
    }
    newBlock->RemoveSelfFromBlockGroup();
    return {falseBlock, trueBlock};
}

void Translator::CollectingSubPatterns(const Ptr<AST::Ty>& patternTy,
    const std::vector<OwnedPtr<AST::Pattern>>& patterns, const Ptr<Value> value,
    std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>>& queue, unsigned offset)
{
    if (patterns.empty()) {
        return;
    }
    // Expand patterns and push to queue.
    std::vector<uint64_t> indexes;
    auto funcTy = DynamicCast<AST::FuncTy>(patternTy);
    // NOTE: When expected pattern type is type of function, current is destructing Enum
    // that we can only load first level field here.
    bool isEnumField = funcTy != nullptr;
    using CollectFnType = std::function<void(Ptr<const AST::Pattern>, Ptr<AST::Ty>)>;
    CollectFnType collectRecursively = [this, &indexes, &queue, &collectRecursively, &value, isEnumField](
                                           auto pattern, auto ty) {
        if (pattern->astKind == AST::ASTKind::WILDCARD_PATTERN) {
            // When sub-pattern is wildcard pattern, the 'value' will be ignored.
            queue.push(std::make_pair(pattern, value));
            return;
        }
        auto tp = DynamicCast<AST::TuplePattern>(pattern);
        if (!tp || isEnumField) {
            auto elementTy = TranslateType(*ty);
            auto elementVal = CreateAndAppendExpression<Field>(elementTy, value, indexes, currentBlock)->GetResult();
            queue.push(std::make_pair(pattern, elementVal));
            return;
        }
        for (size_t i = 0; i < tp->patterns.size(); i++) {
            indexes.emplace_back(i);
            collectRecursively(tp->patterns[i].get(), tp->ty->typeArgs[i]);
            indexes.pop_back();
        }
    };
    const auto& typeArgs = funcTy ? funcTy->paramTys : patternTy->typeArgs;
    CJC_ASSERT(typeArgs.size() == patterns.size());
    for (size_t i = 0; i < patterns.size(); i++) {
        // First accessing should adding the offset.
        indexes.emplace_back(i + offset);
        collectRecursively(patterns[i].get(), typeArgs[i]);
        indexes.pop_back();
    }
}

void Translator::HandleVarPattern(
    const AST::VarPattern& varPattern, const Ptr<Value> value, const Ptr<Block>& trueBlock)
{
    auto backupBlock = currentBlock;
    // NOTE: typecast should be generated in the 'matched' block.
    currentBlock = trueBlock;
    auto val =
        varPattern.desugarExpr ? GetDerefedValue(value, TranslateLocation(varPattern)) : value;
    auto varType = TranslateType(*varPattern.ty);
    const auto& loc = TranslateLocation(varPattern);
    if (opts.enableCompileDebug || opts.enableCoverage) {
        // When debug is enabled, the debug info of variable decl must be assigned with an alloca reference.
        // NOTE: debug should be generated inside 'trueBlock'.
        auto alloca =
            CreateAndAppendExpression<Allocate>(builder.GetType<RefType>(varType), varType, trueBlock)->GetResult();
        // NOTE: scope info of debug location for var pattern in enum switch case should following match case body,
        // should not use current scope info.
        if (!varPattern.varDecl->TestAttr(AST::Attribute::COMPILER_ADD)) {
            CreateAndAppendExpression<Debug>(
                loc, builder.GetUnitTy(), alloca, varPattern.varDecl->identifier, trueBlock);
        }
        CreateAndAppendExpression<Store>(
            loc, builder.GetUnitTy(), TypeCastOrBoxIfNeeded(*val, *varType, loc, false), alloca, trueBlock);
        val = CreateAndAppendExpression<Load>(varType, alloca, trueBlock)->GetResult();
    } else {
        val = TypeCastOrBoxIfNeeded(*val, *varType, loc);
    }
    val->EnableAttr(Attribute::READONLY);
    currentBlock = backupBlock;
    if (varPattern.desugarExpr) {
        // Mapping 'desugarExpr' for boxing/unboxing case.
        // The 'value' will be alloca of expected type for unboxing and real type for boxing case.
        exprValueTable.Set(*varPattern.desugarExpr, *val);
    } else {
        SetSymbolTable(*varPattern.varDecl, *val);
    }
}

Ptr<Value> Translator::CastEnumValueToConstructorTupleType(Ptr<Value> enumValue, const AST::EnumPattern& enumPattern)
{
    auto target = enumPattern.constructor->GetTarget();
    CJC_ASSERT(target->outerDecl && target->outerDecl->astKind == AST::ASTKind::ENUM_DECL);
    std::vector<Type*> resTypes = {
        GetSelectorType(StaticCast<AST::EnumTy>(*target->outerDecl->ty))
    };
    std::vector<Ptr<AST::Ty>> paramTys;
    if (auto funcTy = DynamicCast<AST::FuncTy*>(enumPattern.constructor->ty)) {
        paramTys = funcTy->paramTys;
    } else {
        paramTys = enumPattern.constructor->ty->typeArgs;
    }
    for (auto ty : paramTys) {
        resTypes.emplace_back(TranslateType(*ty));
    }
    auto res = TypeCastOrBoxIfNeeded(*enumValue, *builder.GetType<TupleType>(resTypes), enumValue->GetDebugLocation());
    auto enumId = GetEnumPatternID(enumPattern);
    res->Set<EnumCaseIndex>(enumId);
    return res;
}

Ptr<Value> Translator::HandleEnumPattern(const AST::EnumPattern& enumPattern, Ptr<Value> value,
    std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>>& queue, const Ptr<Block>& trueBlock,
    const DebugLocation& originLoc)
{
    auto enumIdx = GetEnumIDValue(enumPattern.ty, value);
    auto selectorTy = GetSelectorType(StaticCast<AST::EnumTy>(*enumPattern.ty));
    auto enumId = GetEnumPatternID(enumPattern);

    auto constExpr = (selectorTy->IsBoolean()
            ? CreateAndAppendConstantExpression<BoolLiteral>(
                  selectorTy, *currentBlock, static_cast<bool>(enumId))
            : CreateAndAppendConstantExpression<IntLiteral>(selectorTy, *currentBlock, enumId));
    LocalVar* idValue = constExpr->GetResult();
    auto cond = CreateAndAppendExpression<BinaryExpression>(
        originLoc, builder.GetBoolTy(), ExprKind::EQUAL, enumIdx, idValue, currentBlock)
                    ->GetResult();
    CJC_ASSERT(value->GetType()->IsEnum());
    // Set current to 'trueBlock' for generating enum element.
    auto backBlock = currentBlock;
    currentBlock = trueBlock;
    if (!enumPattern.patterns.empty()) {
        value = CastEnumValueToConstructorTupleType(value, enumPattern);
    }
    // Enum pattern's field has offset '1' that index 0 is the id of the enum constructor.
    CollectingSubPatterns(enumPattern.constructor->ty, enumPattern.patterns, value, queue, 1);
    currentBlock = backBlock;
    return cond;
}

Type* Translator::GetSelectorType(const AST::EnumTy& ty) const
{
    auto kind = Cangjie::CHIR::GetSelectorType(*ty.decl);
    return builder.GetChirContext().ToSelectorType(kind);
}

Ptr<Value> Translator::HandleConstPattern(
    const AST::ConstPattern& constPattern, Ptr<Value> value, const DebugLocation& originLoc)
{
    if (constPattern.operatorCallExpr == nullptr) {
        auto litVal = TranslateExprArg(*constPattern.literal);
        SetSkipPrintWarning(litVal);
        return CreateAndAppendExpression<BinaryExpression>(
            originLoc, builder.GetBoolTy(), ExprKind::EQUAL, value, litVal, currentBlock)
            ->GetResult();
    } else {
        // AST desugared the base of 'operatorCallExpr' as a special dummy node.
        // So we need to set the value of 'base' here to avoid translating invalid base of call.
        auto base = StaticCast<AST::MemberAccess>(
            StaticCast<AST::CallExpr>(constPattern.operatorCallExpr.get())->baseFunc.get())
                        ->baseExpr.get();
        exprValueTable.Set(*base, *value);
        return TranslateExprArg(*constPattern.operatorCallExpr);
    }
}

Ptr<Value> Translator::HandleTypePattern(const AST::TypePattern& typePattern, Ptr<Value> value,
    std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>>& queue)
{
    if (typePattern.matchBeforeRuntime) {
        if (typePattern.desugarExpr) {
            // Upcast boxing case.
            CJC_NULLPTR_CHECK(typePattern.desugarVarPattern);
            SetSymbolTable(*typePattern.desugarVarPattern->varDecl, *value);
            auto newVal = TranslateExprArg(*typePattern.desugarExpr);
            queue.push(std::make_pair(typePattern.pattern.get(), newVal));
        } else if (typePattern.pattern->astKind == AST::ASTKind::VAR_PATTERN) {
            queue.push(std::make_pair(typePattern.pattern.get(), value));
        } // If sub pattern is varPattern, the 'value' can be ignored.
        // When pattern is always matched, do not return condition value.
        return nullptr;
    }
    auto targetTy = TranslateType(*typePattern.type->ty);
    queue.push(std::make_pair(typePattern.pattern.get(), value));
    if (typePattern.needRuntimeTypeCheck) {
        return CreateAndAppendExpression<InstanceOf>(builder.GetBoolTy(), value, targetTy, currentBlock)->GetResult();
    } else {
        // When 'matchBeforeRuntime' and 'needRuntimeTypeCheck' are both false, the brans is unreachable.
        auto expr = CreateAndAppendConstantExpression<BoolLiteral>(builder.GetBoolTy(), *currentBlock, false);
        return expr->GetResult();
    }
}

void Translator::TranslateConditionMatches(const AST::MatchExpr& matchExpr, Ptr<Value> retVal)
{
    auto endBlock = CreateBlock();
    bool isStillReachable = true;
    size_t caseSize = matchExpr.matchCaseOthers.size();
    for (size_t i = 0; i < caseSize; ++i) {
        auto current = matchExpr.matchCaseOthers[i].get();
        ScopeContext context(*this);
        auto baseBlock = currentBlock;
        auto currentBody = TranslateMatchCaseBody(*current->exprOrDecls, retVal, endBlock);
        if (!isStillReachable) {
            continue;
        } else if (current->matchExpr->astKind == AST::ASTKind::WILDCARD_EXPR) {
            CreateAndAppendTerminator<GoTo>(currentBody, baseBlock);
            // All cases after wildcard expr are unreachable.
            isStillReachable = false;
        } else {
            currentBlock = baseBlock;
            auto nextBlock = CreateBlock();
            auto cond = TranslateExprArg(*current->matchExpr);
            // Create condition brach in base block and create else block as next case block.
            CreateAndAppendTerminator<Branch>(cond, currentBody, nextBlock, currentBlock);
            currentBlock = nextBlock;
            if (i == caseSize - 1) {
                // Latest 'currentBlock' for all cases is unreachable, marking for later analysis.
                currentBlock->EnableAttr(Attribute::UNREACHABLE);
            }
        }
    }
    // Update block at the end.
    currentBlock = endBlock;
}

namespace {
bool WithoutEnumSubPattern(const AST::MatchExpr& match)
{
    for (auto& curCase : match.matchCases) {
        for (auto& subPattern : curCase->patterns) {
            if (subPattern->astKind != AST::ASTKind::ENUM_PATTERN) {
                continue;
            }
            auto enumPattern = StaticCast<AST::EnumPattern*>(subPattern.get());
            if (!enumPattern->patterns.empty()) {
                return false;
            }
        }
    }
    return true;
}

template <typename T, typename K>
std::pair<std::vector<T>, std::vector<K>> ConvertFromMapToVectors(const std::map<T, K>& inMap)
{
    std::vector<T> keys;
    std::vector<K> values;
    for (auto [key, value] : inMap) {
        keys.emplace_back(key);
        values.emplace_back(value);
    }
    return {keys, values};
}
} // namespace

uint64_t Translator::GetJumpablePatternVal(const AST::Pattern& pattern)
{
    CJC_ASSERT(pattern.astKind == AST::ASTKind::CONST_PATTERN || pattern.astKind == AST::ASTKind::VAR_OR_ENUM_PATTERN ||
        pattern.astKind == AST::ASTKind::ENUM_PATTERN);
    if (auto cp = DynamicCast<AST::ConstPattern>(&pattern)) {
        return GetConstPatternVal(*cp);
    } else {
        return Translator::GetEnumPatternID(GetRealEnumPattern(pattern));
    }
}

bool Translator::IsOptimizableTy(Ptr<AST::Ty> ty)
{
    return ty->IsInteger() || ty->IsRune();
}

bool Translator::IsOptimizableEnumTy(Ptr<AST::Ty> ty)
{
    CJC_ASSERT(ty->IsEnum());
    auto selectorKind = Cangjie::CHIR::GetSelectorType(*StaticCast<AST::EnumTy>(ty)->decl);
    if (selectorKind == Type::TypeKind::TYPE_BOOLEAN) {
        return false;
    }
    auto enumDecl = StaticCast<AST::EnumDecl>(AST::Ty::GetDeclOfTy(ty));
    CJC_NULLPTR_CHECK(enumDecl);
    if (enumDecl->hasEllipsis) {
        return false;
    }
    for (auto& ctor : enumDecl->constructors) {
        if (auto funcTy = DynamicCast<AST::FuncTy>(ctor->ty)) {
            CJC_ASSERT(!funcTy->paramTys.empty());
            if (!IsOptimizableTy(funcTy->paramTys[0])) {
                return false;
            }
        }
    }
    return true;
}

void Translator::TranslateMatchAsTable(const AST::MatchExpr& matchExpr, Ptr<Value> retVal)
{
    auto selectorTy = matchExpr.selector->ty;
    auto selectorVal = TranslateExprArg(*matchExpr.selector);
    SetSkipPrintWarning(selectorVal);
    // Previously checked that selector ty is integer, char or enum.
    auto enumDecl = DynamicCast<AST::EnumDecl>(AST::Ty::GetDeclOfTy(selectorTy));
    if (!enumDecl) {
        // NOTE: selector's ty is Rune, UInt, Int.
        TranslateTrivialMatchAsTable(matchExpr, selectorVal, retVal, 0);
    } else if (!enumDecl->hasArguments || WithoutEnumSubPattern(matchExpr)) {
        // Note: selector'ty is enum, and no enumPattern with param.
        TranslateTrivialMatchAsTable(
            matchExpr, GetEnumIDValue(selectorTy, selectorVal), retVal, enumDecl->constructors.size());
    } else {
        // NOTE: at least one enumPattern has one param, and use enumId as selector for first switch.
        // Furthermore using at most first sub-pattern's value as second switch.
        // It will generate up to one level of nested switch table.
        TranslateEnumPatternMatchAsTable(matchExpr, selectorVal, retVal);
    }
}

void Translator::TranslateTrivialMatchAsTable(
    const AST::MatchExpr& match, Ptr<Value> selectorVal, Ptr<Value> retVal, size_t countOfPatterns)
{
    // NOTE: in current case, the pattern must be constant pattern or enum pattern without param(varOrEnum pattern).
    std::map<uint64_t, Block*> indexToBodies;
    auto baseBlock = currentBlock;
    auto endBlock = CreateBlock();
    Block* defaultBlock = endBlock;
    bool isStillReachable = true;
    ScopeContext context(*this);
    for (auto& curCase : match.matchCases) {
        context.ScopePlus();
        auto currentBody = TranslateMatchCaseBody(*curCase->exprOrDecls, retVal, endBlock);
        CJC_ASSERT(!curCase->patterns.empty());
        if (!isStillReachable) {
            continue; // All cases after wildcard pattern are unreachable pattern which do no need predecessor.
        } else if (curCase->patterns[0]->astKind == AST::ASTKind::WILDCARD_PATTERN) {
            // Wildcard pattern in current case must exited on top level.
            // NOTE: only should to collect first visited wildcard pattern.
            defaultBlock = currentBody;
            const auto& loc = TranslateLocation(*curCase->patterns[0]);
            defaultBlock->SetDebugLocation(loc);
            isStillReachable = false;
            continue;
        }
        // Store pattern value to block relation.
        for (auto& pattern : curCase->patterns) {
            auto patternVal = GetJumpablePatternVal(*pattern);
            const auto& loc = TranslateLocation(*pattern);
            currentBody->SetDebugLocation(loc);
            if (auto [_, success] = indexToBodies.emplace(patternVal, currentBody); !success) {
                continue;
            }
            CJC_ASSERT(!indexToBodies.empty());
            // Enum has limited number of patterns when all of them have been filled, rest cases are unreachable.
            // NOTE: do not check reachability for integer and char.
            if (indexToBodies.size() == countOfPatterns) {
                isStillReachable = false;
            }
        }
    }
    auto [indexes, blocks] = ConvertFromMapToVectors(indexToBodies);
    if (defaultBlock == endBlock) {
        // Exhaustive cases, default block will not be reached, just set it to first valid case block.
        defaultBlock = blocks[0];
    }
    currentBlock = baseBlock;
    Type* targetType;
    if (auto enumTy = DynamicCast<AST::EnumTy>(match.selector->ty)) {
        targetType = GetSelectorType(*enumTy);
    } else {
        targetType = builder.GetUInt64Ty();
    }
    auto castedVal =
        TypeCastOrBoxIfNeeded(*selectorVal, *targetType, selectorVal->GetDebugLocation(), false);
    const auto& loc = TranslateLocation(match);
    CreateAndAppendTerminator<MultiBranch>(loc, castedVal, defaultBlock, indexes, blocks, baseBlock);
    currentBlock = endBlock;
}

void Translator::TranslateEnumPatternMatchAsTable(const AST::MatchExpr& match, Ptr<Value> enumVal, Ptr<Value> retVal)
{
    bool isStillReachable = true;
    auto baseBlock = currentBlock;
    auto endBlock = CreateBlock();
    auto firstDefaultBlock = endBlock;
    auto firstSelectorVal = GetEnumIDValue(match.selector->ty, enumVal);
    EnumMatchInfo matchInfo;
    size_t wildcardIdx = match.matchCases.size();
    ScopeContext context(*this);
    for (size_t i = 0; i < match.matchCases.size(); ++i) {
        context.ScopePlus();
        auto& matchCase = *match.matchCases[i];
        CJC_ASSERT(!matchCase.patterns.empty());
        if (!isStillReachable) {
            continue; // All cases after wildcard pattern are unreachable pattern which do no need predecessor.
        } else if (matchCase.patterns[0]->astKind == AST::ASTKind::WILDCARD_PATTERN) {
            // Wildcard pattern in current case can only exited on top level.
            isStillReachable = false;
            // NOTE: only should to collect first visited wildcard pattern.
            // The sub pattern may contains varPattern, translate when binding second level table.
            firstDefaultBlock = TranslateMatchCaseBody(*matchCase.exprOrDecls, retVal, endBlock);
            const auto& loc = TranslateLocation(matchCase.patterns[0]->begin, matchCase.patterns[0]->end);
            firstDefaultBlock->SetDebugLocation(loc);
            wildcardIdx = i;
            continue;
        }
        // Store pattern value to block relation.
        for (auto& pattern : matchCase.patterns) {
            CollectEnumPatternInfo(*pattern, matchInfo, i);
        }
    }
    // Create branch to first level switch.
    auto [indexes, blocks] = ConvertFromMapToVectors(matchInfo.firstSwitchBlocks);
    if (firstDefaultBlock == endBlock) {
        // Exhaustive cases, default block will not be reached, just set it to first valid case block.
        firstDefaultBlock = blocks[0];
    }
    CreateAndAppendTerminator<MultiBranch>(firstSelectorVal, firstDefaultBlock, indexes, blocks, baseBlock);

    // Create each second switch block.
    auto branchInfos = TranslateSecondLevelAsTable(matchInfo, enumVal, firstDefaultBlock);
    // Finally create goto from case's true block to caseBody block.
    for (size_t i = 0; i < match.matchCases.size(); ++i) {
        if (i == wildcardIdx) {
            continue;
        }
        auto bodyBlock = TranslateMatchCaseBody(*match.matchCases[i]->exprOrDecls, retVal, endBlock);
        for (auto& block : std::as_const(branchInfos[i])) {
            CreateAndAppendTerminator<GoTo>(bodyBlock, block);
        }
    }
    currentBlock = endBlock;
}

void Translator::CollectEnumPatternInfo(const AST::Pattern& pattern, EnumMatchInfo& info, size_t caseId)
{
    // When selector type is enum, the pattern must be varOrEnum or enum pattern.
    auto& enumPattern = GetRealEnumPattern(pattern);
    auto patternVal = GetEnumPatternID(enumPattern);
    // Collect block for second level switch table.
    // When enum pattern does not have sub-pattern, block is the case body block store as nullptr.
    // Otherwise create new block for each enum id only once.
    auto [blockIt, succ] = info.firstSwitchBlocks.emplace(patternVal, nullptr);
    if (succ) {
        const auto& loc = TranslateLocation(pattern);
        blockIt->second = CreateBlock();
        blockIt->second->SetDebugLocation(loc);
    }
    SecondSwitchInfo switchInfo{enumPattern, caseId};
    auto& secondSwitchMap = info.indexToBodies[patternVal];
    if (enumPattern.patterns.empty()) {
        info.innerDefaultInfos[patternVal].emplace_back(switchInfo);
        return;
    }
    auto& secondValPattern = *enumPattern.patterns[0];
    if (secondValPattern.astKind == AST::ASTKind::WILDCARD_PATTERN) {
        info.innerDefaultInfos[patternVal].emplace_back(switchInfo);
    } else if (auto found = info.innerDefaultInfos.find(patternVal);
               found != info.innerDefaultInfos.end() && enumPattern.patterns.size() != 1) {
        // If enum has more than one sub-patterns, and the wildcard pattern of second switch value is existed,
        // the current case should be added to the table of default matching case.
        info.innerDefaultInfos[patternVal].emplace_back(switchInfo);
    } else {
        CJC_ASSERT(secondValPattern.astKind == AST::ASTKind::CONST_PATTERN);
        auto secondVal = GetConstPatternVal(StaticCast<AST::ConstPattern>(secondValPattern));
        secondSwitchMap[secondVal].emplace_back(switchInfo);
    }
}

std::unordered_map<size_t, std::vector<Ptr<Block>>> Translator::TranslateSecondLevelAsTable(
    const EnumMatchInfo& info, Ptr<Value> enumVal, Ptr<Block> firstDefaultBlock)
{
    std::unordered_map<size_t, std::vector<Ptr<Block>>> blockBranchInfos;
    // Create each second switch block.
    for (auto& [firstIdx, secondMap] : info.indexToBodies) {
        auto firstLevelBlock = info.firstSwitchBlocks.at(firstIdx);
        CJC_NULLPTR_CHECK(firstLevelBlock);
        auto found = info.innerDefaultInfos.find(firstIdx);
        // When there is no default pattern on first sub-pattern, set default as previous default block.
        // When only found one and enum does not have sub-pattern, create direct goto.
        // Otherwise generate all collected patterns for other enum sub-patterns.
        Block* secondDefaultBlock;
        if (found == info.innerDefaultInfos.end()) {
            secondDefaultBlock = firstDefaultBlock;
        } else if (!found->second.empty() && found->second[0].ep.patterns.empty()) {
            blockBranchInfos[found->second[0].caseId].emplace_back(firstLevelBlock);
        } else {
            // When 'secondMap' is emtpy, second selector is wildcard,
            // we need pass 'firstLevelBlock' as start block of second table detail.
            secondDefaultBlock = secondMap.empty() ? firstLevelBlock : CreateBlock();
            TranslateSecondLevelTable(firstDefaultBlock, secondDefaultBlock, enumVal, found->second, blockBranchInfos);
        }
        if (secondMap.empty()) {
            continue;
        }
        currentBlock = firstLevelBlock;
        CJC_ASSERT(!secondMap.empty() && !secondMap.begin()->second.empty());
        auto& enumPattern = secondMap.begin()->second.front().ep;
        CJC_ASSERT(!enumPattern.patterns.empty());
        auto& firstPattern = *enumPattern.patterns[0];
        PrintDebugMessage(opts.chirDebugOptimizer, enumPattern);
        CJC_ASSERT(firstPattern.ty->IsInteger() || firstPattern.ty->IsRune());
        auto selectorTy = TranslateType(*firstPattern.ty);
        auto enumValueTuple = CastEnumValueToConstructorTupleType(enumVal, enumPattern);
        auto secondSelectVar =
            CreateAndAppendExpression<Field>(selectorTy, enumValueTuple, std::vector<uint64_t>{1}, currentBlock)
                ->GetResult();
        std::vector<uint64_t> indexes;
        std::vector<Block*> blocks;
        for (auto& [index, infos] : secondMap) {
            indexes.emplace_back(index);
            auto block = CreateBlock();
            blocks.emplace_back(block);
            TranslateSecondLevelTable(firstDefaultBlock, block, enumVal, infos, blockBranchInfos);
        }
        currentBlock = firstLevelBlock;
        CreateAndAppendTerminator<MultiBranch>(
            TypeCastOrBoxIfNeeded(
                *secondSelectVar, *builder.GetUInt64Ty(), secondSelectVar->GetDebugLocation(), false),
            secondDefaultBlock, indexes, blocks, firstLevelBlock);
    }
    return blockBranchInfos;
}

void Translator::TranslateSecondLevelTable(Ptr<Block> endBlock, const Ptr<Block> tableBlock, const Ptr<Value> enumVal,
    const std::vector<SecondSwitchInfo>& infos, std::unordered_map<size_t, std::vector<Ptr<Block>>>& blockBranchInfos)
{
    currentBlock = tableBlock;
    bool hasGotoBase = false;
    CJC_ASSERT(!infos.empty());
    CJC_ASSERT(!infos[0].ep.patterns.empty());
    bool isWildcardPattern = infos[0].ep.patterns[0]->astKind == AST::ASTKind::WILDCARD_PATTERN;
    for (size_t idx = 0; idx < infos.size(); ++idx) {
        const auto& current = infos[idx];
        // When sub-pattern size is exactly 1, 'tableBlock' should be considered as 'trueBlock'.
        // NOTE: the 'tableBlock' can only be used as 'trueBlock' once.
        auto trueBlock = !hasGotoBase && current.ep.patterns.size() == 1 ? tableBlock.get() : CreateBlock();
        hasGotoBase = hasGotoBase || current.ep.patterns.size() == 1;
        auto falseBlock = CreateBlock();
        std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>> queue;
        auto elementTys = StaticCast<AST::FuncTy>(current.ep.constructor->ty)->paramTys;
        // When first pattern is wildcard and current pattern is not, we need to check from first sub-pattern,
        // otherwise only need to start from second sub-pattern.
        size_t start = isWildcardPattern && current.ep.patterns[0]->astKind != AST::ASTKind::WILDCARD_PATTERN ? 0 : 1;
        for (size_t i = start; i < current.ep.patterns.size(); ++i) {
            auto elementTy = TranslateType(*elementTys[i]);
            auto enumValueTuple = CastEnumValueToConstructorTupleType(enumVal, current.ep);
            auto elementVal =
                CreateAndAppendExpression<Field>(elementTy, enumValueTuple, std::vector<uint64_t>{1 + i}, currentBlock)
                    ->GetResult();
            // 1 + i: plus enum ctor id
            queue.emplace(std::make_pair(current.ep.patterns[i].get(), elementVal));
        }
        auto newBlock = CreateBlock();
        while (!queue.empty()) {
            const DebugLocation& originLoc =
                TranslateLocation(current.ep.ctxExpr->begin, current.ep.ctxExpr->end);
            // Typecast of varPattern must be generated at the real trueBlock.
            auto cond = DispatchingPattern(queue, {trueBlock, newBlock}, originLoc);
            if (cond) {
                auto nextBlock = queue.empty() ? trueBlock : newBlock;
                CreateAndAppendTerminator<Branch>(cond, nextBlock, falseBlock, currentBlock);
                currentBlock = nextBlock;
            } else if (queue.empty()) {
                CreateAndAppendTerminator<GoTo>(trueBlock, currentBlock);
            }
            if (cond && !queue.empty()) {
                newBlock = CreateBlock();
            }
        }
        newBlock->RemoveSelfFromBlockGroup();
        // Create 'GOTO' for trueBlock at callee for correct binding of varPattern.
        blockBranchInfos[current.caseId].emplace_back(trueBlock);
        if (idx == infos.size() - 1) {
            CreateAndAppendTerminator<GoTo>(endBlock, falseBlock);
        } else {
            currentBlock = falseBlock;
        }
    }
}
