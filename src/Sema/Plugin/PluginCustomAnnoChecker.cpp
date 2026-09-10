// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file provides the function of checking APILevel customized macros.
 */

#include "PluginCustomAnnoChecker.h"

#include <functional>
#include <iostream>
#include <iterator>
#include <stack>
#include <unordered_map>

#include "ParseJson.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/SafePointer.h"
#include "cangjie/Utils/StdUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace PluginCheck;

namespace {
constexpr std::string_view PKG_NAME_OHOS_LABELS = "ohos.labels";
constexpr std::string_view APILEVEL_ANNO_NAME = "APILevel";
constexpr std::string_view SINCE_IDENTIFIER = "since";
constexpr std::string_view LEVEL_IDENTIFIER = "level";
constexpr std::string_view SYSCAP_IDENTIFIER = "syscap";
constexpr std::string_view CFG_PARAM_LEVEL_NAME = "APILevel_level";
constexpr std::string_view CFG_PARAM_SYSCAP_NAME = "APILevel_syscap";
// For level check:
constexpr uint32_t IFAVAILABLE_LOWER_LIMITLEVEL = 19;
constexpr uint32_t APILEVEL_MAJOR_MIN = 1;
constexpr uint32_t APILEVEL_MAJOR_MAX = 99;

// For Annotation Hide:
constexpr std::string_view HIDE_ANNO_NAME = "Hide";
constexpr std::string_view HIDE_ARG_NAME = "isChecked";

/// Parse an integer or triple-string literal into an APILevelVersion. Accepts
/// `lce` of kind INTEGER (major-only, range [1,99]) or STRING (`"xx.yy.zz"`,
/// validated via TRIPLE_ONLY rule). Returns `std::nullopt` for any other kind
/// or out-of-range value; the caller is responsible for issuing diagnostics.
std::optional<APILevelVersion> ParseStrictLevelLiteral(const LitConstExpr& lce)
{
    if (lce.kind == LitConstKind::INTEGER) {
        auto value = Stoull(lce.stringValue);
        if (!value.has_value() || value.value() < APILEVEL_MAJOR_MIN || value.value() > APILEVEL_MAJOR_MAX) {
            return std::nullopt;
        }
        return APILevelVersion(static_cast<uint32_t>(value.value()));
    }
    if (lce.kind == LitConstKind::STRING) {
        return APILevelVersion::ParseChecked(lce.stringValue, APILevelVersion::ParseRule::TRIPLE_ONLY);
    }
    return std::nullopt;
}

/// Parse the version argument of a `@IfAvailable` expression. Requires `e` to
/// be a `LitConstExpr`; delegates to `ParseStrictLevelLiteral` which accepts
/// INTEGER (major-only) or STRING (`"xx.yy.zz"` triple). Returns `std::nullopt`
/// when `e` is not a literal or when the literal value is out of range.
std::optional<APILevelVersion> ParseIfAvailableArgVersion(const Expr& e)
{
    auto lce = DynamicCast<LitConstExpr>(&e);
    if (!lce) {
        return std::nullopt;
    }
    return ParseStrictLevelLiteral(*lce);
}

/// Parse the `level:` integer argument of `@IfAvailable`. Reads an INTEGER
/// literal from `e` (direct `LitConstExpr` or the right-hand side of a
/// `BinaryExpr`), validates it via `ParseStrictLevelLiteral`, and writes the
/// result into `apilevel.since` — keeping the minimum when called multiple
/// times. Emits `sema_only_literal_support` (ERROR) when the argument is not
/// an integer literal, and `sema_apilevel_invalid_version_format` (ERROR) when
/// the integer is outside the valid major-version range [1,99].
void ParseLevel(const Expr& e, PluginCustomAnnoInfo& apilevel, DiagnosticEngine& diag)
{
    Ptr<const LitConstExpr> lce = nullptr;
    if (e.astKind == ASTKind::BINARY_EXPR) {
        auto be = StaticCast<BinaryExpr>(&e);
        CJC_NULLPTR_CHECK(be->rightExpr);
        lce = DynamicCast<LitConstExpr>(be->rightExpr.get());
    } else if (e.astKind == ASTKind::LIT_CONST_EXPR) {
        lce = StaticCast<LitConstExpr>(&e);
    }
    if (!lce || lce->kind != LitConstKind::INTEGER) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_only_literal_support, e, "integer");
        return;
    }
    auto newLevel = ParseStrictLevelLiteral(*lce);
    if (!newLevel.has_value()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_apilevel_invalid_version_format, e, lce->stringValue.c_str());
        return;
    }
    if (apilevel.since.IsZero()) {
        apilevel.since = *newLevel;
    } else if (*newLevel < apilevel.since) {
        apilevel.since = *newLevel;
    }
}

/// Parse the `since:` string argument of `@!APILevel`. Reads a STRING literal
/// from `e` (direct `LitConstExpr` or the right-hand side of a `BinaryExpr`),
/// validates it via `APILevelVersion::ParseChecked` under MAJOR_OR_TRIPLE rule
/// (accepts `"xx"` or `"xx.yy.zz"`), and writes the result into `apilevel.since`
/// — keeping the minimum when called multiple times. Emits
/// `sema_only_literal_support` (ERROR) when the argument is not a string literal,
/// and `sema_apilevel_invalid_version_format` (ERROR) on parse failure.
void ParseSince(const Expr& e, PluginCustomAnnoInfo& apilevel, DiagnosticEngine& diag)
{
    Ptr<const LitConstExpr> lce = nullptr;
    if (e.astKind == ASTKind::BINARY_EXPR) {
        auto be = StaticCast<BinaryExpr>(&e);
        CJC_NULLPTR_CHECK(be->rightExpr);
        lce = DynamicCast<LitConstExpr>(be->rightExpr.get());
    } else if (e.astKind == ASTKind::LIT_CONST_EXPR) {
        lce = StaticCast<LitConstExpr>(&e);
    }
    if (!lce || lce->kind != LitConstKind::STRING) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_only_literal_support, e, "string");
        return;
    }
    auto newLevel = APILevelVersion::ParseChecked(lce->stringValue, APILevelVersion::ParseRule::MAJOR_OR_TRIPLE);
    if (!newLevel.has_value()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_apilevel_invalid_version_format, e, lce->stringValue.c_str());
        return;
    }
    if (apilevel.since.IsZero()) {
        apilevel.since = *newLevel;
    } else if (*newLevel < apilevel.since) {
        apilevel.since = *newLevel;
    }
}

/// Parse the `syscap:` string argument of `@IfAvailable`. Reads a STRING literal
/// from `e` — either a direct `LitConstExpr` or the sole argument of a
/// `CallExpr` wrapper — and writes the raw syscap identifier string into
/// `apilevel.syscap`. Emits `sema_only_literal_support` (ERROR) when the
/// argument is not a string literal.
void ParseSysCap(const Expr& e, PluginCustomAnnoInfo& apilevel, DiagnosticEngine& diag)
{
    Ptr<const LitConstExpr> lce = nullptr;
    if (e.astKind == ASTKind::CALL_EXPR) {
        auto ce = StaticCast<CallExpr>(&e);
        CJC_ASSERT(ce->args.size() == 1 && ce->args[0]->expr);
        lce = DynamicCast<LitConstExpr>(ce->args[0]->expr.get());
    } else if (e.astKind == ASTKind::LIT_CONST_EXPR) {
        lce = StaticCast<LitConstExpr>(&e);
    }
    if (!lce || lce->kind != LitConstKind::STRING) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_only_literal_support, e, "string");
        return;
    }
    apilevel.syscap = lce->stringValue;
}

/// Parse the `isChecked:` boolean argument of `@Hide`. Reads a BOOL literal
/// from `e` (must be a `LitConstExpr`) and OR-accumulates the value into
/// `apilevel.hasHideAnno`, so that multiple `@Hide` annotations on the same
/// declaration are coalesced — the field becomes true if any annotation has
/// `isChecked: true`. Emits `sema_only_literal_support` (ERROR) when the
/// argument is not a boolean literal.
void ParseCheckingHide(const Expr& e, PluginCustomAnnoInfo& apilevel, DiagnosticEngine& diag)
{
    Ptr<const LitConstExpr> lce = nullptr;
    if (e.astKind == ASTKind::LIT_CONST_EXPR) {
        lce = StaticCast<LitConstExpr>(&e);
    }
    if (!lce || lce->kind != LitConstKind::BOOL) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_only_literal_support, e, "Bool");
        return;
    }
    apilevel.hasHideAnno =
        (apilevel.hasHideAnno.has_value() && apilevel.hasHideAnno.value()) || lce->constNumValue.asBoolean;
}

using ParseNameParamFunc = std::function<void(const Expr&, PluginCustomAnnoInfo&, DiagnosticEngine&)>;
std::unordered_map<std::string_view, ParseNameParamFunc> parseNameParam = {
    {SINCE_IDENTIFIER, ParseSince},
    {LEVEL_IDENTIFIER, ParseLevel},
    {SYSCAP_IDENTIFIER, ParseSysCap},
    {HIDE_ARG_NAME, ParseCheckingHide},
};

void ClearAnnoInfoOfDepPkg(ImportManager& importManager)
{
    auto clearAnno = [](Ptr<Node> node) {
        auto decl = DynamicCast<Decl>(node);
        if (!decl) {
            return VisitAction::WALK_CHILDREN;
        }
        auto isCustomAnno = [](auto& a) { return a->kind == AnnotationKind::CUSTOM; };
        decl->annotations.erase(
            std::remove_if(decl->annotations.begin(), decl->annotations.end(), isCustomAnno), decl->annotations.end());
        return VisitAction::WALK_CHILDREN;
    };
    for (auto& [fullPackageName, _] : importManager.GetDepPkgCjoPaths()) {
        auto depPkg = importManager.GetPackage(fullPackageName);
        if (!depPkg) {
            continue;
        }
        Walker(depPkg, clearAnno).Walk();
    }
}

void MarkFuncDeclMembersAsExternalWeak(FuncDecl& fd)
{
    for (auto& param : fd.funcBody->paramLists[0]->params) {
        if (param->desugarDecl) {
            param->desugarDecl->linkage = Linkage::EXTERNAL_WEAK;
        }
    }
    if (fd.propDecl) {
        fd.propDecl->linkage = Linkage::EXTERNAL_WEAK;
    }
}

void MarkPropDeclMembersAsExternalWeak(PropDecl& pd)
{
    for (auto& getter : pd.getters) {
        if (getter) {
            getter->linkage = Linkage::EXTERNAL_WEAK;
        }
    }
    for (auto& setter : pd.setters) {
        if (setter) {
            setter->linkage = Linkage::EXTERNAL_WEAK;
        }
    }
}

void SetLinkageAsExternalWeak(Ptr<Decl> target)
{
    if (!target) {
        return;
    }
    target->linkage = Linkage::EXTERNAL_WEAK;
    if (auto fd = DynamicCast<FuncDecl>(target)) {
        MarkFuncDeclMembersAsExternalWeak(*fd);
    } else if (auto md = DynamicCast<MacroDecl>(target)) {
        if (md->desugarDecl) {
            md->desugarDecl->linkage = Linkage::EXTERNAL_WEAK;
        }
    } else if (auto pd = DynamicCast<PropDecl>(target)) {
        MarkPropDeclMembersAsExternalWeak(*pd);
    }
}

void MarkTargetAsExternalWeak(Ptr<Node> node)
{
    if (!node) {
        return;
    }
    Ptr<Decl> target = nullptr;
    if (node->GetTarget()) {
        target = node->GetTarget();
    } else if (auto ce = DynamicCast<CallExpr>(node); ce && ce->resolvedFunction) {
        target = ce->resolvedFunction;
    }
    if (!target) {
        return;
    }
    SetLinkageAsExternalWeak(target);
    if (target->outerDecl && target->outerDecl->IsNominalDecl()) {
        SetLinkageAsExternalWeak(target->outerDecl);
    }
}

inline std::string GetModuleName(const std::string& fullPackageName)
{
    return fullPackageName.substr(0, fullPackageName.find('.'));
}

std::string FormatSyscapsString(const std::string& scopeSyscap, const SysCapSet& syscapSet)
{
    std::stringstream scopeSyscapsStr;
    // 3 is maximum number of syscap limit.
    size_t limit = 3;
    size_t count = 0;
    if (!scopeSyscap.empty() && syscapSet.find(scopeSyscap) == syscapSet.end()) {
        scopeSyscapsStr << scopeSyscap;
        ++count;
    }
    for (const auto& syscap : syscapSet) {
        if (count >= limit) {
            scopeSyscapsStr << "...";
            break;
        }
        if (count > 0) {
            scopeSyscapsStr << ", ";
        }
        scopeSyscapsStr << syscap;
        ++count;
    }
    return scopeSyscapsStr.str();
}
} // namespace

bool PluginCustomAnnoChecker::ParseJsonFile(const std::vector<uint8_t>& in) noexcept
{
    size_t startPos = static_cast<size_t>(std::find(in.begin(), in.end(), '{') - in.begin());
    auto root = ParseJsonObject(startPos, in);
    auto deviceSysCapObj = GetJsonObject(root, "deviceSysCap", 0);
    if (!deviceSysCapObj) {
        return false;
    }
    std::map<std::string, SysCapSet> dev2SyscapsMap;
    for (auto& subObj : deviceSysCapObj->pairs) {
        SysCapSet syscapsOneDev;
        for (auto path : subObj->valueStr) {
            std::vector<uint8_t> buffer;
            std::string failedReason;
            FileUtil::ReadBinaryFileToBuffer(path, buffer, failedReason);
            if (!failedReason.empty()) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, path, failedReason);
                return false;
            }
            startPos = static_cast<size_t>(std::find(buffer.begin(), buffer.end(), '{') - buffer.begin());
            auto rootOneDevice = ParseJsonObject(startPos, buffer);
            auto curSyscaps = GetJsonString(rootOneDevice, "SysCaps");
            for (auto& syscap : curSyscaps) {
                syscapsOneDev.insert(syscap);
            }
        }
        dev2SyscapsMap.emplace(subObj->key, syscapsOneDev);
    }
    std::optional<SysCapSet> lastSyscap = std::nullopt;
    for (auto& dev2Syscaps : dev2SyscapsMap) {
        SysCapSet& curSyscaps = dev2Syscaps.second;
        SysCapSet intersection;
        if (lastSyscap.has_value()) {
            std::set_intersection(lastSyscap.value().begin(), lastSyscap.value().end(), curSyscaps.begin(),
                curSyscaps.end(), std::inserter(intersection, intersection.end()));
        } else {
            intersection = curSyscaps;
        }
        lastSyscap = intersection;
        for (auto& syscap : curSyscaps) {
            unionSet.insert(syscap);
        }
    }
    if (lastSyscap) {
        intersectionSet = std::move(*lastSyscap);
    }
    return true;
}

void PluginCustomAnnoChecker::ParseOption() noexcept
{
    auto& option = ci.invocation.globalOptions;
    auto found = option.passedWhenKeyValue.find(std::string(CFG_PARAM_LEVEL_NAME));
    if (found != option.passedWhenKeyValue.end()) {
        if (found->second == "0") {
            globalLevel = APILevelVersion();
            optionWithLevel = false;
        } else {
            auto parsedLevel =
                APILevelVersion::ParseChecked(found->second, APILevelVersion::ParseRule::MAJOR_OR_TRIPLE);
            if (!parsedLevel.has_value()) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_apilevel_invalid_version_format, DEFAULT_POSITION, found->second.c_str());
                return;
            }
            globalLevel = *parsedLevel;
            optionWithLevel = !globalLevel.IsZero();
        }
    }
    found = option.passedWhenKeyValue.find(std::string(CFG_PARAM_SYSCAP_NAME));
    if (found != option.passedWhenKeyValue.end()) {
        auto syscapsCfgPath = found->second;
        std::vector<uint8_t> jsonContent;
        std::string failedReason;
        FileUtil::ReadBinaryFileToBuffer(syscapsCfgPath, jsonContent, failedReason);
        if (!failedReason.empty()) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, syscapsCfgPath, failedReason);
            return;
        }
        optionWithSyscap = ParseJsonFile(jsonContent);
    }
}

bool PluginCustomAnnoChecker::IsAnnoAPILevel(Ptr<Annotation> anno, [[maybe_unused]] const Decl& decl)
{
    if (ctx && ctx->curPackage && ctx->curPackage->fullPackageName == PKG_NAME_OHOS_LABELS) {
        return anno->identifier == APILEVEL_ANNO_NAME;
    }
    if (!anno) {
        return false;
    }
    auto target = anno->baseExpr ? anno->baseExpr->GetTarget() : nullptr;
    if (target) {
        // With semantic info, check by target and its package name.
        return target->GetFullPackageName() == PKG_NAME_OHOS_LABELS &&
            target->outerDecl &&
            target->outerDecl->identifier == APILEVEL_ANNO_NAME;
    }
    // Without semantic info, check by annotation name only.
    return anno->identifier == APILEVEL_ANNO_NAME;
}

bool PluginCustomAnnoChecker::IsAnnoHide(Ptr<Annotation> anno)
{
    if (ctx && ctx->curPackage && ctx->curPackage->fullPackageName == PKG_NAME_OHOS_LABELS) {
        return anno->identifier == HIDE_ANNO_NAME;
    }
    if (!anno) {
        return false;
    }
    auto target = anno->baseExpr ? anno->baseExpr->GetTarget() : nullptr;
    if (target) {
        // With semantic info, check by target and its package name.
        return target->GetFullPackageName() == PKG_NAME_OHOS_LABELS && target->outerDecl &&
            target->outerDecl->identifier == HIDE_ANNO_NAME;
    }
    // Without semantic info, check by annotation name only.
    return anno->identifier == HIDE_ANNO_NAME;
}

void PluginCustomAnnoChecker::ParseHideArg(const Annotation& anno, PluginCustomAnnoInfo& annoInfo)
{
    if (anno.args.empty() || !anno.args[0] || !anno.args[0]->expr) {
        annoInfo.hasHideAnno = false;
        return;
    }
    std::string argName = anno.args[0]->name.Val();
    if (argName != HIDE_ARG_NAME) {
        // Should diagnostic before here.
        return;
    }
    parseNameParam[argName](*anno.args[0]->expr.get(), annoInfo, diag);
}

void PluginCustomAnnoChecker::ParseAPILevelArgs(
    const Decl& decl, const Annotation& anno, PluginCustomAnnoInfo& annoInfo)
{
    for (size_t i = 0; i < anno.args.size(); ++i) {
        std::string_view argName = anno.args[i]->name.Val();
        // Detect legacy integer-form usage:
        //   @!APILevel[20]                  (positional integer)
        //   @!APILevel[level: 20]           (named-level with integer value, old-style call)
        //   @!APILevel[since: 20]           (named-since with integer value, edge case)
        // Issue #824 dropped support for integer version literals on @!APILevel. The
        // dedicated diagnostic gives users a migration-actionable message regardless
        // of which legacy invocation form they hit.
        if (auto lce = DynamicCast<LitConstExpr>(anno.args[i]->expr.get());
            lce && lce->kind == LitConstKind::INTEGER &&
            (argName.empty() || argName == SINCE_IDENTIFIER || argName == LEVEL_IDENTIFIER)) {
            // Anchor at the annotation, not the arg literal — the engine dedups by
            // (range, severity) and would drop our migration hint against
            // sema_need_named_argument otherwise.
            diag.DiagnoseRefactor(
                DiagKindRefactor::sema_apilevel_integer_form_unsupported, anno);
            continue;
        }
        if (parseNameParam.count(argName) <= 0) {
            continue;
        }
        std::string preSyscap = annoInfo.syscap;
        parseNameParam[argName](*anno.args[i]->expr.get(), annoInfo, diag);
        if (!preSyscap.empty() && preSyscap != annoInfo.syscap) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_apilevel_multi_diff_syscap, decl);
        }
    }
    // In the APILevel definition, 'since' is the only argument without a default
    // value. Emitting this warning here means the APILevel annotation produced no
    // valid 'since' version — which could be because the user (or a stale .cj.d
    // declaration) wrote no 'since:' at all, or the value failed to parse. Keep
    // the warning unconditional on 'since == 0'; precise errors (invalid_version_format,
    // integer_form_unsupported) fire elsewhere and complement, not replace, this signal.
    if (annoInfo.since.IsZero()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_apilevel_missing_arg, anno.begin, "since!: String");
    }
}

void PluginCustomAnnoChecker::CheckHideOfExtendDecl(const Decl& decl, const PluginCustomAnnoInfo& annoInfo)
{
    if (decl.astKind != ASTKind::EXTEND_DECL) {
        return;
    }
    auto extendedDecl = Ty::GetDeclPtrOfTy(decl.GetTy());
    if (!extendedDecl) {
        return;
    }
    PluginCustomAnnoInfo extendedAnnoInfo;
    Parse(*extendedDecl, extendedAnnoInfo);

    if (extendedAnnoInfo.hasHideAnno.has_value() && !annoInfo.hasHideAnno.has_value()) {
        // @!Hide class A{}; extend A {} -> error
        // class A{}; @!Hide extend A {} -> ok
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_hide_missing_hide, decl);
        builder.AddNote(*extendedDecl, "the extended declaration is marked with '@!Hide'");
    } else if (extendedAnnoInfo.hasHideAnno.has_value() && annoInfo.hasHideAnno.has_value() &&
        extendedAnnoInfo.hasHideAnno.value() && !annoInfo.hasHideAnno.value()) {
        // @!Hide[isChecked: true] class A {}; @!Hide[isChecked: false] extend A {} -> error
        // @!Hide[isChecked: false] class A {}; @!Hide[isChecked: true] extend A {} -> ok
        // @!Hide[isChecked: false] class A {}; @!Hide[isChecked: false] extend A {} -> ok
        // @!Hide[isChecked: true] class A {}; @!Hide[isChecked: true] extend A {} -> ok
        auto builder = diag.DiagnoseRefactor(
            DiagKindRefactor::sema_hide_diff_param, decl, annoInfo.hasHideAnno.value() ? "true" : "false");
        builder.AddNote(*extendedDecl, "should be same with");
    }

    if (!annoInfo.hasHideAnno.has_value()) {
        return;
    }
    for (auto member : decl.GetMemberDeclPtrs()) {
        PluginCustomAnnoInfo memberAnnoInfo;
        Parse(*member, memberAnnoInfo);
        if (memberAnnoInfo.hasHideAnno.has_value() &&
            memberAnnoInfo.hasHideAnno.value() != annoInfo.hasHideAnno.value()) {
            auto builder = diag.DiagnoseRefactor(
                DiagKindRefactor::sema_hide_diff_param, *member, memberAnnoInfo.hasHideAnno.value() ? "true" : "false");
            builder.AddNote(decl, "should be same with");
        }
    }
}

void PluginCustomAnnoChecker::CheckHideOfOverrideFunction(const Decl& decl, const PluginCustomAnnoInfo& annoInfo)
{
    if (decl.astKind != ASTKind::FUNC_DECL || !decl.outerDecl) {
        return;
    }
    auto fd = StaticCast<FuncDecl>(&decl);
    CJC_NULLPTR_CHECK(ci.typeManager);
    std::optional<bool> functionWithHide = annoInfo.hasHideAnno;
    if (!functionWithHide.has_value()) {
        PluginCustomAnnoInfo outerAnnoInfo;
        Parse(*decl.outerDecl, outerAnnoInfo);
        if (outerAnnoInfo.hasHideAnno.has_value()) {
            functionWithHide = outerAnnoInfo.hasHideAnno.value();
        }
    }

    auto overriddenFd = ci.typeManager->GetTopOverriddenFuncDecl(fd);
    // Already the top-most declaration: nothing to compare against.
    if (!overriddenFd || overriddenFd == fd) {
        return;
    }
    PluginCustomAnnoInfo overriddenAnnoInfo;
    Parse(*overriddenFd, overriddenAnnoInfo);
    std::optional<bool> overriddenFdWithHide = overriddenAnnoInfo.hasHideAnno;
    if (!overriddenFdWithHide.has_value()) {
        CJC_NULLPTR_CHECK(overriddenFd->outerDecl);
        PluginCustomAnnoInfo outerAnnoInfo;
        Parse(*overriddenFd->outerDecl, outerAnnoInfo);
        if (outerAnnoInfo.hasHideAnno.has_value()) {
            overriddenFdWithHide = outerAnnoInfo.hasHideAnno.value();
        }
    }

    if (functionWithHide.has_value() && !overriddenFdWithHide.has_value()) {
        // func f(): Unit {}; @!Hide override func f(): Unit {} -> error
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_hide_missing_hide, *overriddenFd);
        builder.AddNote(*fd, "the override function is marked with '@!Hide'");
    } else if (!functionWithHide.has_value() && overriddenFdWithHide.has_value()) {
        // @!Hide func f(): Unit {}; override func f(): Unit {} -> ok for now
    } else if (functionWithHide.has_value() && functionWithHide.value() != overriddenFdWithHide.value()) {
        // @!Hide[isChecked: true] func f(): Unit {}; @!Hide[isChecked: false] override func f(): Unit {} -> error
        // @!Hide[isChecked: false] func f(): Unit {}; @!Hide[isChecked: true] override func f(): Unit {} -> error
        // @!Hide[isChecked: true] func f(): Unit {}; @!Hide[isChecked: true] override func f(): Unit {} -> ok
        // @!Hide[isChecked: false] func f(): Unit {}; @!Hide[isChecked: false] override func f(): Unit {} -> ok
        auto builder = diag.DiagnoseRefactor(
            DiagKindRefactor::sema_hide_diff_param, decl, functionWithHide.value() ? "true" : "false");
        builder.AddNote(*overriddenFd, "should be same with");
    }
}

void PluginCustomAnnoChecker::MergeCachedAnnoInfo(const PluginCustomAnnoInfo& cached, PluginCustomAnnoInfo& annoInfo)
{
    annoInfo.since = annoInfo.since.IsZero() ? cached.since : std::min(cached.since, annoInfo.since);
    annoInfo.syscap = cached.syscap;
    if (cached.hasHideAnno.has_value()) {
        annoInfo.hasHideAnno = cached.hasHideAnno;
    } else if (!annoInfo.hasHideAnno.has_value()) {
        annoInfo.hasHideAnno = std::nullopt;
    }
    // else: keep the existing annoInfo.hasHideAnno value.
}

void PluginCustomAnnoChecker::ProcessOneAnnotation(
    const Decl& decl, Ptr<Annotation> anno, bool& hideExist, PluginCustomAnnoInfo& annoInfo)
{
    if (IsAnnoHide(anno)) {
        if (hideExist) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_hide_multi_annotation, decl);
            return;
        }
        hideExist = true;
        if (auto param = DynamicCast<FuncParam>(&decl); param && !param->isMemberParam) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_hide_at_func_param, decl);
            return;
        }
        if (!anno->isCompileTimeVisible) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_hide_compile_time_invisible, *anno);
        }
        ParseHideArg(*anno, annoInfo);
    } else if (IsAnnoAPILevel(anno, decl)) {
        ParseAPILevelArgs(decl, *anno, annoInfo);
    }
}

void PluginCustomAnnoChecker::Parse(const Decl& decl, PluginCustomAnnoInfo& annoInfo)
{
    if (auto found = levelCache.find(&decl); found != levelCache.end()) {
        MergeCachedAnnoInfo(found->second, annoInfo);
        return;
    }
    bool hideExist = false;
    for (auto& anno : decl.annotations) {
        if (!anno) {
            continue;
        }
        ProcessOneAnnotation(decl, anno, hideExist, annoInfo);
    }
    levelCache[&decl] = annoInfo;
    CheckHideOfExtendDecl(decl, annoInfo);
    CheckHideOfOverrideFunction(decl, annoInfo);
}

bool PluginCustomAnnoChecker::CheckLevel(
    const Decl& target, const PluginCustomAnnoInfo& scopeAnnoInfo, DiagConfig diagCfg)
{
    if (!optionWithLevel) {
        return true;
    }
    APILevelVersion scopeLevel = !scopeAnnoInfo.since.IsZero() ? scopeAnnoInfo.since : globalLevel;
    PluginCustomAnnoInfo targetAPILevel;
    Parse(target, targetAPILevel);
    if (targetAPILevel.since > scopeLevel) {
        if (diagCfg.reportDiag && !diagCfg.message.empty() && !diagCfg.node->begin.IsZero()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_apilevel_ref_higher, *diagCfg.node, diagCfg.message[0],
                targetAPILevel.since.ToDisplayString(), scopeLevel.ToDisplayString());
        }
        return false;
    }
    return true;
}

bool PluginCustomAnnoChecker::CheckSyscap(
    const Decl& target, const PluginCustomAnnoInfo& scopeAnnoInfo, DiagConfig diagCfg)
{
    if (!optionWithSyscap) {
        return true;
    }
    PluginCustomAnnoInfo targetAPILevel;
    Parse(target, targetAPILevel);
    std::string targetLevel = targetAPILevel.syscap;
    if (targetLevel.empty()) {
        return true;
    }
    // Create a lambda for diagnostic purposes that only creates a temporary collection when needed.
    auto diagForSyscap = [this, &diagCfg, &targetLevel](
                             const std::string& scopeSyscap, const SysCapSet& syscapSet, DiagKindRefactor kind) {
        auto builder = diag.DiagnoseRefactor(kind, *diagCfg.node, targetLevel);
        std::string formattedSyscaps = FormatSyscapsString(scopeSyscap, syscapSet);
        builder.AddNote("the following syscaps are supported: " + formattedSyscaps);
    };

    // Check unionSet (all possible syscaps)
    // If scopeAnnoInfo.syscap is not empty and equals targetLevel, it is considered a match.
    bool inUnionSet = (unionSet.find(targetLevel) != unionSet.end()) ||
        (!scopeAnnoInfo.syscap.empty() && scopeAnnoInfo.syscap == targetLevel);
    if (!inUnionSet) {
        if (diagCfg.reportDiag && !diagCfg.node->begin.IsZero()) {
            diagForSyscap(scopeAnnoInfo.syscap, unionSet, DiagKindRefactor::sema_apilevel_syscap_error);
        }
        return false;
    }

    // Check intersectionSet (all syscaps supported by all devices)
    // If scopeAnnoInfo.syscap is not empty and equals targetLevel, it is considered a match.
    bool inIntersectionSet = (intersectionSet.find(targetLevel) != intersectionSet.end()) ||
        (!scopeAnnoInfo.syscap.empty() && scopeAnnoInfo.syscap == targetLevel);
    if (!inIntersectionSet) {
        if (diagCfg.reportDiag && !diagCfg.node->begin.IsZero()) {
            diagForSyscap(scopeAnnoInfo.syscap, intersectionSet, DiagKindRefactor::sema_apilevel_syscap_warning);
        }
        return false;
    }
    return true;
}

bool PluginCustomAnnoChecker::CheckCheckingHide(const Decl& target, DiagConfig diagCfg)
{
    PluginCustomAnnoInfo targetPluginAnnoInfo;
    Parse(target, targetPluginAnnoInfo);
    if (targetPluginAnnoInfo.hasHideAnno.has_value() && targetPluginAnnoInfo.hasHideAnno.value() &&
        curModuleName != GetModuleName(target.GetFullPackageName())) {
        if (diagCfg.reportDiag && !diagCfg.message.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_undeclared_identifier, *diagCfg.node, diagCfg.message[0]);
        }
        return false;
    }
    return true;
}

void PluginCustomAnnoChecker::MarkClassLikeMembersAsExternalWeakIfNeeded(
    Decl& target, const PluginCustomAnnoInfo& scopeAnnoInfo)
{
    if (!target.IsClassLikeDecl() || !target.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    // Only check open or abstract class or interface.
    if (target.astKind == ASTKind::CLASS_DECL && !target.TestAnyAttr(Attribute::ABSTRACT, Attribute::OPEN)) {
        return;
    }
    auto cld = StaticCast<ClassLikeDecl>(&target);
    for (auto member : cld->GetMemberDeclPtrs()) {
        // Constructor, finalizer, and abstract(function without funcbody) function or property must not be in vtable.
        if (!member->IsFuncOrProp() ||
            member->TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::FINALIZER, Attribute::ABSTRACT)) {
            continue;
        }
        auto legalAPI = CheckLevel(*member, scopeAnnoInfo, {.reportDiag = false});
        legalAPI = legalAPI && CheckSyscap(*member, scopeAnnoInfo, {.reportDiag = false});
        if (!legalAPI) {
            SetLinkageAsExternalWeak(member);
        }
    }
    for (auto super : cld->GetAllSuperDecls()) {
        if (super == cld) {
            continue;
        }
        MarkClassLikeMembersAsExternalWeakIfNeeded(*super, scopeAnnoInfo);
    }
}

bool PluginCustomAnnoChecker::CheckNode(Ptr<Node> node, PluginCustomAnnoInfo& scopeAnnoInfo, bool reportDiag)
{
    if (!node) {
        return true;
    }
    auto target = node->GetTarget();
    if (auto ce = DynamicCast<CallExpr>(node); ce && ce->resolvedFunction) {
        if (ce->callKind == CallKind::CALL_SUPER_FUNCTION) {
            // The check has been completed in the parent type checker.
            return false;
        }
        target = ce->resolvedFunction;
    }
    if (!target) {
        return true;
    }
    bool ret = true;
    if (target->outerDecl) {
        auto identifier = target->outerDecl->identifier.Val();
        if (identifier.empty()) {
            identifier = target->identifier.Val();
        }
        ret = ret && CheckCheckingHide(*target->outerDecl, {reportDiag, node, {identifier}});
        ret = ret && CheckLevel(*target->outerDecl, scopeAnnoInfo, {reportDiag, node, {identifier}});
        ret = ret && CheckSyscap(*target->outerDecl, scopeAnnoInfo, {reportDiag, node, {}});
        if (!ret) {
            return false;
        }
    }
    // The priority of the Hide check is higher than that of the APILevel.
    ret = ret && CheckCheckingHide(*target, {reportDiag, node, {target->identifier.Val()}});
    ret = ret && CheckLevel(*target, scopeAnnoInfo, {reportDiag, node, {target->identifier.Val()}});
    ret = ret && CheckSyscap(*target, scopeAnnoInfo, {reportDiag, node, {target->identifier.Val()}});
    // When an external user inherits a parent class, but the virtual functions in the parent class
    // do not meet the APILevel requirements, those virtual functions will exist in the vtable
    // of downstream packages. They should be marked as External_weak, indicating they are not
    // required to exist.
    MarkClassLikeMembersAsExternalWeakIfNeeded(*target, scopeAnnoInfo);
    return ret;
}

bool PluginCustomAnnoChecker::TryBuildIfAvailableScopeFromIfExpr(
    const IfExpr& ife, PluginCustomAnnoInfo& ifscopeAnnoInfo)
{
    if (!ife.condExpr) {
        return false;
    }
    if (auto ce = DynamicCast<CallExpr>(ife.condExpr.get()); ce && ce->resolvedFunction &&
        ce->resolvedFunction->identifier.Val() == "canIUse") {
        ParseSysCap(*ce, ifscopeAnnoInfo, diag);
        return !ifscopeAnnoInfo.syscap.empty();
    }
    auto be = DynamicCast<BinaryExpr>(ife.condExpr.get());
    if (!be || !be->leftExpr) {
        return false;
    }
    auto ce = DynamicCast<CallExpr>(be->leftExpr.get());
    if (!ce || !ce->resolvedFunction || ce->resolvedFunction->identifier.Val() != "sdkApiVersion") {
        return false;
    }
    ParseSince(*be, ifscopeAnnoInfo, diag);
    return !ifscopeAnnoInfo.since.IsZero();
}

void PluginCustomAnnoChecker::WalkBranchBody(
    Ptr<Block> body, const std::function<VisitAction(Ptr<Node>)>& checker)
{
    if (!body) {
        return;
    }
    for (auto& node : body->body) {
        Walker(node.get(), checker).Walk();
    }
}

std::function<VisitAction(Ptr<Node>)> PluginCustomAnnoChecker::MakeIfBranchChecker(
    PluginCustomAnnoInfo& ifscopeAnnoInfo, PluginCustomAnnoInfo& scopeAnnoInfo)
{
    return [this, &ifscopeAnnoInfo, &scopeAnnoInfo](Ptr<Node> node) -> VisitAction {
        if (auto e = DynamicCast<IfAvailableExpr>(node)) {
            CheckIfAvailableExpr(*e, ifscopeAnnoInfo);
            return VisitAction::SKIP_CHILDREN;
        }
        // Plain 'if (DeviceInfo.sdkApiVersion >= N)' or 'if (canIUse(...))' inside an
        // @IfAvailable branch is treated as ordinary control flow, not a nested API gate.
        // Only @IfAvailable itself opts a scope into Sema-level gate semantics; promoting
        // arbitrary user-written runtime checks here would silently extend that contract.
        // If the reference meets the 'IfAvailable' condition but does not meet the global APILevel
        // configuration, set linkage to 'EXTERNAL_WEAK'.
        auto ret = CheckNode(node, ifscopeAnnoInfo);
        if (ret && !CheckNode(node, scopeAnnoInfo, false)) {
            MarkTargetAsExternalWeak(node);
        }
        if (!ret) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
}

std::function<VisitAction(Ptr<Node>)> PluginCustomAnnoChecker::MakeElseBranchChecker(
    PluginCustomAnnoInfo& scopeAnnoInfo)
{
    return [this, &scopeAnnoInfo](Ptr<Node> node) -> VisitAction {
        if (auto e = DynamicCast<IfAvailableExpr>(node)) {
            CheckIfAvailableExpr(*e, scopeAnnoInfo);
            return VisitAction::SKIP_CHILDREN;
        }
        // Mirror MakeIfBranchChecker: only @IfAvailable opts a scope into gate
        // semantics. Plain 'if (DeviceInfo.sdkApiVersion >= N)' / 'if (canIUse(...))'
        // inside an else branch is left as ordinary control flow.
        if (!CheckNode(node, scopeAnnoInfo)) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
}

bool PluginCustomAnnoChecker::ParseIfAvailableLevelArg(FuncArg& arg, PluginCustomAnnoInfo& ifscopeAnnoInfo)
{
    if (!DynamicCast<LitConstExpr>(arg.expr.get())) {
        return false;
    }
    auto parsedLevel = ParseIfAvailableArgVersion(*arg.expr);
    if (!parsedLevel.has_value()) {
        auto lce = StaticCast<LitConstExpr>(arg.expr.get());
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_apilevel_invalid_version_format, arg, lce->stringValue.c_str());
        return false;
    }
    ifscopeAnnoInfo.since = *parsedLevel;
    return true;
}

/// Validate that arg and both lambda bodies are present; also confirm arg->expr exists for
/// non-syscap arguments and that the argument name is a known parseNameParam key.
/// Returns false if any guard fails and CheckIfAvailableExpr should bail out early.
static bool ValidateIfAvailableArgs(const FuncArg* arg, const LambdaExpr* lambda1,
    const LambdaExpr* lambda2)
{
    if (!arg || !lambda1 || !lambda1->funcBody || !lambda1->funcBody->body || !lambda2 ||
        !lambda2->funcBody || !lambda2->funcBody->body) {
        return false;
    }
    // For the syscap case, DesugarIfAvailableSyscapCondition moves arg->expr into the desugared
    // canIUse(...) call, leaving arg->expr null. Only require arg->expr for non-syscap arguments.
    if (!arg->expr && arg->name.Val() != SYSCAP_IDENTIFIER) {
        return false;
    }
    if (parseNameParam.count(arg->name.Val()) <= 0) {
        return false;
    }
    return true;
}

/// Dispatch the IfAvailable argument to build ifscopeAnnoInfo.
/// Handles the level, syscap, and generic-named-param cases.
/// Returns false when scope construction fails and processing should stop.
bool PluginCustomAnnoChecker::BuildIfAvailableScope(FuncArg& arg, const IfExpr* ifExpr,
    PluginCustomAnnoInfo& ifscopeAnnoInfo)
{
    if (arg.name.Val() == LEVEL_IDENTIFIER) {
        return ParseIfAvailableLevelArg(arg, ifscopeAnnoInfo);
    }
    if (arg.name.Val() == SYSCAP_IDENTIFIER) {
        // arg->expr was moved into the desugared IfExpr's canIUse(...) condition during desugar.
        // Reconstruct the syscap scope from the desugared IfExpr condition instead.
        if (!ifExpr || !TryBuildIfAvailableScopeFromIfExpr(*ifExpr, ifscopeAnnoInfo)) {
            return false;
        }
        return true;
    }
    parseNameParam[arg.name.Val()](*arg.expr, ifscopeAnnoInfo, diag);
    return true;
}

/// Resolve the then/else Block bodies from the desugared IfExpr (if present) or the raw
/// lambda bodies, then walk each with its corresponding branch checker.
void PluginCustomAnnoChecker::WalkIfAvailableBranches(const IfExpr* ifExpr,
    const LambdaExpr& lambda1, const LambdaExpr& lambda2,
    PluginCustomAnnoInfo& ifscopeAnnoInfo, PluginCustomAnnoInfo& scopeAnnoInfo)
{
    auto checkerIf = MakeIfBranchChecker(ifscopeAnnoInfo, scopeAnnoInfo);
    auto checkerElse = MakeElseBranchChecker(scopeAnnoInfo);
    auto thenBody = ifExpr && ifExpr->thenBody ? ifExpr->thenBody.get() : lambda1.funcBody->body.get();
    WalkBranchBody(thenBody, checkerIf);
    Ptr<Block> elseBody = lambda2.funcBody->body.get();
    if (ifExpr && ifExpr->elseBody) {
        elseBody = DynamicCast<Block>(ifExpr->elseBody.get());
    }
    WalkBranchBody(elseBody, checkerElse);
}

void PluginCustomAnnoChecker::CheckIfAvailableExpr(IfAvailableExpr& iae, PluginCustomAnnoInfo& scopeAnnoInfo)
{
    auto ifExpr = DynamicCast<IfExpr>(iae.desugarExpr.get());
    auto arg = iae.GetArg();
    auto lambda1 = iae.GetLambda1();
    auto lambda2 = iae.GetLambda2();
    if (!ValidateIfAvailableArgs(arg, lambda1, lambda2)) {
        return;
    }
    auto ifscopeAnnoInfo = PluginCustomAnnoInfo();
    if (!BuildIfAvailableScope(*arg, ifExpr, ifscopeAnnoInfo)) {
        return;
    }
    if (!ifscopeAnnoInfo.since.IsZero() && ifscopeAnnoInfo.since < APILevelVersion(IFAVAILABLE_LOWER_LIMITLEVEL)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_level_limit, *arg);
        return;
    }
    WalkIfAvailableBranches(ifExpr, *lambda1, *lambda2, ifscopeAnnoInfo, scopeAnnoInfo);
}

void PluginCustomAnnoChecker::CheckAnnoBeforeMacro(Package& pkg)
{
    const std::vector<std::string> annoName = {std::string(APILEVEL_ANNO_NAME), std::string(HIDE_ANNO_NAME)};
    auto checker = [this, &annoName](Ptr<Node> node) -> VisitAction {
        if (node->astKind != ASTKind::MACRO_EXPAND_DECL) {
            return VisitAction::WALK_CHILDREN;
        }
        auto med = StaticCast<MacroExpandDecl>(node);
        if (!Utils::In(med->identifier.Val(), annoName)) {
            return VisitAction::WALK_CHILDREN;
        }
        auto subDecl = med->invocation.decl.get();
        if (subDecl->astKind != ASTKind::MACRO_EXPAND_DECL) {
            return VisitAction::WALK_CHILDREN;
        }
        auto subMed = StaticCast<MacroExpandDecl>(subDecl);
        if (!Utils::In(subMed->identifier.Val(), annoName)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_hide_must_at_end, med->begin, med->identifier);
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    for (auto& file : pkg.files) {
        for (auto& node : file->originalMacroCallNodes) {
            Walker(node, checker).Walk();
        }
    }
}

void PluginCustomAnnoChecker::Check(Package& pkg)
{
    ctx = ci.GetASTContextByPackage(&pkg);
    curModuleName = GetModuleName(pkg.fullPackageName);
    CheckAnnoBeforeMacro(pkg);
    std::vector<Ptr<Decl>> scopeDecl;
    auto checker = [this, &scopeDecl](Ptr<Node> node) -> VisitAction {
        if (auto decl = DynamicCast<Decl>(node)) {
            if (decl->astKind == ASTKind::PRIMARY_CTOR_DECL) {
                return VisitAction::SKIP_CHILDREN;
            }
            scopeDecl.emplace_back(decl);
            return VisitAction::WALK_CHILDREN;
        }
        PluginCustomAnnoInfo scopeAnnoInfo;
        for (auto it = scopeDecl.rbegin(); it != scopeDecl.rend(); ++it) {
            Parse(**it, scopeAnnoInfo);
        }
        if (auto iae = DynamicCast<IfAvailableExpr>(node)) {
            scopeAnnoInfo.since = scopeAnnoInfo.since.IsZero() ? globalLevel : scopeAnnoInfo.since;
            CheckIfAvailableExpr(*iae, scopeAnnoInfo);
            return VisitAction::SKIP_CHILDREN;
        }
        // User-written 'if (DeviceInfo.sdkApiVersion >= N)' or 'if (canIUse(...))' is
        // ordinary runtime control flow, not a Sema-level API gate. The only way to
        // opt a scope into gate semantics (scope-tightened API checks + EXTERNAL_WEAK
        // linkage) is the explicit @IfAvailable expression, which is handled above.
        if (!CheckNode(node, scopeAnnoInfo)) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    auto popScope = [&scopeDecl](Ptr<Node> node) -> VisitAction {
        if (!scopeDecl.empty() && scopeDecl.back() == node) {
            scopeDecl.pop_back();
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&pkg, checker, popScope).Walk();
    // Clear the annotation information of the dependency package to avoid chir failure.
    // In the LSP scenario, annotation information still needs to be saved after SEMA.
    if (!ci.invocation.globalOptions.enableMacroInLSP) {
        ClearAnnoInfoOfDepPkg(importManager);
    }
}
