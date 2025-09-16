// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements macro evaluation server related apis for macro.
 */

#include "cangjie/Macro/MacroEvaluation.h"
#if defined (__linux__) || defined(__APPLE__)
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <pthread.h>
#endif
#ifdef __linux__
#include <sys/prctl.h>
#endif

using namespace Cangjie;
using namespace AST;

namespace {
enum class TaskType : unsigned int {
    FIND_MACRO_DEFLIB,
    EVAL_MACRO_CALL,
    EXIT_MACRO_SRV,
    EXIT_MACRO_STG,
};
inline TaskType GetMacroTaskType(const std::vector<uint8_t>& msg)
{
    switch (MacroEvalMsgSerializer::GetMacroMsgContenType(msg)) {
        case MacroMsgFormat::MsgContent_defLib:
            return TaskType::FIND_MACRO_DEFLIB;
        case MacroMsgFormat::MsgContent_multiCalls:
            return TaskType::EVAL_MACRO_CALL;
        case MacroMsgFormat::MsgContent_exitTask:
            if (!MacroMsgFormat::GetMacroMsg(msg.data())->content_as_exitTask()->flag()) {
                return TaskType::EXIT_MACRO_STG;
            }
            [[fallthrough]]; // need exit
        case MacroMsgFormat::MsgContent_NONE:
            [[fallthrough]]; // need exit
        case MacroMsgFormat::MsgContent_macroResult:
            [[fallthrough]]; // need exit
        default:
            break;
    }
    return TaskType::EXIT_MACRO_SRV;
}
} // namespace

// MacroProcMsger for srv
bool MacroProcMsger::WriteToClientPipe(const uint8_t* buf, size_t size) const
{
#ifdef _WIN32
    return WriteFile(hChildWrite, buf, size, nullptr, nullptr) == TRUE;

#else
    ssize_t res = write(pipefdC2P[1], buf, size);
    while (res >= 0 && res < static_cast<ssize_t>(size)) {
        size -= static_cast<size_t>(res);
        buf += res;
        res = write(pipefdC2P[1], buf, size);
    }
    return res >= 0;
#endif
}
bool MacroProcMsger::ReadFromClientPipe(uint8_t* buf, size_t size) const
{
#ifdef _WIN32
    return ReadFile(hChildRead, buf, size, nullptr, nullptr) == TRUE;
#else
    ssize_t res = read(pipefdP2C[0], buf, size);
    // res == 0, means end of file; res == -1, indicates error accurred
    while (res > 0 && res < static_cast<ssize_t>(size)) {
        size -= static_cast<size_t>(res);
        buf += res;
        res = read(pipefdP2C[0], buf, size);
    }
    return res > 0;
#endif
}

bool MacroProcMsger::SendMsgToClient(const std::vector<uint8_t>& msg)
{
    if (msg.empty()) {
        return false;
    }
    size_t sumSize = msg.size();
    size_t rest = sumSize % msgSliceLen;
    size_t msgNum = rest == 0 ? msg.size() / msgSliceLen : msg.size() / msgSliceLen + 1;
    size_t lastSize = rest == 0 ? msgSliceLen : rest;
    if (!WriteToClientPipe(reinterpret_cast<uint8_t*>(&sumSize), sizeof(sumSize))) {
        perror("WriteToClientPipe");
        return false;
    }
    for (size_t i = 0; i < msgNum; ++i) {
        size_t curNum = (i == msgNum - 1) ? lastSize : msgSliceLen;
        if (!WriteToClientPipe(msg.data() + i * msgSliceLen, curNum)) {
            perror("WriteToClientPipe");
            return false;
        }
    }
    return true;
}

bool MacroProcMsger::ReadMsgFromClient(std::vector<uint8_t>& msg)
{
    size_t msgSize{0};
    msg.clear();
    if (!ReadFromClientPipe(reinterpret_cast<uint8_t*>(&msgSize), sizeof(msgSize))) {
        perror("ReadFromClientPipe");
        return false;
    }
    if (msgSize == 0) {
        Errorln(getpid(), " Msg size error, size: ", msgSize);
        return false;
    }
    size_t rest = msgSize % msgSliceLen;
    size_t msgNum = rest == 0 ? msgSize / msgSliceLen : msgSize / msgSliceLen + 1;
    size_t lastSize = rest == 0 ? msgSliceLen : rest;
    msg.resize(msgSize);
    for (size_t i = 0; i < msgNum; ++i) {
        size_t curNum = (i == msgNum - 1) ? lastSize : msgSliceLen;
        if (!ReadFromClientPipe(msg.data() + i * msgSliceLen, curNum)) {
            perror("ReadFromClientPipe");
            return false;
        }
    }
    return true;
}

#ifdef __linux__
namespace {
inline void RenameSrvProcess()
{
    std::string subName = "msrv" + std::to_string(getpid());
    size_t maxProcNameSize = 15;
    if (subName.size() > maxProcNameSize) {
        subName = subName.substr(0, maxProcNameSize);
    }
    int ret = prctl(PR_SET_NAME, subName.c_str());
    if (ret == -1) {
        Errorln(getpid(), "Rename macro srv to ", subName, " fail");
        return;
    }
}
} // namespace
#endif

#if defined(__linux__) || defined(__APPLE__)
void MacroEvaluation::RunMacroSrv()
{
    useChildProcess = false; // macro srv is child
#ifdef __linux__
    RenameSrvProcess();
#endif
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    RuntimeInit::GetInstance().InitRuntime(
        ci->invocation.GetRuntimeLibPath(), ci->invocation.globalOptions.environment.allVariables);
#endif
    // close unused pipe
    close(MacroProcMsger::GetInstance().pipefdP2C[1]);
    close(MacroProcMsger::GetInstance().pipefdC2P[0]);
    ExecuteEvalSrvTask();
    RuntimeInit::GetInstance().CloseRuntime();
    close(MacroProcMsger::GetInstance().pipefdP2C[0]);
    close(MacroProcMsger::GetInstance().pipefdC2P[1]);
    exit(0);
}
#endif
// Server (macro srv process)
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
bool MacroEvaluation::FindDef(const std::vector<uint8_t>& msg) const
{
    std::list<std::string> macroLibs;
    std::string resMsg{"RespondFindDef "};
    MacroEvalMsgSerializer::DeSerializeDeflibMsg(macroLibs, msg);
    for (auto& obj : std::as_const(macroLibs)) {
        auto dyfile = FileUtil::NormalizePath(obj);
        auto handle = InvokeRuntime::OpenSymbolTable(dyfile);
        if (!handle) {
            resMsg += dyfile;
            break;
        }
        InvokeRuntime::SetOpenedLibHandles(handle);
    }
    std::vector<uint8_t> res(resMsg.begin(), resMsg.end());
    if (!MacroProcMsger::GetInstance().SendMsgToClient(res)) {
        std::cerr << getpid() << ": error Respond FindDef" << std::endl;
        return false;
    }
    return true;
}
#endif
bool MacroEvaluation::SerializeAndNotifyResult(MacroCall& macCall) const
{
    auto pInvocation = macCall.GetInvocation();
    if (pInvocation && MacroExpandFailed(pInvocation->newTokens)) {
        // Check eval result and set status failed.
        macCall.status = MacroEvalStatus::FAIL;
    }
    // Notify the macrocall result to client.
    std::vector<uint8_t> mcResult;
    msgSlzer.SerializeMacroCallResultMsg(macCall, mcResult);
    if (!MacroProcMsger::GetInstance().SendMsgToClient(mcResult)) {
        std::cerr << getpid() << ": error MacroCall Result" << std::endl;
        return false;
    }
    return true;
}

/* Eval macrocalls in parallel mode, and Wait for a macrocall to eval complete and notify the result. */
bool MacroEvaluation::EvalMacroCallsAndWaitResult()
{
    // Find macrodef's method from dylib.
    for (auto& mc : std::as_const(macCalls)) {
        if (mc->status != MacroEvalStatus::INIT) {
            continue;
        }
        if (!mc->FindMacroDefMethod(ci)) {
            mc->status = MacroEvalStatus::FAIL;
            if (!SerializeAndNotifyResult(*mc)) {
                return false;
            }
            continue;
        }
        SaveUsedMacros(*mc);
    }
    const auto foundEvalMacroCall =
        std::find_if(macCalls.cbegin(), macCalls.cend(), [](auto& mc) { return mc->status != MacroEvalStatus::FAIL; });
    if (foundEvalMacroCall == macCalls.end()) {
        // All macrocalls FindMacroDef failed.
        return true;
    }

    // Init global variable of macrodef package before parallel macro expansion.
    InitGlobalVariable();
    // ParallelMacro, evaluate macrocall in runtime coroutine.
    for (auto& mc : std::as_const(macCalls)) {
        if (mc->status != MacroEvalStatus::INIT) {
            continue;
        }
        EvalOneMacroCall(*mc);
        mc->status = MacroEvalStatus::EVAL;
    }
    // Wait for a macrocall evaluation to complete and notify the result.
    bool bEvalFinish = false;
    while (!bEvalFinish) {
        auto iter = macCalls.begin();
        while (iter != macCalls.end()) {
            auto& macCall = *iter;
            // Wait for a macroCall eval complete.
            if (!macCall->isDataReady) {
                (void)++iter;
                continue;
            }
            // Release coroutine handle after eval complete.
            ReleaseThreadHandle(*macCall);
            if (!SerializeAndNotifyResult(*macCall)) {
                return false;
            }
            (void)macCalls.erase(iter);
            bEvalFinish = true;
            break;
        }
    }
    return true;
}

std::unique_ptr<MacroExpandDecl> MacroEvaluation::CreateMacroExpand(const MacroMsgFormat::MacroCall& callFmt) const
{
    auto med = std::make_unique<MacroExpandDecl>();
    MacroEvalMsgSerializer::DeSerializeRangeFromCall(med->begin, med->end, callFmt);
    auto pInvocation = med->GetInvocation();
    MacroEvalMsgSerializer::DeSerializeIdInfoFromCall(pInvocation->identifier, pInvocation->identifierPos, callFmt);
    MacroEvalMsgSerializer::DeSerializeArgsFromCall(pInvocation->args, callFmt);
    MacroEvalMsgSerializer::DeSerializeAttrsFromCall(pInvocation->attrs, callFmt);
    pInvocation->hasAttr = callFmt.hasAttrs();
    return med;
}

void MacroEvaluation::DeSerializeMacroCall(const MacroMsgFormat::MacroCall& callFmt)
{
    macDecls.emplace_back(CreateMacroExpand(callFmt));
    auto macCall = std::make_unique<MacroCall>(macDecls.back().get());
    MacroEvalMsgSerializer::DeSerializeParentNamesFromCall(macCall->parentNames, callFmt);
    MacroEvalMsgSerializer::DeSerializeChildMsgesFromCall(macCall->childMessages, callFmt);
    macCall->methodName = callFmt.methodName()->str();
    macCall->packageName = callFmt.packageName()->str();
    macCall->libPath = callFmt.libPath()->str();
    macCall->ci = ci;
    macCalls.emplace_back(std::move(macCall));
}

bool MacroEvaluation::EvalMacroCall(std::vector<uint8_t>& msg)
{
    if (MacroEvalMsgSerializer::GetMacroMsgContenType(msg) == MacroMsgFormat::MsgContent::MsgContent_multiCalls) {
        // callsOffset->calls() may be empty, but need to wait macrocall eval result for parallel macro.
        auto callsOffset = MacroMsgFormat::GetMacroMsg(msg.data())->content_as_multiCalls();
        flatbuffers::uoffset_t callSize = callsOffset->calls()->size();
        for (flatbuffers::uoffset_t i = 0; i < callSize; i++) {
            DeSerializeMacroCall(*callsOffset->calls()->Get(i));
        }
    }
    if (macCalls.empty()) {
        return false;
    }
    if (enableParallelMacro) {
        // Parallel macro expansion.
        return EvalMacroCallsAndWaitResult();
    }
    // Serial macro expansion.
    auto macCall = macCalls.back().get();
    if (!macCall) {
        Errorln("cannot find macro method.");
        return false;
    }
    if (!macCall->FindMacroDefMethod(ci)) {
        Errorln("cannot find macro method ", macCall->methodName);
        return false;
    }
    SaveUsedMacroPkgs(macCall->packageName);
    InitGlobalVariable();
    EvalOneMacroCall(*macCall);
    return SerializeAndNotifyResult(*macCall);
}

void MacroEvaluation::ResetForNextEval()
{
    usedMacroPkgs.clear(); // for global var
    macDecls.clear();
    macCalls.clear();
    ci->diag.Reset();
}

void MacroEvaluation::ExecuteEvalSrvTask()
{
    std::vector<uint8_t> msgInf;
    for (;;) {
        if (!MacroProcMsger::GetInstance().ReadMsgFromClient(msgInf)) {
            Errorln(getpid(), " Macro srv read message fail");
            break;
        }
        TaskType msgT = GetMacroTaskType(msgInf);
        bool exit = false;
        switch (msgT) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
            case TaskType::FIND_MACRO_DEFLIB:
                if (!FindDef(msgInf)) {
                    Errorln(getpid(), " Macro srv find define fail");
                    exit = true;
                }
                break;
#endif
            case TaskType::EVAL_MACRO_CALL:
                if (!EvalMacroCall(msgInf)) {
                    Errorln(getpid(), " Macro srv eval macro call fail");
                    exit = true;
                }
                break;
            case TaskType::EXIT_MACRO_STG:
                ResetForNextEval();
                break;
            case TaskType::EXIT_MACRO_SRV:
                [[fallthrough]]; // need exit
            default:
                exit = true;
                break;
        }
        if (exit) {
            break;
        }
    }
}
