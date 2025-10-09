// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class ConditionalCompilationImpl.
 */

#ifndef CANGJIE_CONDITIONALCOMPILATION_CONDITIONALCOMPILATIONIMPL_H
#define CANGJIE_CONDITIONALCOMPILATION_CONDITIONALCOMPILATIONIMPL_H

#include <regex>

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/ConditionalCompilation/ConditionalCompilation.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Utils/FileUtil.h"

namespace Cangjie::AST {

const std::size_t CJC_VERSION_LENGTH = 6; // length of cjc version

class ConditionalCompilationImpl {
public:
    explicit ConditionalCompilationImpl(CompilerInstance* c);
    void HandleConditionalCompilation(const Package& root);
    void HandleFileConditionalCompilation(File& file);

private:
    inline std::string GetBackendType() const
    {
        if (backendType == Triple::BackendType::CJNATIVE) {
            return "cjnative";
        } else {
            return Triple::BackendToString(backendType);
        }
    }
    inline std::string GetArchType() const
    {
        return triple.ArchToString();
    }
    std::string GetOSType() const;
    inline std::string GetCJCVersion() const
    {
        std::string version = std::to_string(cjcVersion);
        return RefreshVersionStr(version);
    }
    inline std::string GetDebug() const
    {
        return std::to_string(static_cast<int>(debug));
    }
    inline std::string GetTest() const
    {
        return std::to_string(static_cast<int>(test));
    }
    std::string GetUserDefinedInfoByName(const std::string& name) const;
    inline auto GetPassedValues() const
    {
        return passedCondition;
    }
    std::string GetRelatedInfo(const std::string& target) const;

    CompilerInstance* ci{nullptr};
    Triple::BackendType backendType;
    Triple::Info triple;
    uint32_t cjcVersion;
    bool debug;
    bool test;
    std::unordered_map<std::string, std::string> passedCondition;

    bool EvalConditionExpr(const Expr& condition);

    bool ConditionCheck(const std::string& conditionStr, const Position& begin, const std::string& right);

    bool Eval(const BinaryExpr& expr, const std::string& left, const std::string& right) const;

    bool EvalBinaryExpr(const BinaryExpr& be);
    bool EvalParenExpr(const ParenExpr& pe);
    bool EvalUnaryExpr(const UnaryExpr& ue) const;
    bool EvalRefExpr(const RefExpr& re) const;

    bool EvalLogicBinaryExpr(const BinaryExpr& be);
    bool EvalJudgeBinaryExpr(const BinaryExpr& be);

    std::string RefreshVersionStr(std::string& version) const
    {
        while (version.size() < CJC_VERSION_LENGTH) {
            version = "0" + version;
        }
        return version;
    }

    template <typename T> bool EvalNodeCondition(Ptr<T> node);
};
} // namespace Cangjie::AST
#endif