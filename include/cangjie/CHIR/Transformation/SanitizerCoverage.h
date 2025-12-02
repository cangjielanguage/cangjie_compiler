// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_SANITIZER_COVERAGE_H
#define CANGJIE_CHIR_TRANSFORMATION_SANITIZER_COVERAGE_H

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/Utils/DiagAdapter.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CHIR {
/**
 * CHIR Normal Pass: add sanitizer coverage code to certain code, such as compare, switch, etc.
 */
class SanitizerCoverage {
public:
    /**
     * @brief constructor to insert sanitizer coverage code.
     * @param option global options of cangjie inputs.
     * @param builder CHIR builder for generating IR.
     */
    explicit SanitizerCoverage(const GlobalOptions& option, CHIRBuilder& builder);

    /**
     * @brief Main process to insert sanitizer coverage code.
     * @param package package to do optimization.
     * @param diag reporter to print error or warning info.
     * @param isDebug flag whether print debug log.
     * @return flag whether error happens in package.
     */
    bool RunOnPackage(const Ptr<const Package>& package, DiagAdapter& diag, bool isDebug);

private:
    void RunOnFunc(const Ptr<Func>& func, bool isDebug);
    bool CheckSancovOption(DiagAdapter& diag) const;

    // entry for different sanitizer coverage option
    void InjectTraceForCmp(BinaryExpression& binary, bool isDebug);
    void InjectTraceForSwitch(MultiBranch& mb, bool isDebug);
    void InsertCoverageAheadBlock(Block& block, bool isDebug);
    void InjectTraceMemCmp(Expression& expr, bool isDebug);
    // for switch trace
    RawArrayAllocate* CreateArrayForSwitchCaseList(MultiBranch& multiBranch);
    Intrinsic* CreateRawDataAcquire(const Expression& dataList, Type& elementType) const;

    // for memory compare IR generator
    std::vector<Value*> GenerateCStringMemCmp(const std::string& fuzzName, Value& oper1, Value& oper2, Apply& apply);
    std::vector<Value*> GenerateStringMemCmp(const std::string& fuzzName, Value& oper1, Value& oper2, Apply& apply);
    std::vector<Value*> GenerateArrayCmp(const std::string& fuzzName, Value& oper1, Value& oper2, Apply& apply);
    std::pair<Value*, Value*> CastArrayListToArray(Value& oper1, Value& oper2, Apply& apply);
    Expression* CreateOneCPointFromList(Value& array, Apply& apply, Type& elementType, Type& startType);
    std::pair<std::string, std::vector<Value*>> GetMemFuncSymbols(Value& oper1, Value& oper2, Apply& apply);

    Expression* CreateMemCmpFunc(const std::string& intrinsicName, Type& paramsType, const std::vector<Value*>& params,
        const DebugLocation& loc, Block* parent);

    // create coverage call for different option
    std::vector<Expression*> GenerateCoverageCallByOption(const DebugLocation& loc, bool isDebug, Block* parent);
    std::vector<Expression*> GeneratePCGuardExpr(const DebugLocation& loc, bool isDebug, Block* parent);
    std::vector<Expression*> GenerateInline8bitExpr(const DebugLocation& loc, bool isDebug, Block* parent);
    std::vector<Expression*> GenerateInlineBoolExpr(const DebugLocation& loc, bool isDebug, Block* parent);
    std::vector<Expression*> GenerateStackDepthExpr(const DebugLocation& loc, bool isDebug, Block* parent);

    // create imported function or global var
    GlobalVar* GenerateGlobalVar(const std::string& globalVarName, const DebugLocation& loc, Type& globalType);
    GlobalVar* GetGlobalVar(const std::string& globalVarName);
    ImportedValue* GenerateForeignFunc(
        const std::string& globalFuncName, const DebugLocation& loc, Type& funcType, const std::string& packName = "");
    ImportedValue* GetImportedFunc(const std::string& mangledName);
    Func* CreateInitFunc(const std::string& name, FuncType& funcType, const DebugLocation& loc);

    // create init func
    void GenerateInitFunc(const Func& globalInitFunc, bool isDebug);
    void CreateTopLevelInitFunc(const std::vector<Func*>& initFuncs, const Func& globalInitFunc);
    Func* CreateArrayInitFunc(const std::string& initItemName, Type& initType);
    Func* CreatePCTableInitFunc();
    Intrinsic* CreateRawDataAcquire(Type& type, const std::vector<Value*>& list, Value& size, Block& block);

    void InitFuncBag(const Package& package);

    // option imported from system
    const GlobalOptions::SanitizerCoverageOptions& sanCovOption;
    // a list for pc table array
    std::vector<std::pair<std::string, DebugLocation>> pcArray;
    // function Bag imported from package for fuzz
    std::unordered_map<std::string, ImportedValue*> funcBag;
    // global var Bag imported needed by fuzz
    std::unordered_map<std::string, GlobalVar*> globalVarBag;
    // bb counter overall package
    int64_t bbCounter{0};

    std::string packageName;
    CHIRBuilder& builder;
};
} // namespace Cangjie::CHIR

#endif