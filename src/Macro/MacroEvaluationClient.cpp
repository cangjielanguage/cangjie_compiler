// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements macro evaluation apis for macro child process.
 */

#include "cangjie/Macro/MacroEvaluation.h"
#include "cangjie/Utils/ProfileRecorder.h"
#if defined(__linux__) || defined(__APPLE__)
#ifdef __ohos__
#include <signal.h>
#else
#include <sys/signal.h>
#endif
#include <sys/select.h>
#include <sys/wait.h>
#else
#include <windows.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <cstdlib>
#include <csignal>
using namespace Cangjie;
using namespace AST;
using namespace InvokeRuntime;

MacroEvalMsgSerializer MacroEvaluation::msgSlzer;

namespace {

const std::string MACRO_SRV_NAME = "LSPMacroServer";

void SignalHandler(int)
{
    Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
    _exit(1);
}

void SetExitSignal(void)
{
    std::signal(SIGABRT, SignalHandler);
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    std::signal(SIGSEGV, SignalHandler);
}

inline bool IsResultForMacCall(const std::string& id, const Position& pos, const MacroInvocation& mi)
{
    if (id != mi.identifier) {
        return false;
    }
    if (pos != mi.identifierPos) {
        return false;
    }
    if (pos.fileID != mi.identifierPos.fileID) {
        return false;
    }
    return true;
}
} // namespace

// MacroProcMsger for client
#ifdef _WIN32
void MacroProcMsger::SetSrvPipeHandle(HANDLE hRead, HANDLE hWrite)
{
    hChildRead = hRead;
    hChildWrite = hWrite;
}
#else
void MacroProcMsger::SetSrvPipeHandle(int hRead, int hWrite)
{
    pipefdP2C[0] = hRead;
    pipefdC2P[1] = hWrite;
}
#endif

void MacroProcMsger::CloseClientResource()
{
#ifdef _WIN32
    if (hParentRead != nullptr) {
        if (CloseHandle(hParentRead) == TRUE) {
            hParentRead = nullptr;
        } else {
            Errorln("CloseHandle hParentRead error");
        }
    }
    if (hParentWrite != nullptr) {
        if (CloseHandle(hParentWrite) == TRUE) {
            hParentWrite = nullptr;
        } else {
            Errorln("CloseHandle hParentWrite error");
        }
    }
    if (hProcess != nullptr) {
        if (CloseHandle(hProcess) == TRUE) {
            hProcess = nullptr;
        } else {
            Errorln("CloseHandle hProcess error");
        }
    }
    if (hThread != nullptr) {
        if (CloseHandle(hThread) == TRUE) {
            hThread = nullptr;
        } else {
            Errorln("CloseHandle hThread error");
        }
    }
#else
    if (pipefdP2C[1] != -1) {
        close(pipefdP2C[1]);
        pipefdP2C[1] = -1;
    }
    if (pipefdC2P[0] != -1) {
        close(pipefdC2P[0]);
        pipefdC2P[0] = -1;
    }
#endif
}

void MacroProcMsger::CloseMacroSrv()
{
    if (macroSrvRun.load()) {
        std::vector<uint8_t> msg;
        MacroEvalMsgSerializer msgSlzer;
        msgSlzer.SerializeExitMsg(msg);
        if (SendMsgToSrv(msg)) {
            auto start = std::chrono::steady_clock::now();
            auto end = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsedSeconds = end - start;
            double waitTime{5.0};
            while (macroSrvRun.load() && (elapsedSeconds.count() < waitTime)) {
                end = std::chrono::steady_clock::now();
                elapsedSeconds = end - start;
            }
            if (elapsedSeconds.count() >= waitTime) {
                Errorln(getpid(), " wait macro srv exit time out ", waitTime);
            }
        } else {
            std::cerr << getpid() << ": error Send Exit Task" << std::endl;
        }
    }
    CloseClientResource();
    return;
}

bool MacroProcMsger::WriteToSrvPipe(const uint8_t* buf, size_t size) const
{
#ifdef _WIN32
    return WriteFile(hParentWrite, buf, size, nullptr, 0) == TRUE;
#else
    ssize_t res = write(pipefdP2C[1], buf, size);
    while (res >= 0 && res < static_cast<ssize_t>(size)) {
        size -= static_cast<size_t>(res);
        buf += res;
        res = write(pipefdP2C[1], buf, size);
    }
    return res >= 0;
#endif
}

bool MacroProcMsger::ReadFromSrvPipe(uint8_t* buf, size_t size) const
{
#ifdef _WIN32
    return ReadFile(hParentRead, buf, size, nullptr, nullptr) == TRUE;
#else
    ssize_t res = read(pipefdC2P[0], buf, size);
    // res == 0, means end of file; res == -1, indicates error accurred
    while (res > 0 && res < static_cast<ssize_t>(size)) {
        size -= static_cast<size_t>(res);
        buf += res;
        res = read(pipefdC2P[0], buf, size);
    }
    return res > 0;
#endif
}

bool MacroProcMsger::SendMsgToSrv(const std::vector<uint8_t>& msg)
{
    if (msg.empty()) {
        return false;
    }
    if (pipeError.load()) {
        return false;
    }
    // Pipe capacity is limited, send msg slice by slice
    size_t sumSize = msg.size();
    size_t rest = sumSize % msgSliceLen;
    size_t msgNum = rest == 0 ? msg.size() / msgSliceLen : msg.size() / msgSliceLen + 1;
    size_t lastSize = rest == 0 ? msgSliceLen : rest;
    if (!WriteToSrvPipe(reinterpret_cast<uint8_t*>(&sumSize), sizeof(sumSize))) {
        perror("WriteToSrvPipe");
        pipeError.store(true);
        return false;
    }
    for (size_t i = 0; i < msgNum; ++i) {
        size_t curNum = (i == msgNum - 1) ? lastSize : msgSliceLen;
        if (!WriteToSrvPipe(msg.data() + i * msgSliceLen, curNum)) {
            perror("WriteToSrvPipe");
            pipeError.store(true);
            return false;
        }
    }
    return true;
}

bool MacroProcMsger::ReadMsgFromSrv(std::vector<uint8_t>& msg)
{
    size_t msgSize{0};
    msg.clear();
    if (pipeError.load()) {
        return false;
    }
    if (!ReadFromSrvPipe(reinterpret_cast<uint8_t*>(&msgSize), sizeof(msgSize))) {
        perror("ReadFromSrvPipe");
        pipeError.store(true);
        return false;
    }
    if (msgSize == 0) {
        perror("ReadFromSrvPipe size err");
        pipeError.store(true);
        return false;
    }
    size_t rest = msgSize % msgSliceLen;
    size_t msgNum = rest == 0 ? msgSize / msgSliceLen : msgSize / msgSliceLen + 1;
    size_t lastSize = rest == 0 ? msgSliceLen : rest;
    msg.resize(msgSize);
    for (size_t i = 0; i < msgNum; ++i) {
        size_t curNum = (i == msgNum - 1) ? lastSize : msgSliceLen;
        if (!ReadFromSrvPipe(msg.data() + i * msgSliceLen, curNum)) {
            perror("ReadFromSrvPipe");
            pipeError.store(true);
            return false;
        }
    }
    return true;
}

bool MacroProcMsger::ReadAllMsgFromSrv(std::list<std::vector<uint8_t>>& msgVec)
{
    msgVec.clear();
    std::vector<uint8_t> msg;
    bool errFlag = true;
    while (ReadMsgFromSrv(msg)) {
        msgVec.push_back(msg);
        // Check whether there is any msg in pipe
#ifdef _WIN32
        // if totalBytesAvail > 0, needs to keep reading
        DWORD totalBytesAvail;
        if (PeekNamedPipe(hParentRead, NULL, 0, nullptr, &totalBytesAvail, nullptr) == FALSE) {
            Errorln("PeekNamedPipe failed with error %d", GetLastError());
            return false;
        }
        if (totalBytesAvail == 0) {
            return true;
        }
#else
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(pipefdC2P[0], &readset);
        timeval timeout = {0, 0};
        int ret = select(pipefdC2P[0] + 1, &readset, nullptr, nullptr, &timeout);
        if (ret == -1) {
            perror("srv proc select pipe fail, stop read.");
            return false;
        } else if (ret == 0) {
            return true;
        } else if (FD_ISSET(pipefdC2P[0], &readset)) {
            continue;
        } else {
            perror("srv proc FD_ISSET fail, stop read.");
            return false;
        }
#endif
    }
    return !errFlag;
}

void MacroEvaluation::DeserializeMacroCallsResult(
    std::list<MacroCall*>& calls, const std::list<std::vector<uint8_t>>& msgList) const
{
    std::string id;
    Position pos;
    for (auto& msg : msgList) {
        MacroEvalMsgSerializer::DeSerializeIdInfoFromResult(id, pos, msg);
        bool findFlag{false};
        for (auto& mc : std::as_const(calls)) {
            auto pInvocation = mc->GetInvocation();
            if (!IsResultForMacCall(id, pos, *pInvocation)) {
                continue;
            }
            findFlag = true;
            MacroEvalMsgSerializer::DeSerializeTksFromResult(pInvocation->newTokens, msg);
            mc->status = MacroEvalMsgSerializer::DeSerializeStatusFromResult(msg);
            MacroEvalMsgSerializer::DeSerializeItemsFromResult(mc->items, msg);
            MacroEvalMsgSerializer::DeSerializeAssertParentsFromResult(mc->assertParents, msg);
            std::vector<Diagnostic> diags;
            MacroEvalMsgSerializer::DeSerializeDiagsFromResult(diags, msg);
            for (auto& diag : diags) {
                mc->DiagReport(static_cast<int>(diag.diagSeverity), MakeRange(diag.start, diag.end),
                               diag.errorMessage.c_str(), diag.mainHint.str.c_str());
            }
            mc->isDataReady = true;
            for (auto& parentName : mc->assertParents) {
                (void)ci->diag.Diagnose(
                    mc->GetBeginPos(), DiagKind::macro_assert_parent_context_failed, mc->GetFullName(), parentName);
            }
            break;
        }
        if (!findFlag) {
            Errorln("DeserializeMacroCallsResult not find macCall for result ", id, pos.ToString());
        }
    }
}

// Client
bool MacroEvaluation::SendMacroDefTask(const std::unordered_set<std::string>& macroLibs) const
{
    std::vector<uint8_t> msg;
    msgSlzer.SerializeDeflibMsg(macroLibs, msg);
    if (!MacroProcMsger::GetInstance().SendMsgToSrv(msg)) {
        std::cerr<<getpid()<<": error Send MacroDef Task"<<std::endl;
        return false;
    }
    return true;
}

void MacroEvaluation::WaitMacroDefResult() const
{
    std::vector<uint8_t> msg;
    if (!MacroProcMsger::GetInstance().ReadMsgFromSrv(msg)) {
        std::cerr<<getpid()<<": error Wait MacroDef Result"<<std::endl;
        return;
    }
    std::string msgStr(msg.begin(), msg.end());
    auto msgSize = msgStr.size();
    auto msgHeadlen = std::string("RespondFindDef ").size();
    if (msgSize <= msgHeadlen) {
        // Open macrodef lib success.
        return;
    }
    auto dyfile = msgStr.substr(msgHeadlen, msgSize - msgHeadlen);
    if (!dyfile.empty()) {
        // Open macrodef lib failed in child process.
        (void)ci->diag.Diagnose(DiagKind::can_not_open_macro_library, dyfile);
    }
    return;
}

/* Send a macrocall task for serial macro expansion. */
bool MacroEvaluation::SendMacroCallTask(MacroCall& call) const
{
    std::vector<uint8_t> msg;
    msgSlzer.SerializeMacroCallMsg(call, msg);
    if (!MacroProcMsger::GetInstance().SendMsgToSrv(msg)) {
        std::cerr<<getpid()<<": error Send MacroCall Task"<<std::endl;
        call.status = MacroEvalStatus::FAIL;
        return false;
    }
    return true;
}

/* Send a exit stg task to clear usedMacroPkgs for globale var. */
void MacroEvaluation::SendExitStgTask() const
{
    std::vector<uint8_t> msg;
    msgSlzer.SerializeExitMsg(msg, false);
    if (!MacroProcMsger::GetInstance().SendMsgToSrv(msg)) {
        std::cerr << getpid() << ": error Send exit stg false" << std::endl;
    }
}

/* Wait for a macrocall eval result for serial macro expansion. */
void MacroEvaluation::WaitMacroCallEvalResult(MacroCall& call) const
{
    std::list<std::vector<uint8_t>> resBuf;
    if (!MacroProcMsger::GetInstance().ReadAllMsgFromSrv(resBuf)) {
        std::cerr<<getpid()<<": error Wait MacroCall EvalResult"<<std::endl;
        call.status = MacroEvalStatus::FAIL;
        return;
    }
    if (resBuf.size() != 1) {
        call.status = MacroEvalStatus::FAIL;
        return;
    }
    std::string id;
    Position pos;
    MacroEvalMsgSerializer::DeSerializeIdInfoFromResult(id, pos, resBuf.front());
    auto pInvocation = call.GetInvocation();
    if (!IsResultForMacCall(id, pos, *pInvocation)) {
        call.status = MacroEvalStatus::FAIL;
        return;
    }
    MacroEvalMsgSerializer::DeSerializeTksFromResult(pInvocation->newTokens, resBuf.front());
    call.status = MacroEvalMsgSerializer::DeSerializeStatusFromResult(resBuf.front());
    MacroEvalMsgSerializer::DeSerializeItemsFromResult(call.items, resBuf.front());
    MacroEvalMsgSerializer::DeSerializeAssertParentsFromResult(call.assertParents, resBuf.front());
    std::vector<Diagnostic> diags;
    MacroEvalMsgSerializer::DeSerializeDiagsFromResult(diags, resBuf.front());
    for (auto& diag : diags) {
        call.DiagReport(static_cast<int>(diag.diagSeverity), MakeRange(diag.start, diag.end),
                        diag.errorMessage.c_str(), diag.mainHint.str.c_str());
    }
    call.isDataReady = true;
    // Check assertParentContext failed or not in child process.
    for (auto& parentName : call.assertParents) {
        (void)ci->diag.Diagnose(call.GetBeginPos(),
            DiagKind::macro_assert_parent_context_failed, call.GetFullName(), parentName);
    }
    return;
}

/* Send macrocalls task for parallel macro expansion. */
bool MacroEvaluation::SendMacroCallsTask(std::list<MacroCall*>& calls) const
{
    std::vector<uint8_t> msgData;
    std::list<MacroCall*> needSendCalls;
    for (auto& mc : std::as_const(calls)) {
        if (mc->hasSend) {
            continue;
        }
        auto name = mc->GetIdentifier() + mc->GetBeginPos().ToString();
        Utils::ProfileRecorder::Start("Parallel Evaluate Macros", name);
        needSendCalls.push_back(mc);
        mc->hasSend = true;
    }
    msgSlzer.SerializeMultiCallsMsg(needSendCalls, msgData);
    // If all macrocall tasks have been sent to the server already, the msgs are empty,
    // but we need to send the MsgContent_multiCalls to the server to wait for the result of unfinished macrocall.
    if (!MacroProcMsger::GetInstance().SendMsgToSrv(msgData)) {
        std::cerr<<getpid()<<": error Send MacroCalls Task"<<std::endl;
        for (auto& mc : std::as_const(calls)) {
            mc->status = MacroEvalStatus::FAIL;
        }
        return false;
    }
    return true;
}

/* Wait for macrocall eval result for parallel macro expansion */
bool MacroEvaluation::WaitMacroCallsEvalResult(std::list<MacroCall*>& calls) const
{
    const auto foundEvalmc = std::find_if(calls.cbegin(), calls.cend(), [](auto& mc) { return !mc->isDataReady; });
    if (foundEvalmc == calls.end()) {
        // All macrocalls eval finish.
        return true;
    }
    std::list<std::vector<uint8_t>> msgList;
    if (!MacroProcMsger::GetInstance().ReadAllMsgFromSrv(msgList)) {
        std::cerr<<getpid()<<": error Wait MacroCalls EvalResult"<<std::endl;
        for (auto& mc : std::as_const(calls)) {
            mc->status = MacroEvalStatus::FAIL;
        }
        return false;
    }
    DeserializeMacroCallsResult(calls, msgList);
    return true;
}

#if defined(__linux__) || defined(__APPLE__)
namespace {
static void WaitProcessExit(pid_t pid)
{
    [[maybe_unused]] int status;
    pid_t ret = waitpid(pid, &status, 0); // Wait child process exit
    MacroProcMsger::GetInstance().macroSrvRun.store(false);
    if (ret == -1) {
        Errorln("Error waiting for macro srv process");
    }
}

static void CloseBothEndsOfPipe(int* pipe)
{
    close(pipe[0]);
    close(pipe[1]);
    pipe[0] = -1;
    pipe[1] = -1;
}
inline void RedirectStdOutTOStdErr()
{
    int ret = dup2(STDERR_FILENO, STDOUT_FILENO);
    if (ret == -1) {
        perror("macro srv dup2 from STDOUT_FILENO to STDERR_FILENO fail");
    }
}
} // namespace

void MacroEvaluation::ExecMacroSrv(pid_t pid) const
{
    // args
    std::vector<char*> cstrings;
    std::string macSrvName = MACRO_SRV_NAME;
    std::string hRead = std::to_string(MacroProcMsger::GetInstance().pipefdP2C[0]);
    std::string hWrite = std::to_string(MacroProcMsger::GetInstance().pipefdC2P[1]);
    std::string enPara = enableParallelMacro ? "1" : "0";
    std::string pidStr = std::to_string(pid);
    cstrings.push_back(macSrvName.data());
    cstrings.push_back(hRead.data());
    cstrings.push_back(hWrite.data());
    cstrings.push_back(enPara.data());
    cstrings.push_back(ci->invocation.globalOptions.executablePath.data());
    cstrings.push_back(pidStr.data());
    cstrings.push_back(nullptr);  // for execvp argv
    execvp(macSrvName.c_str(), cstrings.data());
}

void MacroEvaluation::CreateMacroSrvProcess()
{
    [[maybe_unused]] std::lock_guard lg(MacroProcMsger::GetInstance().mutex);
    if (MacroProcMsger::GetInstance().macroSrvRun.load()) {
        return;
    }
    // close old pipe
    MacroProcMsger::GetInstance().CloseClientResource();
    // create pipe
    if (pipe(MacroProcMsger::GetInstance().pipefdP2C) == -1) {
        perror("Create P2C pipe fail: ");
        return;
    }
    if (pipe(MacroProcMsger::GetInstance().pipefdC2P) == -1) {
        CloseBothEndsOfPipe(MacroProcMsger::GetInstance().pipefdP2C);
        perror("Create C2P pipe fail: ");
        return;
    }
    MacroProcMsger::GetInstance().pipeError.store(false);
    pid_t ppid = getpid();
    pid_t pid = fork();
    if (pid < 0) {
        CloseBothEndsOfPipe(MacroProcMsger::GetInstance().pipefdP2C);
        CloseBothEndsOfPipe(MacroProcMsger::GetInstance().pipefdC2P);
        return;
    }
    MacroProcMsger::GetInstance().macroSrvRun.store(true);
    if (pid == 0) {
        // child process
#ifdef __linux__
        if (prctl(PR_SET_PDEATHSIG, SIGHUP) == -1) {
            perror("PR_SET_PDEATHSIG: ");
        }
#endif
        RedirectStdOutTOStdErr();
        ExecMacroSrv(ppid);
        // if exec fails, the following code will be run:
        perror("run macro srv in fork, due to exec fail");
        RunMacroSrv();
    } else {
        // main process
        SetExitSignal();
        std::thread w(WaitProcessExit, pid);
        w.detach();
        // close unused pipe
        close(MacroProcMsger::GetInstance().pipefdP2C[0]);
        close(MacroProcMsger::GetInstance().pipefdC2P[1]);
    }
    return;
}
#else
void WaitProcessExit(PROCESS_INFORMATION pi)
{
    if (WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED) {
        Errorln("Getting child process exit code: ", GetLastError());
    }
    MacroProcMsger::GetInstance().macroSrvRun.store(false);
}

// if parent process exits child proc should also exit
void CreateJobObjectForMacroSrv()
{
    HANDLE ghJob = CreateJobObject(nullptr, nullptr);
    if (ghJob == nullptr) {
        Errorln("Create job object for macro srv fail!");
        return;
    } else {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
        // Configure all child processes associated with the job to terminate when the job end
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (0 == SetInformationJobObject(ghJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
            Errorln("Could not SetInformationJobObject for macro srv");
            return;
        }
    }
    if (AssignProcessToJobObject(ghJob, MacroProcMsger::GetInstance().hProcess) == FALSE) {
        Errorln("Assign process to JobObject fail!");
    }
}

void RedirectStdOutForMarcoSrv(STARTUPINFO& si)
{
    HANDLE hParentErr = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdOutput = hParentErr;
    si.hStdError = hParentErr;
    si.dwFlags = STARTF_USESTDHANDLES;
}

void CloseSrvPipe()
{
    if (MacroProcMsger::GetInstance().hChildWrite != nullptr) {
        if (CloseHandle(MacroProcMsger::GetInstance().hChildWrite) == TRUE) {
            MacroProcMsger::GetInstance().hChildWrite = nullptr;
        } else {
            Errorln("CloseHandle hParentRead error");
        }
    }
    if (MacroProcMsger::GetInstance().hChildRead != nullptr) {
        if (CloseHandle(MacroProcMsger::GetInstance().hChildRead) == TRUE) {
            MacroProcMsger::GetInstance().hChildRead = nullptr;
        } else {
            Errorln("CloseHandle hParentWrite error");
        }
    }
}

bool CreateMacroMsgPipe()
{
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    BOOL bRet =
        CreatePipe(&MacroProcMsger::GetInstance().hParentRead, &MacroProcMsger::GetInstance().hChildWrite, &sa, 0);
    if (bRet == FALSE) {
        Errorln("Create macro srv pipe fail!");
        return false;
    }
    bRet = CreatePipe(&MacroProcMsger::GetInstance().hChildRead, &MacroProcMsger::GetInstance().hParentWrite, &sa, 0);
    if (bRet == FALSE) {
        CloseSrvPipe();
        MacroProcMsger::GetInstance().CloseClientResource();
        Errorln("Create macro srv pipe fail!");
        return false;
    }
    return true;
}

std::string GetMacroSrvCmd(bool enableParallelMacro, std::string& cjcPath)
{
    std::string cmdStr = MACRO_SRV_NAME + ".exe";
    const size_t buffLen = 20;
    char handlebuffer[buffLen];
    sprintf_s(handlebuffer, buffLen, "%d", MacroProcMsger::GetInstance().hChildRead);
    cmdStr += " " + std::string(handlebuffer);
    sprintf_s(handlebuffer, buffLen, "%d", MacroProcMsger::GetInstance().hChildWrite);
    cmdStr += " " + std::string(handlebuffer);
    cmdStr += enableParallelMacro ? " 1" : " 0";
    cmdStr += " \"" + cjcPath +"\""; // cjc folder to find runtime for lsp not in sdk
    return cmdStr;
}

void MacroEvaluation::CreateMacroSrvProcess()
{
    [[maybe_unused]] std::lock_guard lg(MacroProcMsger::GetInstance().mutex);
    if (MacroProcMsger::GetInstance().macroSrvRun.load()) {
        return;
    }
    // close old resource
    MacroProcMsger::GetInstance().CloseClientResource();
    if (!CreateMacroMsgPipe()) {
        return;
    }
    MacroProcMsger::GetInstance().pipeError.store(false);
    // The LSP uses the std out for communication. To avoid affecting it, the std out is redirected to the std err.
    STARTUPINFO si = {0};
    si.cb = sizeof(si);
    RedirectStdOutForMarcoSrv(si);
    PROCESS_INFORMATION pi;
    BOOL bRet = CreateProcess(nullptr, const_cast<LPSTR>(GetMacroSrvCmd(enableParallelMacro,
        ci->invocation.globalOptions.executablePath).c_str()), nullptr, nullptr,
        TRUE, NULL, nullptr, nullptr, &si, &pi);
    if (bRet == FALSE) {
        CloseSrvPipe();
        MacroProcMsger::GetInstance().CloseClientResource();
        Errorln("Create ", MACRO_SRV_NAME, " fail!");
        return;
    }
    MacroProcMsger::GetInstance().macroSrvRun.store(true);
    std::thread w(WaitProcessExit, pi);
    w.detach();
    MacroProcMsger::GetInstance().hProcess = pi.hProcess;
    MacroProcMsger::GetInstance().hThread = pi.hThread;
    CreateJobObjectForMacroSrv();
    CloseSrvPipe();
    return;
}
#endif