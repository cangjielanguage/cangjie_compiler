// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_CHECKER_UNREACHABLE_BRANCH_CHECK_H
#define CANGJIE_CHIR_CHECKER_UNREACHABLE_BRANCH_CHECK_H

#include "cangjie/CHIR/Analysis/AnalysisWrapper.h"
#include "cangjie/CHIR/Analysis/ConstAnalysis.h"
#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/DiagAdapter.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/Utils/TaskQueue.h"

namespace Cangjie::CHIR {

class UnreachableBranchCheck {
public:
    using ConstAnalysisWrapper = AnalysisWrapper<ConstAnalysis, ConstDomain>;
    explicit UnreachableBranchCheck(
        ConstAnalysisWrapper* constAnalysisWrapper, DiagAdapter& diag, const std::string& packageName);

    void RunOnPackage(const Package& package, size_t threadNum);

    void RunOnFunc(const Ptr<Func> func);

private:
    void PrintWarning(const Terminator& node, Block& block, std::set<Block*>& hasProcessed, bool isRecursive = false);

    DiagAdapter& diag;
    ConstAnalysisWrapper* analysisWrapper;

    const std::string& currentPackageName;
};

} // namespace Cangjie::CHIR

#endif
