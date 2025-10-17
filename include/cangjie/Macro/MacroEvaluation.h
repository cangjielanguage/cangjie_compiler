// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the MacroEvaluation class.
 */

#ifndef CANGJIE_MACROEVALUATION_H
#define CANGJIE_MACROEVALUATION_H

#include "cangjie/AST/Node.h"
#ifdef _WIN32
#include <windows.h>
// we need to undefine THIS, INTERFACE, and FASTCALL which are defined by MinGW
#if defined(THIS)
#undef THIS
#endif
#if defined(INTERFACE)
#undef INTERFACE
#endif
#if defined(interface)
#undef interface
#endif
#if defined(CONST)
#undef CONST
#endif
#if defined(GetObject)
#undef GetObject
#endif
#if defined(FASTCALL)
#undef FASTCALL
#endif
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Macro/MacroCommon.h"
#include "cangjie/Macro/MacroEvalMsgSerializer.h"
namespace Cangjie {
class MacroEvaluation {
public:
    MacroEvaluation(CompilerInstance* cIns, MacroCollector* mc, bool useChildProcess)
        : ci(cIns),
          macroCollector(mc),
          useChildProcess(useChildProcess)
    {
        InitThreadNum();
        if (useChildProcess) {
            CreateMacroSrvProcess();
        }
    }
    ~MacroEvaluation()
    {
    }
    const std::vector<std::string> GetVecOfGeneratedCodes()
    {
        return vecOfGeneratedCodes;
    }
    /**
     * @brief Evaluate macro in runtime, used by compiled-macro evaluation.
     */
    void Evaluate();

    /**
     * @brief Help evaluate macro, need used by interpreted evaluation directly.
     */
    void EvalMacros();

    /**
     * @brief Convert tokens to string for .macrocall file.
     *
     * @param tokens processed tokens after macro expand.
     * @param offset Indentation in file.
     * @return std::string results after converting.
     */
    std::string ConvertTokensToString(const TokenVector& tokens, int offset = 1);

    // Begin: for process isolation in lsp.
    void CreateMacroSrvProcess();
    void ExecuteEvalSrvTask();
    // End: for process isolation in lsp.

private:
    CompilerInstance* ci{nullptr};
    MacroCollector* macroCollector{nullptr};
    // Generated Tokens in string format for pretty print.
    std::vector<std::string> vecOfGeneratedCodes;

    std::vector<Position> escapePosVec = {};                // All escape token's pos.
    std::list<MacroCall*> pMacroCalls;                      // For multi-thread to evaluate macrocalls.
    std::vector<std::unique_ptr<MacroCall>> childMacCalls;  // For save child macrocalls.
    size_t threadNum = 0;                                   // Max num of threads.
    std::vector<bool> isThreadUseds;
    bool enableParallelMacro{false};
    std::unordered_map<std::string, bool> usedMacroPkgs;    // for compiled macro
    bool useChildProcess{false};

    // Begin: for process isolation in lsp.
    static MacroEvalMsgSerializer msgSlzer;
    std::list<std::unique_ptr<AST::MacroExpandDecl>> macDecls;
    std::list<std::unique_ptr<MacroCall>> macCalls;

    // client process
    bool SendMacroDefTask(const std::unordered_set<std::string>& macroLibs) const;
    void SendExitStgTask() const;
    void WaitMacroDefResult() const;
    bool SendMacroCallTask(MacroCall& call) const;
    void WaitMacroCallEvalResult(MacroCall& call) const;
    bool SendMacroCallsTask(std::list<MacroCall*>& calls) const;
    bool WaitMacroCallsEvalResult(std::list<MacroCall*>& calls) const;
    void DeserializeMacroCallsResult(
        std::list<MacroCall*>& calls, const std::list<std::vector<uint8_t>>& msgList) const;

    // srv process
    std::unique_ptr<AST::MacroExpandDecl> CreateMacroExpand(const MacroMsgFormat::MacroCall& callFmt) const;
    void DeSerializeMacroCall(const MacroMsgFormat::MacroCall& callFmt);
    bool SerializeAndNotifyResult(MacroCall& macCall) const;
    bool EvalMacroCallsAndWaitResult();
#if defined(__linux__) || defined(__APPLE__)
    void RunMacroSrv();
    void ExecMacroSrv(pid_t pid) const;
#endif
    bool FindDef(const std::vector<uint8_t>& msg) const; // find macro libs and opene lib handles in macro srv
    bool EvalMacroCall(std::vector<uint8_t>& msg); // EvalMacroCall in macro srv
    void ResetForNextEval();
    // End: for process isolation in isolation lsp.
    void InitThreadNum()
    {
        // Maxnum of threads: half of hardware_concurrency.
        threadNum = std::thread::hardware_concurrency() / 2;
        if (ci->invocation.globalOptions.enableParallelMacro && threadNum > 1) {
            isThreadUseds.resize(threadNum, false);
            enableParallelMacro = true;
        }
    }
    /**
     * Obtain macro dynamic library from --macro-lib.
     */
    std::unordered_set<std::string> GetMacroDefDynamicFiles();

    /**
     * Save used macros for unused import.
     */
    void SaveUsedMacros(MacroCall& macCall);

    /**
     * Save used macro packages for init global variable.
     */
    void SaveUsedMacroPkgs(const std::string packageName);

    /**
     * Init global variable before parallel compiled macro.
     */
    void InitGlobalVariable();

    /**
     * Eval single macro with runtime.
     */
    void EvaluateWithRuntime(MacroCall& macCall);

    /**
     * Release threadHandle when used parallel mode
     */
    void ReleaseThreadHandle(MacroCall& macCall);
    /**
     * Check attribute for macrocall.
     */
    bool CheckAttrTokens(std::vector<Token>& attrTokens, MacroCall& macCall);

    void ProcessTokensInQuoteExpr(
        TokenVector& input, size_t& startIndex, size_t& curIndex, MacroCall& macCall, bool retEval = false);
    bool HasMacroCallInStrInterpolation(const std::string& str, MacroCall& macCall);
    bool HasMacroCallInStrInterpolation(
        TokenVector& input, size_t startIndex, size_t curIndex, MacroCall& parentMacCall);
    void CreateChildMacroCall(
        TokenVector& inputTokens, size_t& startIndex, size_t& curIndex, MacroCall& macCall, bool reEval = false);
    void CheckDeprecatedMacrosUsage(MacroCall& macCall) const;
    bool NeedCreateMacroCallTree(MacroCall& macCall, bool reEval);
    void CreateMacroCallTree(MacroCall& macCall, bool reEval = false);
    void CreateMacroCallsTree(bool reEval = false);
    void EvalOneMacroCall(MacroCall& macCall);

    void EvalMacroCallsOnSingleThread();
    void EvalMacroCallsOnMultiThread();
    bool CreateThreadToEvalMacroCall(MacroCall& macCall);
    bool WaitForOneMacroCallEvalFinish(std::list<MacroCall*>& evalMacCalls);
    void EvalMacroCalls();

    /**
     * ReEvaluate the macrocalls if there are new macrocalls after macro expansion.
     */
    void ReEvalAfterEvalMacroCalls();

    /**
     * Record the inner built-in macro. The expanded position info will use the outer macro's position info.
     */
    void RefreshBuildInMacroPostionInfo();

    /**
     * Release the memory allocated when using macro with context.
     */
    void FreeMacroInfoVecForMacroCall(MacroCall& mc) const;

    void ProcessNewTokens(MacroCall& macCall);

    void CollectMacroLibs();
};
} // namespace Cangjie

#endif
