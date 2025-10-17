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

#include "DiagnosticEngineImpl.h"

namespace Cangjie {
DiagnosticEngine::StashDisableDiagnoseStatus::StashDisableDiagnoseStatus(DiagnosticEngine* e, bool hasTargetType)
    : engine(e),
      enableDiagnose(e->impl->enableDiagnose),
      disableDiagDeep(e->impl->disableDiagDeep),
      storedDiags(e->impl->storedDiags),
      hasTargetType(hasTargetType)
{
    if (!hasTargetType) {
        e->impl->enableDiagnose = true;
        e->impl->disableDiagDeep = 0;
        e->impl->storedDiags.clear();
    }
}

DiagnosticEngine::StashDisableDiagnoseStatus::~StashDisableDiagnoseStatus() noexcept
{
    if (hasTargetType) {
        engine->impl->enableDiagnose = true;
        engine->impl->disableDiagDeep = 0;
        engine->impl->storedDiags.erase(
            std::remove_if(engine->impl->storedDiags.begin(), engine->impl->storedDiags.end(),
                [](const Diagnostic& diag) { return diag.diagSeverity == DiagSeverity::DS_ERROR; }),
            engine->impl->storedDiags.end());
        for (auto& diag : engine->impl->storedDiags) {
            engine->Diagnose(diag);
        }
    }
    std::swap(engine->impl->enableDiagnose, enableDiagnose);
    std::swap(engine->impl->disableDiagDeep, disableDiagDeep);
    std::swap(engine->impl->storedDiags, storedDiags);
}

bool DiagnosticEngine::HasSourceManager()
{
    return impl->HasSourceManager();
}

void DiagnosticEngine::SetIsEmitter(bool emitter)
{
    return impl->SetIsEmitter(emitter);
}

void DiagnosticEngine::SetDisableWarning(bool dis)
{
    return impl->SetDisableWarning(dis);
}

bool DiagnosticEngine::GetIsEmitter() const
{
    return impl->GetIsEmitter();
}

void DiagnosticEngine::SetIsDumpErrCnt(bool dump)
{
    return impl->SetIsDumpErrCnt(dump);
}

bool DiagnosticEngine::GetIsDumpErrCnt() const
{
    return impl->GetIsDumpErrCnt();
}

void DiagnosticEngine::SetSourceManager(SourceManager* sm)
{
    return impl->SetSourceManager(sm);
}

SourceManager& DiagnosticEngine::GetSourceManager() noexcept
{
    return impl->GetSourceManager();
}

void DiagnosticEngine::AddMacroCallNote(Diagnostic& diagnostic, const AST::Node& node, const Position& pos)
{
    return impl->AddMacroCallNote(diagnostic, node, pos);
}

void DiagnosticEngine::Prepare()
{
    impl->Prepare();
}
void DiagnosticEngine::Commit()
{
    impl->Commit();
}
void DiagnosticEngine::ClearTransaction()
{
    impl->ClearTransaction();
}

void DiagnosticEngine::EnableCheckRangeErrorCodeRatherICE()
{
    impl->EnableCheckRangeErrorCodeRatherICE();
}
void DiagnosticEngine::DisableCheckRangeErrorCodeRatherICE()
{
    impl->DisableCheckRangeErrorCodeRatherICE();
}
bool DiagnosticEngine::IsCheckRangeErrorCodeRatherICE() const
{
    return impl->IsCheckRangeErrorCodeRatherICE();
}
void DiagnosticEngine::SetDiagEngineErrorCode(DiagEngineErrorCode errorCode)
{
    impl->SetDiagEngineErrorCode(errorCode);
}

std::lock_guard<std::mutex> DiagnosticEngine::LockFirstErrorCategory()
{
    return impl->LockFirstErrorCategory();
}
const std::optional<DiagCategory>& DiagnosticEngine::FirstErrorCategory() const
{
    return impl->FirstErrorCategory();
}
int32_t DiagnosticEngine::GetDisableDiagDeep() const
{
    return impl->GetDisableDiagDeep();
}
const std::vector<Diagnostic>& DiagnosticEngine::GetStoredDiags() const
{
    return impl->GetStoredDiags();
}
void DiagnosticEngine::SetStoredDiags(std::vector<Diagnostic>&& value)
{
    return impl->SetStoredDiags(std::move(value));
}
bool DiagnosticEngine::GetEnableDiagnose() const
{
    return impl->GetEnableDiagnose();
}
bool DiagnosticEngine::DiagFilter(Diagnostic& diagnostic) noexcept
{
    return impl->DiagFilter(diagnostic);
}

void DiagnosticEngine::ConvertArgsToDiagMessage(Diagnostic& diagnostic) noexcept
{
    impl->ConvertArgsToDiagMessage(diagnostic);
}
void DiagnosticEngine::RegisterHandler(std::unique_ptr<DiagnosticHandler>&& h)
{
    impl->RegisterHandler(std::move(h));
}

void DiagnosticEngine::IncreaseErrorCount(DiagCategory category)
{
    impl->IncreaseErrorCount(category);
}

void DiagnosticEngine::IncreaseWarningCount(DiagCategory category)
{
    impl->IncreaseWarningCount(category);
}

void DiagnosticEngine::IncreaseErrorCount()
{
    impl->IncreaseErrorCount();
}

uint64_t DiagnosticEngine::GetWarningCount()
{
    return impl->GetWarningCount();
}

uint64_t DiagnosticEngine::GetErrorCount()
{
    return impl->GetErrorCount();
}

void DiagnosticEngine::IncreaseWarningPrintCount()
{
    impl->IncreaseWarningPrintCount();
}
unsigned int DiagnosticEngine::GetWarningPrintCount() const
{
    return impl->GetWarningPrintCount();
}
void DiagnosticEngine::IncreaseErrorPrintCount()
{
    impl->IncreaseErrorPrintCount();
}
unsigned int DiagnosticEngine::GetErrorPrintCount() const
{
    return impl->GetErrorPrintCount();
}
std::optional<unsigned int> DiagnosticEngine::GetMaxNumOfDiags() const
{
    return impl->GetMaxNumOfDiags();
}
bool DiagnosticEngine::IsSupressedUnusedMain(const Diagnostic& diagnostic) noexcept
{
    return impl->IsSupressedUnusedMain(diagnostic);
}
void DiagnosticEngine::HandleDiagnostic(Diagnostic& diagnostic) noexcept
{
    impl->HandleDiagnostic(diagnostic);
}

void DiagnosticEngine::EmitCategoryDiagnostics(DiagCategory cate)
{
    impl->EmitCategoryDiagnostics(cate);
}

DiagEngineErrorCode DiagnosticEngine::GetCategoryDiagnosticsString(DiagCategory cate, std::string& diagOut)
{
    return impl->GetCategoryDiagnosticsString(cate, diagOut);
}

DiagEngineErrorCode DiagnosticEngine::GetCategoryDiagnosticInfos(DiagCategory cate, std::vector<DiagnosticInfo>& diagOut)
{
    return impl->GetCategoryDiagnosticInfos(cate, diagOut);
}

void DiagnosticEngine::EmitCategoryGroup()
{
    impl->EmitCategoryGroup();
}

void DiagnosticEngine::SetErrorCountLimit(std::optional<unsigned int> errorCountLimit)
{
    impl->SetErrorCountLimit(errorCountLimit);
}

std::vector<Diagnostic> DiagnosticEngine::GetCategoryDiagnostic(DiagCategory cate)
{
    return impl->GetCategoryDiagnostic(cate);
}

void DiagnosticEngine::Reset()
{
    impl->Reset();
}
    
void DiagnosticEngine::ClearError()
{
    impl->ClearError();
}

void DiagnosticEngine::SetDiagnoseStatus(bool enable)
{
    impl->SetDiagnoseStatus(enable);
}

bool DiagnosticEngine::GetDiagnoseStatus() const
{
    return impl->GetDiagnoseStatus();
}

void DiagnosticEngine::ReportErrorAndWarningCount()
{
    impl->ReportErrorAndWarningCount();
}
std::vector<Diagnostic> DiagnosticEngine::DisableDiagnose()
{
    return impl->DisableDiagnose();
}

void DiagnosticEngine::EnableDiagnose()
{
    impl->EnableDiagnose();
}
void DiagnosticEngine::EnableDiagnose(const std::vector<Diagnostic>& diags)
{
    impl->EnableDiagnose(diags);
}
std::vector<Diagnostic> DiagnosticEngine::ConsumeStoredDiags()
{
    return impl->ConsumeStoredDiags();
}
bool DiagnosticEngine::HardDisable() const
{
    return impl->HardDisable();
}
void DiagnosticEngine::CheckRange(DiagCategory cate, const Range& range)
{
    return impl->CheckRange(cate, range);
}
Range DiagnosticEngine::MakeRealRange(
    const AST::Node& node, const Position begin, const Position end, bool begLowBound) const
{
    return impl->MakeRealRange(node, begin, end, begLowBound);
}
} // namespace Cangjie
