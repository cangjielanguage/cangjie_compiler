// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares APILevelAnnoChecker class and APILevel information.
 */

#ifndef CHECK_APILEVEL_H
#define CHECK_APILEVEL_H

#include <set>
#include <string>
#include <unordered_set>

#include "cangjie/AST/Node.h"
#include "cangjie/AST/NodeX.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {
namespace APILevelCheck {

/**
 * It should same as cangjie code follow:
 * ```
 * package ohos
 * @Annotation
 * public class APILevel {
 *     // since
 *     public let level: UInt8
 *     public let atomicservice: Bool
 *     public let crossplatform: Bool
 *     // deprecated: 0 means the Api is not deprecated
 *     public let deprecated: UInt8
 *     public let form: Bool
 *     public let permission: ?PermissionValue
 *     public let stagemodelonly: Bool
 *     public let syscap: String
 *     public const init(level_val: UInt8, atomicservice!: Bool = false, crossplatform!: Bool = false,
 *         deprecated!: UInt8 = 0, form!: Bool = false, permission!: ?PermissionValue= None,
 *         stagemodelonly!: Bool = true, syscap!: String = "") {
 *         this.level = level_val
 *         this.atomicservice = atomicservice
 *         this.crossplatform = crossplatform
 *         this.deprecated = deprecated
 *         this.form = form
 *         this.permission = permission
 *         this.stagemodelonly = stagemodelonly
 *         this.syscap = syscap
 *     }
 * }
 * ```
 */

using LevelType = uint32_t;

struct APILevelAnnoInfo {
    struct PermissionValue {};

    LevelType level{0};
    bool atomicservice{false};
    bool crossplatform{false};
    uint8_t deprecated{0};
    bool form{false};
    PermissionValue* permission{nullptr};
    bool stagemodelonly{true};
    std::string syscap{""};
};

using SysCapSet = std::vector<std::string>;

class APILevelAnnoChecker {
public:
    APILevelAnnoChecker(CompilerInstance& ci, DiagnosticEngine& diag, ImportManager& importManager)
        : ci(ci), diag(diag), importManager(importManager)
    {
        ParseOption();
    }
    APILevelAnnoInfo Parse(const AST::Decl& decl);
    void Check(AST::Package& pkg);

private:
    void ParseOption() noexcept;
    void ParseJsonFile(const std::vector<uint8_t>& in) noexcept;
    bool CheckLevel(
        const AST::Node& node, const AST::Decl& target, const APILevelAnnoInfo& scopeAPILevel, bool reportDiag);
    bool CheckSyscap(
        const AST::Node& node, const AST::Decl& target, const APILevelAnnoInfo& scopeAPILevel, bool reportDiag);
    bool CheckNode(Ptr<AST::Node> node, APILevelAnnoInfo& scopeAPILevel, bool reportDiag = true);
    void CheckIfAvailableExpr(AST::IfAvailableExpr& iae, APILevelAnnoInfo& scopeAPILevel);
    bool IsAnnoAPILevel(Ptr<AST::Annotation> anno, const AST::Decl& decl);

private:
    CompilerInstance& ci;
    DiagnosticEngine& diag;
    ImportManager& importManager;

    LevelType globalLevel{0};
    SysCapSet intersectionSet;
    SysCapSet unionSet;
    std::unordered_map<Ptr<const AST::Decl>, APILevelAnnoInfo> levelCache;
    Ptr<ASTContext> ctx;

    bool optionWithLevel{false};
    bool optionWithSyscap{false};
};
} // namespace APILevelCheck
} // namespace Cangjie

#endif
