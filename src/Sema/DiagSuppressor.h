// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements diagnostic suppressor class for semantic check.
 */

#ifndef CANGJIE_DIAGSUPPRESSOR_H
#define CANGJIE_DIAGSUPPRESSOR_H

#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie {
class DiagSuppressor {
public:
    explicit DiagSuppressor(DiagnosticEngine& diag) : diag(diag)
    {
        originDiagVec = diag.DisableDiagnose();
    }
    ~DiagSuppressor()
    {
        diag.EnableDiagnose(originDiagVec);
    }
    std::vector<Diagnostic> GetSuppressedDiag();
    void ReportDiag();
    bool HasError() const;

private:
    DiagnosticEngine& diag;
    std::vector<Diagnostic> originDiagVec;
};
} // namespace Cangjie

#endif // CANGJIE_DIAGSUPPRESSOR_H
