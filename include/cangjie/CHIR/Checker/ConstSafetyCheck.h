// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_CHECKER_CONST_SAFETY_CHECK_H
#define CANGJIE_CHIR_CHECKER_CONST_SAFETY_CHECK_H

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/CHIR/Analysis/AnalysisWrapper.h"
#include "cangjie/CHIR/Analysis/ConstAnalysis.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"

namespace Cangjie::CHIR {
class ConstSafetyCheck {
public:
    using ConstAnalysisWrapper = AnalysisWrapper<ConstAnalysis<ConstStatePool>, ConstDomain>;

    explicit ConstSafetyCheck(ConstAnalysisWrapper* constAnalysisWrapper);

    void RunOnPackage(const Package& package, size_t threadNum) const;

    void RunOnFunc(const Func* func) const;

private:
    ConstAnalysisWrapper* analysisWrapper;
};
} // namespace Cangjie::CHIR

#endif
