// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares after-typecheck Java interop stage:
 * desugaring of type checks (`is`-expr) and typecasts (`as`-expr) including pattern-matching.
 * This stage is required when checking/casting to Java interop type (any `JObject` successor).
 */
#ifndef CANGJIE_SEMA_AFTER_TYPECHECK_NATIVE_FFI_JAVA_DESUGAR_TYPE_CHECKING_AND_CASTING
#define CANGJIE_SEMA_AFTER_TYPECHECK_NATIVE_FFI_JAVA_DESUGAR_TYPE_CHECKING_AND_CASTING

#include "AfterTypeCheckStage.h"
#include "InteropLibBridge.h"
#include "Utils.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie::Native::FFI::Java {
using namespace Interop::Java;

/**
 * Desugar `as`, `is` where type operand is Java interop type
 */
class DesugarTypeCheckingAndCasting : public AfterTypeCheckStage {
public:
    explicit DesugarTypeCheckingAndCasting(
        InteropLibBridge& ilib,
        DiagnosticEngine& diag,
        Interop::Java::Utils& utils);
protected:
    void Process(AfterTypeCheckContext& ctx) override;
private:
    /**
     * Transforms `x is T` expressions, where T is Java class into and x is class or interface:
     *
     * match (x) {
     *   case x : JObject =>
     *     Java_CFFI_isInstanceOf(Java_CFFI_get_env(), x.javaref, [Name of T Java class])
     *   case _ => false
     * }
     */
    void DesugarIsExpression(AST::IsExpr& ie) const;

    /**
     * Transforms `x as T` expressions, similarly to `is` above:
     *
     * In case of T is JavaMirror:
     * match (x) {
     *   case x : JObject =>
     *     match (Java_CFFI_isInstanceOf(Java_CFFI_get_env(), x.javaref, [Name of T Java class])) {
     *       case true =>
     *         (When T is JavaMirror) Some(T(x.javaref))
     *         (When T is JavaImpl) Java_CFFI_getFromRegistryByObj<T>(Java_CFFI_get_env(), x.javaref)
     *       case false => None
     *     }
     *   case _ => None
     * }
     * }
     */
    void DesugarAsExpression(AST::AsExpr& ae) const;

    /**
     * Transforms case arm where Java class is used in type patterns
     *
     * `case (.. (xi : Ti) ..) where guard => ...`
     * where Ti is Java class. Gets replaced with:
     * case (.. (xi : JObject) ..) where IsInstanceOf(xi.javaref, Ti) && .. && guard => {
     *  (.. let xi$Casted = T(x.javaref) ..)
     *  // All references to xi are replaces with references to xi$Casted
     *  ...
     * }
     *
     * Guard also might use xi variables, so casted variables are added to guard too and
     * all references are replaced
     */
    void DesugarMatchCase(AST::MatchCase& matchCase) const;

    /**
     * Transforms let pattern (let Some(a) = b), where type pattern with Java class is used
     *
     * let (... (xi : Ti) ...) = ...
     * where Ti is Java class. Gets replaced with:
     * let ( ... (xi : JObject) ... ) = ... && IsInstanceOf(xi, Ti) && ...
     */
    void DesugarLetPattern(AST::LetPatternDestructor& letPat) const;

    OwnedPtr<AST::Expr> CreateIsInstanceCall(
        Ptr<AST::VarDecl> jObjectVar, AST::ModalTy classTy, Ptr<AST::File> curFile) const;
    OwnedPtr<AST::Expr> CreateJObjectCast(Ptr<AST::VarDecl> jObjectVar,
        Ptr<AST::ClassLikeDecl> castDecl, Ptr<AST::File> curFile) const;
    OwnedPtr<AST::Block> CastAndSubstituteVars(
        AST::Expr& expr, const std::vector<std::tuple<Ptr<AST::VarDecl>, AST::ModalTy>>& patternVars) const;

    InteropLibBridge& ilib;
    DiagnosticEngine& diag;
    Interop::Java::Utils& utils;
};

}

#endif // CANGJIE_SEMA_AFTER_TYPECHECK_NATIVE_FFI_JAVA_DESUGAR_TYPE_CHECKING_AND_CASTING
