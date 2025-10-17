// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class DiagnosticEngineImpl, which is an implementation class of DiagnosticEngine.
 */

#ifndef CANGJIE_BASIC_DIAGNOSTICENGINEIMPL_H
#define CANGJIE_BASIC_DIAGNOSTICENGINEIMPL_H

#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie {
class DiagnosticEngineImpl {
public:
    ~DiagnosticEngineImpl() noexcept
    {
        if (handler->GetKind() == DiagHandlerKind::COMPILER_HANDLER) {
            auto hk = static_cast<CompilerDiagnosticHandler*>(handler.get());
            if (hk->IsJsonFormat()) {
                hk->EmitDiagnosesInJson();
            }
        }
    }

    bool HasSourceManager() const
    {
        return sourceManager != nullptr;
    }
    void SetIsEmitter(bool emitter)
    {
        isEmitter = emitter;
    }
    void SetDisableWarning(bool dis)
    {
        disableWarning = dis;
    }
    bool GetIsEmitter() const
    {
        return isEmitter;
    }
    void SetIsDumpErrCnt(bool dump)
    {
        isDumpErrCnt = dump;
    }
    bool GetIsDumpErrCnt() const
    {
        return isDumpErrCnt;
    }
    void SetSourceManager(SourceManager* sm);
    SourceManager& GetSourceManager() noexcept;
    std::lock_guard<std::mutex> LockFirstErrorCategory()
    {
        return std::lock_guard{firstErrorCategoryMtx};
    }
    const std::optional<DiagCategory>& FirstErrorCategory() const
    {
        return firstErrorCategory;
    }
    int32_t GetDisableDiagDeep() const
    {
        return disableDiagDeep;
    }
    const std::vector<Diagnostic>& GetStoredDiags() const&
    {
        return storedDiags;
    }
    std::vector<Diagnostic> GetStoredDiags() &&
    {
        return std::move(storedDiags);
    }
    void SetStoredDiags(std::vector<Diagnostic>&& value)
    {
        storedDiags = std::move(value);
    }
    bool GetEnableDiagnose() const
    {
        return enableDiagnose;
    }
    
    void AddMacroCallNote(Diagnostic& diagnostic, const AST::Node& node, const Position& pos);

    // ability of transaction
    void Prepare();
    void Commit();
    void ClearTransaction();

    // use DiagEngineErrorCode rather than internal error message (for libast)
    void EnableCheckRangeErrorCodeRatherICE()
    {
        checkRangeErrorCodeRatherICE = true;
    };
    void DisableCheckRangeErrorCodeRatherICE()
    {
        checkRangeErrorCodeRatherICE = false;
    };
    bool IsCheckRangeErrorCodeRatherICE() const
    {
        return checkRangeErrorCodeRatherICE;
    };
    void SetDiagEngineErrorCode(DiagEngineErrorCode errorCode)
    {
        diagEngineErrorCode = errorCode;
    };
    /**
     * Convert unformat diagnostic message to real diagnostic message.
     */
    void ConvertArgsToDiagMessage(Diagnostic& diagnostic) noexcept;
    /**
     * Register diagnostic observer to diagnostic engine.
     */
    void RegisterHandler(std::unique_ptr<DiagnosticHandler>&& h);

    void IncreaseErrorCount(DiagCategory category);
    
    void IncreaseWarningCount(DiagCategory category);

    void IncreaseErrorCount();

    uint64_t GetWarningCount();
    
    uint64_t GetErrorCount();
    void IncreaseWarningPrintCount()
    {
        warningPrintCount++;
    }
    unsigned int GetWarningPrintCount() const
    {
        return warningPrintCount;
    }
    void IncreaseErrorPrintCount()
    {
        errorPrintCount++;
    }
    unsigned int GetErrorPrintCount() const
    {
        return errorPrintCount;
    }
    std::optional<unsigned int> GetMaxNumOfDiags() const
    {
        return maxNumOfDiags;
    }
    bool IsSupressedUnusedMain(const Diagnostic& diagnostic) noexcept;
    void HandleDiagnostic(Diagnostic& diagnostic) noexcept;
    
    void EmitCategoryDiagnostics(DiagCategory cate)
    {
        if (handler->GetKind() == DiagHandlerKind::COMPILER_HANDLER) {
            auto hk = static_cast<CompilerDiagnosticHandler*>(handler.get());
            hk->EmitCategoryDiagnostics(cate);
        }
    }

    DiagEngineErrorCode GetCategoryDiagnosticsString(DiagCategory cate, std::string& diagOut)
    {
        if (handler->GetKind() == DiagHandlerKind::COMPILER_HANDLER) {
            if (diagEngineErrorCode != DiagEngineErrorCode::NO_ERRORS && checkRangeErrorCodeRatherICE) {
                // Emit may cause unpredictable errors if diag engine error has been found before
                return diagEngineErrorCode;
            }
            auto hk = static_cast<CompilerDiagnosticHandler*>(handler.get());
            hk->SetOutToStringStream();
            hk->EmitCategoryDiagnostics(cate);
            hk->SetOutToErrStream();
            diagOut = hk->GetOutString();
        }
        return diagEngineErrorCode;
    }

    DiagEngineErrorCode GetCategoryDiagnosticInfos(DiagCategory cate, std::vector<DiagnosticInfo>& diagOut)
    {
        diagOut.clear();
        if (handler->GetKind() == DiagHandlerKind::COMPILER_HANDLER) {
            if (diagEngineErrorCode != DiagEngineErrorCode::NO_ERRORS && checkRangeErrorCodeRatherICE) {
                // Emit may cause unpredictable errors if diag engine error has been found before
                return diagEngineErrorCode;
            }
            auto hk = static_cast<CompilerDiagnosticHandler*>(handler.get());
            hk->EmitCategoryDiagnosticInfos(cate, diagOut);
        }
        return diagEngineErrorCode;
    }

    void EmitCategoryGroup()
    {
        if (handler->GetKind() == DiagHandlerKind::COMPILER_HANDLER) {
            auto hk = static_cast<CompilerDiagnosticHandler*>(handler.get());
            hk->EmitDiagnoseGroup();
        }
    }
    
    void SetErrorCountLimit(std::optional<unsigned int> errorCountLimit)
    {
        maxNumOfDiags = std::move(errorCountLimit);
    }
    
    std::vector<Diagnostic> GetCategoryDiagnostic(DiagCategory cate)
    {
        if (handler->GetKind() == DiagHandlerKind::COMPILER_HANDLER) {
            auto hk = static_cast<CompilerDiagnosticHandler*>(handler.get());
            return hk->GetCategoryDiagnostic(cate);
        }
        return {};
    }
    
    void ClearError()
    {
        errorCount = 0;
    }
    
    void Reset();
    
    /**
     * Set the status of diagnostic engine.
     * @param enable
     */
    void SetDiagnoseStatus(bool enable)
    {
        enableDiagnose = enable;
        hardDisable = !enable;
    }

    /**
     * Get the status of diagnostic engine.
     */
    bool GetDiagnoseStatus() const
    {
        return enableDiagnose;
    }

    /**
     * Report the number of errors and warnings.
     */
    void ReportErrorAndWarningCount();
    /**
     * Do diagnostic filter, if the diagnostic should be filter,return true.
     */
    bool DiagFilter(Diagnostic& diagnostic) noexcept;

    /**
     * Disable diagnose, and take out current stored diagnoses.
     * return current stored diagnoses in diagnose engine.
     */
    std::vector<Diagnostic> DisableDiagnose();

    void EnableDiagnose();

    /**
     * Set stored diagnoses to given contents, can be used to suppress cached diagnoses.
     * @param diags given diagnoses to be restored in diagnose engine
     */
    void EnableDiagnose(const std::vector<Diagnostic>& diags);

    std::vector<Diagnostic> ConsumeStoredDiags();

    bool HardDisable() const { return hardDisable; }
    /**
     * Make real Range if node is expanded from macrocall, otherwise Make Range from the begin and the end.
     */
    Range MakeRealRange(
        const AST::Node& node, const Position begin, const Position end, bool begLowBound = false) const;
    void CheckRange(DiagCategory cate, const Range& range);

private:
    mutable unsigned int errorCount = 0;
    mutable unsigned int warningCount = 0;
    mutable unsigned int errorPrintCount = 0;
    mutable unsigned int warningPrintCount = 0;
    // Key is category, value is error and warning count.
    mutable std::unordered_map<DiagCategory, std::pair<uint64_t, uint64_t>> countByCategory;
    std::mutex mux;
    std::mutex transactionMutex; // for Prepare/Commit/ClearTransaction
    int32_t disableDiagDeep = 0;
    bool enableDiagnose{true};
    bool disableWarning{false}; /* Does disable all warning. */
    bool hardDisable{false}; /**< Mark whether disabled by SetDiagnoseStatus. */
    std::unique_ptr<DiagnosticHandler> handler;

    std::optional<unsigned int> maxNumOfDiags = DEFAULT_DIAG_NUM;
    std::vector<std::function<bool(Diagnostic& diag)>> diagFilters;
    std::vector<Diagnostic> storedDiags;
    
    // IsEmitter is used to some tools like CJLint which don't want to output error to terminal.
    bool isEmitter{true};
    bool isDumpErrCnt{true};
    SourceManager* sourceManager{nullptr};
    // transaction of Diagnose, e.g Parse Type in ParseRefExpr
    std::unordered_map<std::thread::id, std::vector<Diagnostic>> transactionMap;
    std::unordered_map<std::thread::id, bool> isInTransaction;

    WarningOptionMgr* const warningOption = WarningOptionMgr::GetInstance();

    std::string GetArgStr(
        char formatChar, std::vector<DiagArgument>& formatArgs, unsigned long index, Diagnostic& diagnostic);

    bool checkRangeErrorCodeRatherICE{false};
    DiagEngineErrorCode diagEngineErrorCode{DiagEngineErrorCode::NO_ERRORS};
    std::mutex firstErrorCategoryMtx;
    std::optional<DiagCategory> firstErrorCategory = std::nullopt;
    friend class DiagnosticEngine::StashDisableDiagnoseStatus;
};
}
#endif
