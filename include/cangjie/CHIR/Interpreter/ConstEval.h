// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the constant evaluation pass using the interpreter
 */

#ifndef CANGJIE_CHIR_INTERP_CONST_EVAL_H
#define CANGJIE_CHIR_INTERP_CONST_EVAL_H

#include <iostream>

#include <cangjie/CHIR/IR/CHIRBuilder.h>
#include <cangjie/CHIR/IR/Expression/Terminator.h>
#include <cangjie/CHIR/Interpreter/BCHIR.h>
#include <cangjie/CHIR/Interpreter/BCHIRInterpreter.h>
#include <cangjie/CHIR/Interpreter/BCHIRLinker.h>
#include <cangjie/CHIR/Interpreter/InterpreterValueUtils.h>
#include <cangjie/CHIR/IR/Package.h>
#include <cangjie/Frontend/CompilerInstance.h>
#include <cangjie/Option/Option.h>

namespace Cangjie::CHIR::Interpreter {

class IVal2CHIR {
public:
    IVal2CHIR(CHIRBuilder& chirBuilder, const Bchir& bchir, const Package& package)
        : chirBuilder(chirBuilder), bchir(bchir), package(package)
    {
    }
    IVal2CHIR(const IVal2CHIR&) = delete;
    IVal2CHIR& operator=(const IVal2CHIR&) = delete;

    // Returns a Constant that represents the same value as `val`.
    // Returns nullptr if `val` does not have an equivalent constant.
    Constant* TryConvertToConstant(Type& ty, const IVal& val, Block& parent);
    // Converts `val` to chir that produces an equivalent value.
    // Returns the value for the expression that holds the full value.
    // Returns nullptr if the value cannot be converted because the type is
    // unsupported or if it references a function that has not been imported.
    Value* ConvertToChir(Type& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent);

private:
    // Dispatch for the kinds that carry a constant representation beyond the
    // plain literals of TryConvertToConstant. Kinds without one return nullptr.
    Value* ConvertNonConstantToChir(
        Type& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent);
    Value* ConvertUnfoldablePrimitiveToChir(Type& ty);
    Value* ConvertUnfoldableOtherToChir(Type& ty);
    Value* ConvertStringToChir(
        Type& ty, const ITuple& val, const std::function<void(Expression*)>& insertExpr, Block& parent);
    Value* ConvertTupleToChir(Type& ty, const ITuple& val, std::function<void(Expression*)>& insertExpr, Block& parent);
    Value* ConvertEnumToChir(
        EnumType& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent);
    // Trivial enums carry the selector value only; payload enums decode to one
    // [selector, param...] tuple. Both return null for non-decodable values.
    Value* ConvertTrivialEnumToChir(EnumType& ty, Type* selectorTy, Block& parent, const IVal& val,
        std::function<void(Expression*)>& insertExpr);
    Value* ConvertPayloadEnumToChir(
        EnumType& ty, Type* selectorTy, Block& parent, const IVal& val, std::function<void(Expression*)>& insertExpr);
    // Constant of a class reference; other reference kinds are not supported
    // by const eval and return nullptr in ConvertRefToChir.
    Value* ConvertClassRefToChir(
        RefType& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent);
    Value* ConvertRefToChir(RefType& ty, const IVal& val, std::function<void(Expression*)>& insertExpr, Block& parent);
    Value* ConvertFuncToChir(const FuncType& ty, const IFunc& val);
    Value* ConvertArrayToChir(
        VArrayType& ty, const IArray& val, std::function<void(Expression*)>& insertExpr, Block& parent);

    ClassType* FindClassType(const std::string& mangledName);

    CHIRBuilder& chirBuilder;
    const Bchir& bchir;
    const Package& package;
};

class ConstEvalPass {
public:
    explicit ConstEvalPass(CompilerInstance& ci, CHIRBuilder& builder, SourceManager& sourceManager,
        const Cangjie::GlobalOptions& opts, DiagnosticEngine& diag)
        : ci(ci), builder(builder), sourceManager(sourceManager), opts(opts), diag(diag)
    {
    }
    ConstEvalPass(const ConstEvalPass&) = delete;
    ConstEvalPass& operator=(const ConstEvalPass&) = delete;

    // Evaluates constants (variables declared with `const`) and simplifies
    // their intializers.
    void RunOnPackage(Package& package,
        const std::vector<CHIR::Function*>& initFuncsForConstVar, std::vector<Bchir>& bchirPackages);

private:
    void RunInterpreter(Package& package, std::vector<Bchir>& bchirPackages,
        const std::vector<CHIR::Function*>& initFuncsForConstVar,
        std::function<void(Package&, BCHIRInterpreter&, BCHIRLinker&)> onSuccess);
    void ReplaceGlobalConstantInitializers(Package& package, BCHIRInterpreter& interpreter, BCHIRLinker& linker);
    void RemoveConstructorCalls(const Value& value);
    std::optional<BlockGroup*> CreateNewInitializer(Function& oldInitializer,
        const BCHIRInterpreter& interpreter, const BCHIRLinker& linker, const Package& package);

    void PrintDebugMessage(
        const DebugLocation& loc, const Function& oldInit, const std::optional<BlockGroup*>& newInit) const;

    CompilerInstance& ci;
    CHIRBuilder& builder;
    SourceManager& sourceManager;
    const Cangjie::GlobalOptions& opts;
    DiagnosticEngine& diag;
};

} // namespace Cangjie::CHIR::Interpreter

#endif
