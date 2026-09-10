// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Usefulness checking related classes.
 *
 * We adopt the algorithm described in Luc Maranget's paper, warnings for pattern matching, to do exhaustiveness
 * and reachable checking. And some ideas are from Rust's usefulness checking module.
 *
 * The algorithm should be called after type checking/inferring, and it requires the type information to work.
 * The core of the algorithm is the concept of "usefulness". Informally, the term usefulness can be viewed as
 * the reachable. Please refer to the paper if you want a formal definition. The exhaustive and reachable
 * checking can be solved by usefulness checking. And we stick to use the term "usefulness", which is consistent
 * with the paper.
 *
 * Suppose that p_1, p_2, ..., p_n are all the match arms given by a user.
 *        match (x) {
 *            case p_1 => {}
 *            case p_2 => {}
 *            case ... => {}
 *            case p_n => {}
 *        }
 * 1. Reachable checking: In order to check if p_i (1 <= i <= n) is reachable, suppose that p_1, ..., p_{i-1} have
 *    been checked and they are all reachable. If q is useful, which means q is reachable, so far so good;
 *    On the other hand, if q is not useful, the compiler should report that q is unreachable.
 * 2. Exhaustiveness checking: We append a wildcard to the end and check the usefulness of the wildcard.
 *        match (x) {
 *            case p_1 => {}
 *            case p_2 => {}
 *            case ... => {}
 *            case p_n => {}
 *            case _   => {} // The wildcard is inserted virtually
 *        }
 *    If the wildcard is useful, there must be cases not covered by p_1, p_2, ..., p_n, and the match expression is
 *    nonexhaustive. A diagnose message should be reported.
 *    Otherwise, the match expression is exhaustive.
 * 3. Report missed patterns: p_i (1 <= i <= n) is useful, if and only if there are cases that p_i could match but
 *    p_1, ..., p_{i-1} couldn't. These cases are the "witnesses" of the usefulness of p_i.
 *    In order to report missed patterns, we find the witnesses of the virtually inserted wildcard described before.
 *    They are the uncovered cases.
 * In fact, we use FindWitnesses() to check the usefulness: a match arm is useful iff it has witnesses.
 */

#include "PatternUsefulness.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "TypeCheckUtil.h"

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
using namespace AST;

namespace {
bool IsValidTy(ModalTy ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return false;
    }
    if (ty->IsEnum()) {
        auto& enumTy = static_cast<EnumTy&>(*ty);
        if (!enumTy.decl) {
            return false;
        }
        for (auto& ctor : enumTy.decl->constructors) {
            if (!ctor || !ctor->GetTy().IsCorrect()) {
                return false;
            }
        }
    }
    return true;
}

inline bool IsSuperTypeForFFI(ModalTy ty)
{
    return ty->IsCType();
}

template <typename T> bool IsSealedLikeCommon(const T& ty)
{
    CJC_NULLPTR_CHECK(ty.declPtr);
    const auto& decl = *ty.declPtr;
    if (decl.TestAttr(Attribute::SEALED) || decl.TestAttr(Attribute::PRIVATE)) {
        return true;
    }
    return false;
}

bool HasVisibleInit(const ClassDecl& cd)
{
    CJC_NULLPTR_CHECK(cd.body);
    return Utils::In(cd.body->decls, [](auto& decl) {
        CJC_NULLPTR_CHECK(decl);
        return decl->TestAttr(Attribute::CONSTRUCTOR) && decl->TestAnyAttr(Attribute::PUBLIC, Attribute::PROTECTED);
    });
}

bool IsSealedLike(ModalTy ty)
{
    if (ty->IsClass()) {
        auto& classTy = StaticCast<const ClassTy>(*ty);
        CJC_NULLPTR_CHECK(classTy.declPtr);
        return IsSealedLikeCommon(classTy) || !HasVisibleInit(*classTy.declPtr);
    }
    if (ty->IsInterface()) {
        return IsSealedLikeCommon(StaticCast<const InterfaceTy>(*ty));
    }
    // Other types cannot be inherited, they are all sealed-like.
    return true;
}

Ptr<ClassLikeTy> AsSealedLikeClassLikeTy(ModalTy ty)
{
    if (!ty->IsClassLike()) {
        return nullptr;
    }
    ClassLikeTy& clt = StaticCast<ClassLikeTy>(*ty);
    // Generic types are not supported so far.
    if (!clt.typeArgs.empty()) {
        return nullptr;
    }
    if (IsSealedLike(ty)) {
        return &clt;
    }
    return nullptr;
}

bool LitConstExprEq(const LitConstExpr& left, const LitConstExpr& right)
{
    if (left.kind != right.kind) {
        return false;
    }
    switch (left.kind) {
        case LitConstKind::UNIT:
        case LitConstKind::NONE: {
            return true;
        }
        case LitConstKind::BOOL: {
            return left.constNumValue.asBoolean == right.constNumValue.asBoolean;
        }
        case LitConstKind::INTEGER:
        case LitConstKind::RUNE_BYTE: {
            return left.constNumValue.asInt.Uint64() == right.constNumValue.asInt.Uint64();
        }
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
        case LitConstKind::FLOAT: {
            return left.constNumValue.asFloat.value - right.constNumValue.asFloat.value == 0;
        }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        case LitConstKind::RUNE:
        case LitConstKind::STRING:
        case LitConstKind::JSTRING: {
            return left.stringValue == right.stringValue;
        }
        default:
            CJC_ASSERT(false);
            return false;
    }
}

enum class ConstructorKind {
    UNIT,
    BOOLEAN,                // (bool)
    NON_EXHAUSTIVE_LITERAL, // (const LitConstExpr&), i.e., String, Rune, Float, ...
    ENUM,                   // (const std::string&, ModalTy), the constructor of enum
    NON_EXHAUSTIVE_ENUM,
    TUPLE,
    TYPE, // (ModalTy)
    WILDCARD,
    MISSING, // A special wildcard to cover non-exhaustive and type patterns
    OR,      // patterns separated by vertical bars
    INVALID,
};

struct EnumConstructor {
    const std::string& identifier;
    ModalTy ty;

    EnumConstructor(const std::string& identifier, ModalTy ty) : identifier(identifier), ty(ty) {}
};

using ConstructorUnion = std::variant<bool, std::reference_wrapper<const LitConstExpr>,
    std::reference_wrapper<const RefExpr>, ModalTy, EnumConstructor>;

class Constructor {
public:
    static Constructor Unit()
    {
        return Constructor(ConstructorKind::UNIT, 0);
    }
    static Constructor Boolean(bool boolCtor)
    {
        return Constructor(ConstructorKind::BOOLEAN, 0, boolCtor);
    }
    static Constructor NonExhaustive(const LitConstExpr& litCtor)
    {
        return Constructor(ConstructorKind::NON_EXHAUSTIVE_LITERAL, 0, litCtor);
    }
    static Constructor NonExhaustiveEnum()
    {
        return Constructor(ConstructorKind::NON_EXHAUSTIVE_ENUM, 0);
    }
    static Constructor Tuple(size_t numArgs)
    {
        return Constructor(ConstructorKind::TUPLE, numArgs);
    }
    static Constructor Enum(const Decl& decl, ModalTy ty)
    {
        if (!Ty::IsTyCorrect(ty)) {
            return Invalid();
        }
        size_t numArgs = 0;
        if (decl.astKind == ASTKind::FUNC_DECL) {
            numArgs = static_cast<const FuncDecl&>(decl).funcBody->paramLists[0]->params.size();
        }
        return Constructor(ConstructorKind::ENUM, numArgs, EnumConstructor(decl.identifier, ty));
    }
    static Constructor Type(ModalTy ty) { return Constructor(ConstructorKind::TYPE, 0, ty); }
    static Constructor Wildcard()
    {
        return Constructor(ConstructorKind::WILDCARD, 0);
    }
    static Constructor Or(size_t numArgs)
    {
        return Constructor(ConstructorKind::OR, numArgs);
    }
    static Constructor Missing()
    {
        return Constructor(ConstructorKind::MISSING, 0);
    }
    static Constructor Invalid()
    {
        return Constructor(ConstructorKind::INVALID, 0);
    }

    static Constructor FromLiteral(const Expr& expr)
    {
        if (expr.astKind != ASTKind::LIT_CONST_EXPR) {
            CJC_ABORT();
            return Constructor::Invalid();
        }
        return FromLitConstExpr(static_cast<const LitConstExpr&>(expr));
    }

    std::string ToString() const
    {
        switch (kind) {
            case ConstructorKind::UNIT: {
                return "()";
            }
            case ConstructorKind::BOOLEAN: {
                return std::get<bool>(ctor) ? "true" : "false";
            }
            case ConstructorKind::ENUM: {
                return std::get<EnumConstructor>(ctor).identifier;
            }
            case ConstructorKind::WILDCARD:
            case ConstructorKind::MISSING: {
                return std::string(TypeCheckUtil::WILDCARD_CHAR);
            }
            case ConstructorKind::NON_EXHAUSTIVE_LITERAL: {
                return std::get<std::reference_wrapper<const LitConstExpr>>(ctor).get().stringValue;
            }
            case ConstructorKind::NON_EXHAUSTIVE_ENUM:
                return "_";
            case ConstructorKind::TYPE: {
                return std::get<ModalTy>(ctor).String();
            }
            default: {
                return "invalid";
            }
        }
    }

    bool IsCoveredBy(TypeManager& typeManager, const Constructor& other) const
    {
        if (other.kind == ConstructorKind::WILDCARD || other.kind == ConstructorKind::MISSING) {
            return true;
        }
        switch (kind) {
            case ConstructorKind::WILDCARD:
            case ConstructorKind::MISSING: {
                return false;
            }
            case ConstructorKind::UNIT: {
                return other.kind == ConstructorKind::UNIT;
            }
            case ConstructorKind::BOOLEAN: {
                return other.kind == ConstructorKind::BOOLEAN && std::get<bool>(ctor) == std::get<bool>(other.ctor);
            }
            case ConstructorKind::NON_EXHAUSTIVE_LITERAL: {
                return other.kind == ConstructorKind::NON_EXHAUSTIVE_LITERAL &&
                    LitConstExprEq(std::get<std::reference_wrapper<const LitConstExpr>>(ctor),
                        std::get<std::reference_wrapper<const LitConstExpr>>(other.ctor));
            }
            case ConstructorKind::NON_EXHAUSTIVE_ENUM:
                return other.kind == ConstructorKind::WILDCARD || other.kind == ConstructorKind::NON_EXHAUSTIVE_ENUM;
            case ConstructorKind::ENUM: {
                // We don't have to worry about different enums with the same identifier, i.e., `A.E` and `B.E`.
                // The type checker guarantees their types must be the same enum.
                return other.kind == ConstructorKind::ENUM &&
                    std::get<EnumConstructor>(ctor).identifier == std::get<EnumConstructor>(other.ctor).identifier &&
                    std::get<EnumConstructor>(ctor).ty->IsEnum() ==
                    std::get<EnumConstructor>(other.ctor).ty->IsEnum() &&
                    std::get<EnumConstructor>(ctor).ty->typeArgs.size() ==
                    std::get<EnumConstructor>(other.ctor).ty->typeArgs.size();
            }
            case ConstructorKind::TUPLE: {
                return other.kind == ConstructorKind::TUPLE;
            }
            case ConstructorKind::TYPE: {
                return other.kind == ConstructorKind::TYPE &&
                    typeManager.IsSubtype(std::get<ModalTy>(ctor), std::get<ModalTy>(other.ctor), true, false);
            }
            default: {
                return false;
            }
        }
    }

    ConstructorKind Kind() const
    {
        return kind;
    }

    const ConstructorUnion& Ctor() const
    {
        return ctor;
    }

    size_t Arity() const
    {
        return arity;
    }

private:
    static Constructor FromLitConstExpr(const LitConstExpr& lit)
    {
        switch (lit.kind) {
            case LitConstKind::UNIT: {
                return Constructor::Unit();
            }
            case LitConstKind::BOOL: {
                return Constructor::Boolean(lit.stringValue == "true");
            }
            default: {
                return Constructor::NonExhaustive(lit);
            }
        }
    }

    Constructor(ConstructorKind kind, size_t arity) : kind(kind), arity(arity)
    {
    }
    Constructor(ConstructorKind kind, size_t arity, const ConstructorUnion& ctor) : kind(kind), ctor(ctor), arity(arity)
    {
    }

    ConstructorKind kind;
    ConstructorUnion ctor;
    size_t arity = 0; // number of arguments the constructor applys to
};

class DestructedPattern {
public:
    static DestructedPattern FromPattern(TypeManager& typeManager, ModalTy goalTy, Pattern& pattern)
    {
        if (!pattern.GetTy().IsCorrect() || !IsValidTy(pattern.GetTy())) {
            return {Constructor::Invalid(), {}, {TypeManager::GetInvalidTy()}, &pattern};
        }
        auto goal = typeManager.ReplaceThisTy(goalTy);
        // Now we are 100% sure that `pattern.ty` contains a type.
        switch (pattern.astKind) {
            case ASTKind::CONST_PATTERN: {
                return FromConstPattern(static_cast<ConstPattern&>(pattern));
            }
            case ASTKind::TUPLE_PATTERN: {
                return FromTuplePattern(typeManager, static_cast<TuplePattern&>(pattern), goal.Mode());
            }
            case ASTKind::ENUM_PATTERN: {
                return FromEnumPattern(typeManager, static_cast<EnumPattern&>(pattern), goal.Mode());
            }
            case ASTKind::TYPE_PATTERN: {
                return FromTypePattern(typeManager, goal, static_cast<TypePattern&>(pattern));
            }
            // Variable pattern behaves the same as a wildcard in the usefulness checking problem.
            // And we don't care about the name for binding.
            case ASTKind::WILDCARD_PATTERN:
            case ASTKind::VAR_PATTERN: {
                return {Constructor::Wildcard(), {}, pattern.GetTy().With(goal.Mode()), &pattern};
            }
            case ASTKind::VAR_OR_ENUM_PATTERN: {
                return FromPattern(typeManager, goal, *static_cast<VarOrEnumPattern&>(pattern).pattern);
            }
            default: {
                CJC_ABORT(); // unreachable
                return {Constructor::Invalid(), {}, {TypeManager::GetInvalidTy()}, &pattern};
            }
        }
    }

    static DestructedPattern FromPatterns(
        TypeManager& typeManager, ModalTy goalTy, std::vector<OwnedPtr<Pattern>>& patterns)
    {
        CJC_ASSERT(!patterns.empty());
        OwnedPtr<Pattern>& firstPattern = patterns.front();
        if (std::any_of(patterns.cbegin(), patterns.cend(), [](auto& pattern) {
                CJC_NULLPTR_CHECK(pattern);
                return !pattern->GetTy().IsCorrect() || !IsValidTy(pattern->GetTy());
            })) {
            return {Constructor::Invalid(), {}, {TypeManager::GetInvalidTy()}, firstPattern.get()};
        }
        if (patterns.size() == 1) {
            return FromPattern(typeManager, goalTy, *firstPattern);
        } else {
            std::vector<DestructedPattern> subPatterns;
            std::transform(patterns.begin(), patterns.end(), std::back_inserter(subPatterns),
                [&typeManager, &goalTy](const OwnedPtr<Pattern>& pattern) {
                    CJC_NULLPTR_CHECK(pattern);
                    return FromPattern(typeManager, goalTy, *pattern);
                });
            return {Constructor::Or(subPatterns.size()), subPatterns, goalTy, firstPattern.get()};
        }
    }

    DestructedPattern(
        Constructor ctor, std::vector<DestructedPattern> subPatterns, ModalTy goalTy, Ptr<Pattern> pattern)
        : ctor_(std::move(ctor)), subPatterns_(std::move(subPatterns)), goalTy_(goalTy), pattern_(pattern)
    {
    }

    std::string ToString() const
    {
        if (ctor_.Kind() == ConstructorKind::TUPLE) {
            return SubPatternsToString(subPatterns_);
        }
        if (ctor_.Kind() == ConstructorKind::ENUM) {
            if (ctor_.Arity() > 0) {
                // Enum constructor with fields.
                return ctor_.ToString() + SubPatternsToString(subPatterns_);
            }
            // Enum constructor without associated type.
            return ctor_.ToString();
        }
        return ctor_.ToString();
    }

    bool IsUnreachableTypePattern(TypeManager& typeManager) const
    {
        if (ctor_.Kind() != ConstructorKind::TYPE) {
            return false;
        }
        AST::ModalTy patternTy = std::get<AST::ModalTy>(ctor_.Ctor());
        // Nothing type is always unreachable.
        if (patternTy->IsNothing()) {
            return true;
        }
        // Usually, a type pattern is unreachable,
        // if the type pattern does not have subtyping relationships with its goal type,
        // For example,
        //        match (x) { // x: Int64
        //             case _: Bool => ... // unreachable
        //         }
        // But there are exceptions:
        // 1. Generic types may be reachable after instantiated.
        //        func f<T>(x: T) {
        //            match (x) {
        //                case _: Int64 => ... // not (T <: Int64 or Int64 <: T), but is reachable in `f(0)`
        //            }
        //        }
        // 2. Non-sealed like types may be reachable
        //        package pkg
        //        public interface I {}
        //        public open class A {}
        //
        //        import pkg.*
        //        class B <: A & I {}
        //        func f(x: I) {
        //            match (x) {
        //                case _: A => ... // not (I <: A or A <: I), but is reachable
        //                case _: I => ...
        //            }
        //        }
        if (goalTy_->HasGeneric() || patternTy->HasGeneric() || !IsSealedLike(goalTy_) || !IsSealedLike(patternTy)) {
            return false;
        }
        bool goalIsSubtype = typeManager.IsSubtype(goalTy_, patternTy, true, false);
        bool patternIsSubtype = typeManager.IsSubtype(patternTy, goalTy_, true, false);

        return !goalIsSubtype && !patternIsSubtype;
    }

    bool AllWildcard() const
    {
        return ctor_.Kind() == ConstructorKind::WILDCARD ||
            (ctor_.Kind() == ConstructorKind::TUPLE &&
                std::all_of(subPatterns_.cbegin(), subPatterns_.cend(),
                    [](const DestructedPattern& subPattern) { return subPattern.AllWildcard(); }));
    }

    /**
     * The subroutine to check the patterns recursively.
     *
     * @param otherCtor must Intersects() with the Head().
     */
    std::vector<DestructedPattern> Specialize(const Constructor& otherCtor) const
    {
        if (ctor_.Kind() == ConstructorKind::WILDCARD) {
            return SpecializeWildcard(goalTy_, otherCtor);
        }
        return subPatterns_;
    }

    const Constructor& Ctor() const
    {
        return ctor_;
    }

    const std::vector<DestructedPattern>& SubPatterns() const
    {
        return subPatterns_;
    }

    AST::ModalTy GoalTy() { return goalTy_; }

    Ptr<AST::Pattern> Node()
    {
        return pattern_;
    }

private:
    static DestructedPattern FromConstPattern(ConstPattern& constPattern)
    {
        CJC_ASSERT(constPattern.GetTy().IsCorrect());
        return {Constructor::FromLiteral(*constPattern.literal), {}, constPattern.GetTy(), &constPattern};
    }

    static std::vector<DestructedPattern> SubPatternsFromPatterns(TypeManager& typeManager,
        const std::vector<AST::ModalTy>& goalTys, const std::vector<OwnedPtr<Pattern>>& patterns)
    {
        std::vector<DestructedPattern> subPatterns;
        CJC_ASSERT(goalTys.size() == patterns.size());
        for (size_t i = 0; i < goalTys.size(); i++) {
            CJC_ASSERT(Ty::IsTyCorrect(goalTys[i]));
            CJC_NULLPTR_CHECK(patterns[i]);
            (void)subPatterns.emplace_back(DestructedPattern::FromPattern(typeManager, goalTys[i], *patterns[i]));
        }
        return subPatterns;
    }

    static DestructedPattern FromTuplePattern(TypeManager& typeManager, TuplePattern& tuplePattern,
        ModalInfo targetMode)
    {
        CJC_ASSERT(tuplePattern.GetTy().IsCorrect() && tuplePattern.GetTy()->IsTuple());
        const TupleTy& tupleTy = StaticCast<TupleTy>(*tuplePattern.GetTy());
        auto tyArgs = tupleTy.typeArgs;
        for (size_t i{0}; i < tyArgs.size(); ++i) {
            tyArgs[i] = tyArgs[i].With(targetMode);
        }
        std::vector<DestructedPattern> subPatterns =
            SubPatternsFromPatterns(typeManager, tupleTy.typeArgs, tuplePattern.patterns);
        return {
            Constructor::Tuple(subPatterns.size()), subPatterns, tuplePattern.GetTy().With(targetMode), &tuplePattern};
    }

    static DestructedPattern FromEnumPattern(TypeManager& typeManager, EnumPattern& enumPattern, ModalInfo targetMode)
    {
        CJC_NULLPTR_CHECK(enumPattern.constructor);
        Ptr<const Decl> target = enumPattern.constructor->GetTarget();
        if (!target || !enumPattern.GetTy().IsCorrect()) {
            return {Constructor::Invalid(), {}, {TypeManager::GetInvalidTy()}, &enumPattern};
        }
        MultiTypeSubst m;
        typeManager.GenerateGenericMapping(m, *enumPattern.GetTy());
        ModalTy instTy = typeManager.GetBestInstantiatedTy({target->DataTy(), targetMode}, m);
        CJC_ASSERT(Ty::IsTyCorrect(instTy));
        Constructor ctor = Constructor::Enum(*target, instTy);
        if (instTy->IsFunc()) {
            const FuncTy& funcTy = StaticCast<const FuncTy&>(*instTy);
            return {ctor, SubPatternsFromPatterns(typeManager, funcTy.paramTys, enumPattern.patterns),
                enumPattern.GetTy().With(targetMode), &enumPattern};
        }
        return {ctor, {}, enumPattern.GetTy(), &enumPattern};
    }

    static DestructedPattern FromTypePattern(TypeManager& typeManager, AST::ModalTy goalTy, TypePattern& typePattern)
    {
        CJC_ASSERT(typePattern.GetTy().IsCorrect());
        // If `goalTy <: typePattern.GetTy()`, the type pattern can always be matched.
        // For example: In `match (x) { case _: ToString => ... }`, where `x: Int64`,
        // the type pattern is equivalent to a wildcard.
        // An exception is that `Nothing` is always unreachable, it will be handled by `IsUnreachableTypePattern`.
        if (!typePattern.GetTy()->IsNothing() && typeManager.IsSubtype(goalTy.Ty(), typePattern.GetTy(), true, false)) {
            return {Constructor::Wildcard(), {}, goalTy, &typePattern};
        }
        return {Constructor::Type(typePattern.GetTy().With(goalTy.Mode())), {}, goalTy, &typePattern};
    }

    static std::string SubPatternsToString(const std::vector<DestructedPattern>& subPatterns)
    {
        if (subPatterns.empty()) {
            return "()";
        }
        auto subPatternIter = subPatterns.cbegin();
        std::string result = "(" + (*subPatternIter).ToString();
        ++subPatternIter;
        while (subPatternIter != subPatterns.cend()) {
            result += ", " + (*subPatternIter).ToString();
            ++subPatternIter;
        }
        return result + ")";
    }

    static std::vector<DestructedPattern> SpecializeWildcard(AST::ModalTy ty, const Constructor& ctor)
    {
        std::vector<DestructedPattern> subPatterns;
        if (ctor.Kind() == ConstructorKind::TUPLE) {
            // Expand the wildcard, i.e., _ => (_, _, ..., _)
            CJC_ASSERT(AST::Ty::IsTyCorrect(ty));
            CJC_ASSERT(ty->IsTuple());
            CJC_ASSERT(ctor.Arity() == ty->typeArgs.size());
            for (size_t i = 0; i < ctor.Arity(); i++) {
                auto argTy = ty->TyArg(i);
                CJC_ASSERT(Ty::IsTyCorrect(argTy));
                subPatterns.push_back({Constructor::Wildcard(), {}, {argTy, ty.Mode()}, nullptr});
            }
            return subPatterns;
        }
        if (ctor.Kind() == ConstructorKind::ENUM) {
            AST::ModalTy ctorTy = std::get<EnumConstructor>(ctor.Ctor()).ty;
            if (ctorTy->IsEnum()) {
                // `ctor` doesn't have fields, e.g., `None`. The transform will not happen.
                // An empty `subPatterns` will be returned as we need.
                return subPatterns;
            }
            // Otherwise, `ctor` is a enum constructor with fields.
            CJC_ASSERT(ctorTy->IsFunc());
            const AST::FuncTy& ctorFuncTy = StaticCast<FuncTy>(*ctorTy);
            std::transform(ctorFuncTy.paramTys.cbegin(), ctorFuncTy.paramTys.cend(), std::back_inserter(subPatterns),
                [ty](auto paramTy) {
                    CJC_ASSERT(Ty::IsTyCorrect(paramTy));
                    return DestructedPattern(Constructor::Wildcard(), {}, paramTy.With(ty.Mode()), nullptr);
                });
            return subPatterns;
        }
        return subPatterns;
    }

    Constructor ctor_;
    std::vector<DestructedPattern> subPatterns_;
    ModalTy goalTy_;
    Ptr<Pattern> pattern_;
};

class PatternStack {
public:
    PatternStack()
    {
    }
    explicit PatternStack(const DestructedPattern& pattern)
    {
        stack.emplace_back(pattern);
    }

    bool IsEmpty() const
    {
        return stack.empty();
    }

    bool AllWildcard() const
    {
        return std::all_of(
            stack.cbegin(), stack.cend(), [](const DestructedPattern& pattern) { return pattern.AllWildcard(); });
    }

    size_t Size() const
    {
        return stack.size();
    }

    DestructedPattern& Head()
    {
        return stack.back();
    }

    PatternStack Specialize(const Constructor& ctor)
    {
        std::vector<DestructedPattern> newStack;
        // Copies the `stack` except the head (the last element) to `newStack`.
        std::copy(stack.cbegin(), stack.cend() - 1, std::back_inserter(newStack));
        std::vector<DestructedPattern> headSubPatterns = Head().Specialize(ctor);
        std::copy(headSubPatterns.crbegin(), headSubPatterns.crend(), std::back_inserter(newStack));
        return PatternStack(std::move(newStack));
    }

    /**
     * Apply the constructor on the PatternStack.
     *
     * This function is designed to recover the original patterns. For example, after applying the enum constructor
     * `A(Int64, Int64)` on a PatternStack `[1, 2, 3]`, we get a new PatternStack `[A(1, 2), 3]`.
     */
    PatternStack Apply(const Constructor& ctor, ModalTy ty) const
    {
        std::vector<DestructedPattern> subPatterns;
        CJC_ASSERT(Size() >= ctor.Arity());
        std::copy_n(stack.crbegin(), ctor.Arity(), std::back_inserter(subPatterns));
        std::vector<DestructedPattern> newStack;
        std::copy_n(stack.cbegin(), stack.size() - ctor.Arity(), std::back_inserter(newStack));
        newStack.emplace_back(DestructedPattern(ctor, subPatterns, ty, nullptr));
        return PatternStack(std::move(newStack));
    }

    /**
     * Expand the or-pattern.
     *
     * Returns a vector of the expanded PatternStacks. For example, `[true | false, 1, 2, 3]` will be expanded to:
     *     [
     *         [true, 1, 2, 3],
     *         [false, 1, 2, 3]
     *     ]
     */
    std::vector<PatternStack> ExpandOr()
    {
        CJC_ASSERT(!stack.empty());
        CJC_ASSERT(Head().Ctor().Kind() == ConstructorKind::OR);
        std::vector<PatternStack> results;
        std::transform(Head().SubPatterns().cbegin(), Head().SubPatterns().cend(), std::back_inserter(results),
            [this](const DestructedPattern& newHead) {
                std::vector<DestructedPattern> newStack;
                std::copy(this->stack.cbegin(), this->stack.cend() - 1, std::back_inserter(newStack));
                newStack.emplace_back(newHead);
                return PatternStack(std::move(newStack));
            });
        return results;
    }

private:
    explicit PatternStack(std::vector<DestructedPattern>&& stack) : stack(stack)
    {
    }

    std::vector<DestructedPattern> stack; // Stores `DestructedPattern`s reversely, i.e., the `Head()` is the last item.
};

class Matrix {
public:
    bool IsEmpty() const
    {
        return rows.empty();
    }

    bool HasSingleRowAndAllWildcard() const
    {
        return rows.size() == 1 && rows.front().AllWildcard();
    }

    bool MissingAll()
    {
        return Utils::All(rows, [](const PatternStack& row) { return row.IsEmpty(); });
    }

    bool HeadsAllWildcard()
    {
        return std::all_of(rows.begin(), rows.end(), [](PatternStack& row) {
            if (row.IsEmpty()) {
                return false;
            }
            return row.Head().AllWildcard();
        });
    }

    Matrix Specialize(TypeManager& typeManager, const Constructor& ctor)
    {
        Matrix newMatrix;
        for (auto& row : rows) {
            CJC_ASSERT(!row.IsEmpty());
            if (ctor.IsCoveredBy(typeManager, row.Head().Ctor())) {
                PatternStack newRow = row.Specialize(ctor);
                newMatrix.rows.emplace_back(newRow);
            }
        }
        return newMatrix;
    }

    void AppendRows(std::vector<PatternStack>&& newRows)
    {
        if (rows.empty()) {
            rows = std::move(newRows);
        } else {
            rows.reserve(rows.size() + newRows.size());
            std::move(std::begin(newRows), std::end(newRows), std::back_inserter(rows));
            newRows.clear();
        }
    }

private:
    std::vector<PatternStack> rows;
};

class UsefulnessChecker {
public:
    UsefulnessChecker(DiagnosticEngine& diag, TypeManager& typeManager) : diag(diag), typeManager_(typeManager)
    {
    }
    UsefulnessChecker(DiagnosticEngine& diag, TypeManager& typeManager, Matrix&& matrix)
        : diag(diag), typeManager_(typeManager), matrix_(matrix)
    {
    }

    /**
     * Find the witnesses of usefulness.
     *
     * There is an invariant in this function: the size of each returned PatternStacks is equal to the size of @param
     * vec.
     */
    std::vector<PatternStack> FindWitnesses(PatternStack& vec)
    {
        // Base case:
        if (vec.IsEmpty()) {
            if (matrix_.IsEmpty()) {
                return {PatternStack()};
            }
            return {};
        }
        if (matrix_.HasSingleRowAndAllWildcard()) {
            return {};
        }
        // Induction:
        DestructedPattern& head = vec.Head();
        if (head.IsUnreachableTypePattern(typeManager_)) {
            return {};
        }
        if (head.Ctor().Kind() == ConstructorKind::WILDCARD) {
            return FindWitnessesForWildcard(vec);
        }
        if (head.Ctor().Kind() == ConstructorKind::OR) {
            return FindWitnessesForExpandedOr(vec.ExpandOr());
        }
        if (head.Ctor().Kind() == ConstructorKind::TYPE) {
            PatternStack wildcard =
                PatternStack(DestructedPattern(Constructor::Wildcard(), {}, head.GoalTy(), nullptr));
            if (FindWitnessesForWildcard(wildcard).empty()) {
                return {};
            }
        }
        return FindWitnessesBySpecialization(vec, head.Ctor());
    }

    void AddWitnesses(std::vector<PatternStack>&& witnesses)
    {
        matrix_.AppendRows(std::move(witnesses));
    }

private:
    /**
     * Split the wildcard into a vector of possible constructors.
     *
     * Returns a single wildcard if the matrix is empty, otherwise, returns all the possible constructors. For
     * example: match (Some(true)) { case None => {}
     *     }
     * The missing cases are `Some(true)` and `Some(false)`, but `Some(_)` is preferred.
     */
    static std::vector<Constructor> SplitWildcard(
        TypeManager& typeManager, ModalTy ty, Matrix& matrix, bool isABIStable)
    {
        if (matrix.MissingAll()) {
            return {Constructor::Wildcard()};
        }
        if (auto sealedTy = AsSealedLikeClassLikeTy(ty); sealedTy && !IsSuperTypeForFFI(ty) && !isABIStable) {
            std::vector<Constructor> ctors;
            SplitWildcardForSealed(ctors, *sealedTy);
            return ctors;
        }
        if (!Ty::IsTyCorrect(ty)) {
            return {Constructor::Missing()};
        }
        if (ty->IsUnit()) {
            return {Constructor::Unit()};
        }
        if (ty->IsBoolean()) {
            return {Constructor::Boolean(false), Constructor::Boolean(true)};
        }
        if (ty->IsTuple()) {
            return {Constructor::Tuple(ty->typeArgs.size())};
        }
        if (ty->IsEnum()) {
            MultiTypeSubst m;
            typeManager.GenerateGenericMapping(m, *ty);
            std::vector<Constructor> ctors;
            auto& enumTy = StaticCast<EnumTy>(*ty);
            for (const auto& decl : enumTy.declPtr->constructors) {
                if (auto varDecl = DynamicCast<const VarDecl*>(decl.get()); varDecl) {
                    ctors.emplace_back(Constructor::Enum(*varDecl, ty));
                } else if (auto funcDecl = DynamicCast<const FuncDecl*>(decl.get()); funcDecl) {
                    ctors.emplace_back(
                        Constructor::Enum(*funcDecl, typeManager.GetBestInstantiatedTy(funcDecl->GetTy(), m)));
                }
            }
            if (enumTy.IsNonExhaustive()) {
                ctors.push_back(Constructor::NonExhaustiveEnum());
            }
            return ctors;
        }
        return {Constructor::Missing()};
    }

    static void SplitWildcardForSealed(std::vector<Constructor>& ctors, ClassLikeTy& sealedTy)
    {
        std::set<ModalTy, CmpTyByNameModal> directSubtypes(
            sealedTy.directSubtypes.cbegin(), sealedTy.directSubtypes.cend());
        for (ModalTy subTy : directSubtypes) {
            CJC_ASSERT(Ty::IsTyCorrect(subTy));
            if (auto subSealed = AsSealedLikeClassLikeTy(subTy); subSealed && !IsSuperTypeForFFI(subTy)) {
                SplitWildcardForSealed(ctors, *subSealed);
            } else {
                (void)ctors.emplace_back(Constructor::Type(subTy));
            }
        }
        if (sealedTy.IsClass()) {
            auto& ct = StaticCast<ClassTy&>(sealedTy);
            CJC_NULLPTR_CHECK(ct.declPtr);
            if (!ct.declPtr->TestAttr(Attribute::ABSTRACT)) {
                ctors.emplace_back(Constructor::Type(ModalTy(static_cast<Ty*>(&sealedTy))));
            }
        }
    }

    std::vector<PatternStack> FindWitnessesForWildcard(PatternStack& vec)
    {
        DestructedPattern& head = vec.Head();
        CJC_ASSERT(head.Ctor().Kind() == ConstructorKind::WILDCARD);
        if (matrix_.HeadsAllWildcard()) {
            // Avoid inspecting the sub-patterns if the matrix' heads are all wildcards.
            return FindWitnessesBySpecialization(vec, head.Ctor());
        }
        ModalTy goalTy = head.GoalTy();
        if (goalTy->IsTuple()) {
            // Expand the wildcard into a tuple of wildcards, i.e., _ => (_, _, ..., _), then call FindWitnesses for
            // the rewritted wildcards.
            Constructor tuple = Constructor::Tuple(goalTy->typeArgs.size());
            auto stack = vec.Specialize(tuple).Apply(tuple, goalTy);
            return FindWitnesses(stack);
        }
        // Split the wildcard into a series of constructors, call FindWitnesses for each constructor and concatenate
        // the witnesses together.
        std::vector<PatternStack> witnesses;
        auto ctors = SplitWildcard(typeManager_, goalTy, matrix_, keepABIStable);
        for (const Constructor& ctor : ctors) {
            for (const PatternStack& witness : FindWitnessesBySpecialization(vec, ctor)) {
                witnesses.emplace_back(witness);
            }
        }
        return witnesses;
    }

    std::vector<PatternStack> FindWitnessesBySpecialization(PatternStack& vec, const Constructor& ctor)
    {
        CJC_ASSERT(!vec.IsEmpty());
        std::vector<PatternStack> witnesses;
        UsefulnessChecker newChecker(diag, typeManager_, matrix_.Specialize(typeManager_, ctor));
        auto stack = vec.Specialize(ctor);
        for (const PatternStack& witness : newChecker.FindWitnesses(stack)) {
            witnesses.emplace_back(witness.Apply(ctor, vec.Head().GoalTy()));
        }
        return witnesses;
    }

    std::vector<PatternStack> FindWitnessesForExpandedOr(std::vector<PatternStack>&& vecs) const
    {
        UsefulnessChecker newChecker(diag, typeManager_, Matrix(matrix_));
        std::vector<Ptr<Node>> unreachables;
        std::vector<PatternStack> totalWitnesses;
        for (PatternStack& vec : vecs) {
            std::vector<PatternStack> witnesses = newChecker.FindWitnesses(vec);
            if (witnesses.empty()) {
                unreachables.emplace_back(vec.Head().Node());
            } else {
                std::copy(witnesses.cbegin(), witnesses.cend(), std::back_inserter(totalWitnesses));
                newChecker.AddWitnesses(std::move(witnesses));
            }
        }
        if (!totalWitnesses.empty()) {
            // If the or-pattern has witnesses (the match case is reachable), some of the sub-patterns can still be
            // unreachable. For example, `case true | true | false`, the `true` in the middle is unreachable.
            // We set each sub-pattern to be UNREACHABLE and report diagnoses.
            for (Ptr<Node> unreachable : unreachables) {
                CJC_NULLPTR_CHECK(unreachable);
                unreachable->EnableAttr(Attribute::UNREACHABLE);
                diag.DiagnoseRefactor(DiagKindRefactor::sema_unreachable_pattern, *unreachable);
            }
        }
        // Otherwise, every sub-pattern is unreachable and the or-pattern itself is unreachable.
        // There is no need to report unreachable for each sub-pattern.
        return totalWitnesses;
    }

    DiagnosticEngine& diag;
    TypeManager& typeManager_;
    Matrix matrix_;
    bool keepABIStable{true};
};
} // namespace

namespace PatternUsefulness {
bool CheckMatchExprHasSelectorExhaustivenessAndReachability(
    DiagnosticEngine& diag, TypeManager& typeManager, const MatchExpr& me)
{
    if (!me.selector || !me.selector->GetTy() || !IsValidTy(me.selector->GetTy())) {
        // Avoid exhaustive & reachable checking if fatal errors appeared.
        return true;
    }
    UsefulnessChecker checker(diag, typeManager);
    for (auto& mc : me.matchCases) {
        if (mc->patterns.empty()) {
            CJC_ABORT();
            continue;
        }
        for (auto& pattern : mc->patterns) {
            if (Ty::IsInitialTy(pattern->DataTy())) {
                pattern->SetTy(typeManager.TryGreedySubst(me.selector->GetTy()));
            }
        }
        // The PatternStack vec contains only one item in the beginning.
        PatternStack vec(DestructedPattern::FromPatterns(
            typeManager, typeManager.TryGreedySubst(me.selector->GetTy()), mc->patterns));
        std::vector<PatternStack> witnesses = checker.FindWitnesses(vec);
        if (!witnesses.empty()) {
            // Add the witnesses to the matrix only if the match case doesn't have guard.
            // Note that we should use `FindWitnesses` to check reachability even if it has a pattern guard.
            if (!mc->patternGuard) {
                checker.AddWitnesses(std::move(witnesses));
            }
        } else {
            mc->EnableAttr(Attribute::UNREACHABLE);
            diag.DiagnoseRefactor(DiagKindRefactor::sema_unreachable_pattern, *mc,
                MakeRange(mc->patterns.front()->begin, mc->patterns.back()->end));
        }
    }
    auto stack = PatternStack(
        DestructedPattern(Constructor::Wildcard(), {}, typeManager.TryGreedySubst(me.selector->GetTy()), nullptr));
    std::vector<PatternStack> witnesses = checker.FindWitnesses(stack);
    if (witnesses.empty()) {
        return true;
    }
    DiagnosticBuilder diagBuilder = diag.DiagnoseRefactor(DiagKindRefactor::sema_nonexhuastive_patterns, *me.selector);
    diagBuilder.AddMainHintArguments(me.selector->GetTy().String());
    for (PatternStack& witness : witnesses) {
        // Every witness has only one item in the final result of FindWitnesses()
        CJC_ASSERT(witness.Size() == 1);
        diagBuilder.AddNote(witness.Head().ToString() + " is not covered");
    }
    return false;
}
} // namespace PatternUsefulness
} // namespace Cangjie
