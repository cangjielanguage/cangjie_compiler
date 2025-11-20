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

#include "CheckAPILevel.h"

#include <functional>
#include <iostream>
#include <stack>
#include <unordered_map>

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/SafePointer.h"
#include "cangjie/Utils/StdUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace APILevelCheck;

namespace {
constexpr std::string_view PKG_NAME_WHERE_APILEVEL_AT = "ohos.labels";
constexpr std::string_view APILEVEL_ANNO_NAME = "APILevel";
constexpr std::string_view SINCE_IDENTGIFIER = "since";
constexpr std::string_view LEVEL_IDENTGIFIER = "level";
constexpr std::string_view SYSCAP_IDENTGIFIER = "syscap";
constexpr std::string_view CFG_PARAM_LEVEL_NAME = "APILevel_level";
constexpr std::string_view CFG_PARAM_SYSCAP_NAME = "APILevel_syscap";
// For level check:
const LevelType IFAVAIALBEL_LOWER_LIMITLEVEL = 19;

LevelType Str2LevelType(std::string s)
{
    return static_cast<LevelType>(Stoull(s).value_or(0));
}

void ParseLevel(const Expr& e, APILevelAnnoInfo& apilevel, DiagnosticEngine& diag)
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
    auto newLevel = Str2LevelType(lce->stringValue);
    apilevel.since = apilevel.since == 0 ? newLevel : std::min(newLevel, apilevel.since);
}

void ParseSince(const Expr& e, APILevelAnnoInfo& apilevel, DiagnosticEngine& diag)
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
    auto newLevel = Str2LevelType(lce->stringValue);
    apilevel.since = apilevel.since == 0 ? newLevel : std::min(newLevel, apilevel.since);
}

void ParseSysCap(const Expr& e, APILevelAnnoInfo& apilevel, DiagnosticEngine& diag)
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

using ParseNameParamFunc = std::function<void(const Expr&, APILevelAnnoInfo&, DiagnosticEngine&)>;
std::unordered_map<std::string_view, ParseNameParamFunc> parseNameParam = {
    {SINCE_IDENTGIFIER, ParseSince},
    {LEVEL_IDENTGIFIER, ParseLevel},
    {SYSCAP_IDENTGIFIER, ParseSysCap},
};

struct JsonObject;

struct JsonPair {
    std::string key;
    std::vector<std::string> valueStr;
    std::vector<OwnedPtr<JsonObject>> valueObj;
    std::vector<uint64_t> valueNum;
};

struct JsonObject {
    std::vector<OwnedPtr<JsonPair>> pairs;
};

enum class StringMod {
    KEY,
    VALUE,
};

std::string ParseJsonString(size_t& pos, const std::vector<uint8_t>& in)
{
    std::stringstream str;
    if (in[pos] == '"') {
        ++pos;
        while (pos < in.size() && in[pos] != '"') {
            str << in[pos];
            ++pos;
        }
    }

    return str.str();
}

uint64_t ParseJsonNumber(size_t& pos, const std::vector<uint8_t>& in)
{
    if (in[pos] < '0' || in[pos] > '9') {
        return 0;
    }
    std::stringstream num;
    while (pos < in.size() && in[pos] >= '0' && in[pos] <= '9') {
        num << in[pos];
        ++pos;
    }
    if (num.str().size()) {
        --pos;
    }
    return Stoull(num.str()).value_or(0);
}

OwnedPtr<JsonObject> ParseJsonObject(size_t& pos, const std::vector<uint8_t>& in);
void ParseJsonArray(size_t& pos, const std::vector<uint8_t>& in, Ptr<JsonPair> value)
{
    if (in[pos] != '[') {
        return;
    }
    ++pos;
    while (pos < in.size()) {
        if (in[pos] == ' ' || in[pos] == '\n') {
            ++pos;
            continue;
        }
        if (in[pos] == '"') {
            value->valueStr.emplace_back(ParseJsonString(pos, in));
        }
        if (in[pos] == '{') {
            value->valueObj.emplace_back(ParseJsonObject(pos, in));
        }
        if (in[pos] == ']') {
            return;
        }
        ++pos;
    }
}

OwnedPtr<JsonObject> ParseJsonObject(size_t& pos, const std::vector<uint8_t>& in)
{
    if (in[pos] != '{') {
        return nullptr;
    }
    ++pos;
    auto ret = MakeOwned<JsonObject>();
    auto mod = StringMod::KEY;
    while (pos < in.size()) {
        if (in[pos] == ' ' || in[pos] == '\n') {
            ++pos;
            continue;
        }
        if (in[pos] == '}') {
            return ret;
        }
        if (in[pos] == ':') {
            mod = StringMod::VALUE;
        }
        if (in[pos] == ',') {
            mod = StringMod::KEY;
        }
        if (in[pos] == '"') {
            if (mod == StringMod::KEY) {
                auto newData = MakeOwned<JsonPair>();
                newData->key = ParseJsonString(pos, in);
                ret->pairs.emplace_back(std::move(newData));
            } else {
                ret->pairs.back()->valueStr.emplace_back(ParseJsonString(pos, in));
            }
        }
        if (in[pos] >= '0' && in[pos] <= '9') {
            ret->pairs.back()->valueNum.emplace_back(ParseJsonNumber(pos, in));
        }
        if (in[pos] == '{') {
            // The pos will be updated to the pos of matched '}'.
            ret->pairs.back()->valueObj.emplace_back(ParseJsonObject(pos, in));
        }
        if (in[pos] == '[') {
            // The pos will be updated to the pos of matched ']'.
            ParseJsonArray(pos, in, ret->pairs.back().get());
        }
        ++pos;
    }
    return ret;
}

std::vector<std::string> GetJsonString(Ptr<JsonObject> root, const std::string& key)
{
    for (auto& v : root->pairs) {
        if (v->key == key) {
            return v->valueStr;
        }
        for (auto& o : v->valueObj) {
            auto ret = GetJsonString(o.get(), key);
            if (!ret.empty()) {
                return ret;
            }
        }
    }
    return {};
}

Ptr<JsonObject> GetJsonObject(Ptr<JsonObject> root, const std::string& key, const size_t index)
{
    for (auto& v : root->pairs) {
        if (v->key == key && v->valueObj.size() > index) {
            return v->valueObj[index].get();
        }
        for (auto& o : v->valueObj) {
            auto ret = GetJsonObject(o.get(), key, index);
            if (ret) {
                return ret;
            }
        }
    }
    return nullptr;
}

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
    target->linkage = Linkage::EXTERNAL_WEAK;
    if (auto fd = DynamicCast<FuncDecl>(target)) {
        for (auto& param : fd->funcBody->paramLists[0]->params) {
            if (param->desugarDecl) {
                param->desugarDecl->linkage = Linkage::EXTERNAL_WEAK;
            }
        }
        if (fd->propDecl) {
            fd->propDecl->linkage = Linkage::EXTERNAL_WEAK;
        }
    } else if (auto md = DynamicCast<MacroDecl>(target)) {
        if (md->desugarDecl) {
            md->desugarDecl->linkage = Linkage::EXTERNAL_WEAK;
        }
    } else if (auto pd = DynamicCast<PropDecl>(target)) {
        for (auto& getter : pd->getters) {
            if (!getter) {
                continue;
            }
            getter->linkage = Linkage::EXTERNAL_WEAK;
        }
        for (auto& setter : pd->setters) {
            if (!setter) {
                continue;
            }
            setter->linkage = Linkage::EXTERNAL_WEAK;
        }
    }
    if (target->outerDecl && target->outerDecl->IsNominalDecl()) {
        target->outerDecl->linkage = Linkage::EXTERNAL_WEAK;
        MarkTargetAsExternalWeak(target->outerDecl);
    }
}

} // namespace

void APILevelAnnoChecker::ParseJsonFile(const std::vector<uint8_t>& in) noexcept
{
    size_t startPos = static_cast<size_t>(std::find(in.begin(), in.end(), '{') - in.begin());
    auto root = ParseJsonObject(startPos, in);
    auto deviceSysCapObj = GetJsonObject(root, "deviceSysCap", 0);
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
                return;
            }
            startPos = static_cast<size_t>(std::find(buffer.begin(), buffer.end(), '{') - buffer.begin());
            auto rootOneDevice = ParseJsonObject(startPos, buffer);
            auto curSyscaps = GetJsonString(rootOneDevice, "SysCaps");
            for (auto syscap : curSyscaps) {
                if (Utils::NotIn(syscap, syscapsOneDev)) {
                    syscapsOneDev.emplace_back(syscap);
                }
            }
        }
        dev2SyscapsMap.emplace(subObj->key, syscapsOneDev);
    }
    std::optional<SysCapSet> lastSyscap = std::nullopt;
    for (auto& dev2Syscaps : dev2SyscapsMap) {
        SysCapSet& curSyscaps = dev2Syscaps.second;
        std::sort(curSyscaps.begin(), curSyscaps.end());
        SysCapSet intersection;
        if (lastSyscap.has_value()) {
            std::set_intersection(lastSyscap.value().begin(), lastSyscap.value().end(), curSyscaps.begin(),
                curSyscaps.end(), std::back_inserter(intersection));
        } else {
            intersection = curSyscaps;
        }
        lastSyscap = intersection;
        for (auto syscap : curSyscaps) {
            if (Utils::NotIn(syscap, unionSet)) {
                unionSet.emplace_back(syscap);
            }
        }
    }
    intersectionSet = lastSyscap.value();
}

void APILevelAnnoChecker::ParseOption() noexcept
{
    auto& option = ci.invocation.globalOptions;
    auto found = option.passedWhenKeyValue.find(std::string(CFG_PARAM_LEVEL_NAME));
    if (found != option.passedWhenKeyValue.end()) {
        globalLevel = Str2LevelType(found->second);
        optionWithLevel = true;
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
        ParseJsonFile(jsonContent);
        optionWithSyscap = true;
    }
}

bool APILevelAnnoChecker::IsAnnoAPILevel(Ptr<Annotation> anno, [[maybe_unused]] const Decl& decl)
{
    if (ctx && ctx->curPackage && ctx->curPackage->fullPackageName == PKG_NAME_WHERE_APILEVEL_AT) {
        return anno->identifier == APILEVEL_ANNO_NAME;
    }
    if (!anno || anno->identifier != APILEVEL_ANNO_NAME) {
        return false;
    }
    auto target = anno->baseExpr ? anno->baseExpr->GetTarget() : nullptr;
    if (target && target->curFile && target->curFile->curPackage &&
        target->curFile->curPackage->fullPackageName != PKG_NAME_WHERE_APILEVEL_AT) {
        return false;
    }
    return true;
}

APILevelAnnoInfo APILevelAnnoChecker::Parse(const Decl& decl)
{
    if (decl.annotations.empty()) {
        return APILevelAnnoInfo();
    }
    if (auto found = levelCache.find(&decl); found != levelCache.end()) {
        return found->second;
    }
    APILevelAnnoInfo ret;
    for (auto& anno : decl.annotations) {
        if (!anno || !IsAnnoAPILevel(anno.get(), decl)) {
            continue;
        }
        for (size_t i = 0; i < anno->args.size(); ++i) {
            std::string argName = anno->args[i]->name.Val();
            if (parseNameParam.count(argName) <= 0) {
                continue;
            }
            std::string preSyscap = ret.syscap;
            parseNameParam[argName](*anno->args[i]->expr.get(), ret, diag);
            if (!preSyscap.empty() && preSyscap != ret.syscap) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_apilevel_multi_diff_syscap, decl);
            }
        }
        // In the APILevel definition, only "since" does not provide a default value. Here, the alert indicates that
        // there is an issue with the APILevel annotation, which may originnate from the cj.d file.
        if (ret.since == 0) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_apilevel_missing_arg, anno->begin, "since!: String");
        }
    }
    levelCache[&decl] = ret;
    return ret;
}

bool APILevelAnnoChecker::CheckLevel(
    const Node& node, const Decl& target, const APILevelAnnoInfo& scopeAPILevel, bool reportDiag)
{
    if (!optionWithLevel) {
        return true;
    }
    LevelType scopeLevel = scopeAPILevel.since != 0 ? scopeAPILevel.since : globalLevel;
    auto targetAPILevel = Parse(target);
    if (targetAPILevel.since > scopeLevel && !node.begin.IsZero()) {
        if (reportDiag) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_apilevel_ref_higher, node, target.identifier.Val(),
                std::to_string(targetAPILevel.since), std::to_string(scopeLevel));
        }
        return false;
    }
    return true;
}

bool APILevelAnnoChecker::CheckSyscap(
    const Node& node, const Decl& target, const APILevelAnnoInfo& scopeAPILevel, bool reportDiag)
{
    if (!optionWithSyscap) {
        return true;
    }
    SysCapSet scopeSyscaps = unionSet;
    if (!scopeAPILevel.syscap.empty()) {
        scopeSyscaps.emplace_back(scopeAPILevel.syscap);
    }
    auto targetAPILevel = Parse(target);
    std::string targetLevel = targetAPILevel.syscap;
    if (targetLevel.empty()) {
        return true;
    }
    auto diagForSyscap = [this, &scopeSyscaps, &node, &targetLevel](DiagKindRefactor kind) {
        auto builder = diag.DiagnoseRefactor(kind, node, targetLevel);
        std::stringstream scopeSyscapsStr;
        // 3 is maximum number of syscap limit.
        for (size_t i = 0; i < std::min(scopeSyscaps.size(), static_cast<size_t>(3)); ++i) {
            std::string split = scopeSyscaps[i] == scopeSyscaps.back() ? "" : ", ";
            scopeSyscapsStr << scopeSyscaps[i] << split;
        }
        if (scopeSyscaps.size() > 3) {
            scopeSyscapsStr << "...";
        }
        builder.AddNote("the following syscaps are supported: " + scopeSyscapsStr.str());
    };

    auto found = std::find(scopeSyscaps.begin(), scopeSyscaps.end(), targetLevel);
    if (found == scopeSyscaps.end() && !node.begin.IsZero()) {
        if (reportDiag) {
            diagForSyscap(DiagKindRefactor::sema_apilevel_syscap_error);
        }
        return false;
    }

    scopeSyscaps = intersectionSet;
    if (!scopeAPILevel.syscap.empty()) {
        scopeSyscaps.emplace_back(scopeAPILevel.syscap);
    }
    found = std::find(scopeSyscaps.begin(), scopeSyscaps.end(), targetLevel);
    if (found == scopeSyscaps.end() && !node.begin.IsZero()) {
        if (reportDiag) {
            diagForSyscap(DiagKindRefactor::sema_apilevel_syscap_warning);
        }
        return false;
    }
    return true;
}

bool APILevelAnnoChecker::CheckNode(Ptr<Node> node, APILevelAnnoInfo& scopeAPILevel, bool reportDiag)
{
    if (!node) {
        return true;
    }
    auto target = node->GetTarget();
    if (auto ce = DynamicCast<CallExpr>(node); ce && ce->resolvedFunction) {
        target = ce->resolvedFunction;
    }
    if (!target) {
        return true;
    }
    bool ret = true;
    if (target->TestAttr(Attribute::CONSTRUCTOR) && target->outerDecl) {
        ret = CheckLevel(*node, *target->outerDecl, scopeAPILevel, reportDiag) && ret;
        ret = CheckSyscap(*node, *target->outerDecl, scopeAPILevel, reportDiag) && ret;
        if (!ret) {
            return false;
        }
    }
    ret = CheckLevel(*node, *target, scopeAPILevel, reportDiag) && ret;
    ret = CheckSyscap(*node, *target, scopeAPILevel, reportDiag) && ret;
    return ret;
}

void APILevelAnnoChecker::CheckIfAvailableExpr(IfAvailableExpr& iae, APILevelAnnoInfo& scopeAPILevel)
{
    if (!iae.desugarExpr || iae.desugarExpr->astKind != ASTKind::IF_EXPR) {
        return;
    }
    auto ifExpr = StaticCast<IfExpr>(iae.desugarExpr.get());
    Ptr<FuncArg> arg = iae.GetArg();
    if (parseNameParam.count(arg->name.Val()) <= 0) {
        return;
    }
    auto ifScopeAPILevel = APILevelAnnoInfo();
    parseNameParam[arg->name.Val()](*ifExpr->condExpr, ifScopeAPILevel, diag);
    if (ifScopeAPILevel.since != 0 && ifScopeAPILevel.since < IFAVAIALBEL_LOWER_LIMITLEVEL) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_ifavailable_level_limit, *arg);
        return;
    }
    // if branch.
    auto checkerIf = [this, &ifScopeAPILevel, &scopeAPILevel](Ptr<Node> node) -> VisitAction {
        if (auto e = DynamicCast<IfAvailableExpr>(node)) {
            CheckIfAvailableExpr(*e, ifScopeAPILevel);
            return VisitAction::SKIP_CHILDREN;
        }
        // If the reference meets the 'IfAvaliable' condition but does not meet the global APILevel configuration, set
        // linkage to 'EXTERNAL_WEAK'.
        auto ret = CheckNode(node, ifScopeAPILevel);
        if (ret && !CheckNode(node, scopeAPILevel, false)) {
            MarkTargetAsExternalWeak(node);
        }
        if (!ret) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(ifExpr->thenBody.get(), checkerIf).Walk();
    // else branch.
    auto checkerElse = [this, &scopeAPILevel](Ptr<Node> node) -> VisitAction {
        if (auto e = DynamicCast<IfAvailableExpr>(node)) {
            CheckIfAvailableExpr(*e, scopeAPILevel);
            return VisitAction::SKIP_CHILDREN;
        }
        if (!CheckNode(node, scopeAPILevel)) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(ifExpr->elseBody.get(), checkerElse).Walk();
}

void APILevelAnnoChecker::Check(Package& pkg)
{
    ctx = ci.GetASTContextByPackage(&pkg);
    std::vector<Ptr<Decl>> scopeDecl;
    auto checker = [this, &scopeDecl](Ptr<Node> node) -> VisitAction {
        if (auto decl = DynamicCast<Decl>(node)) {
            scopeDecl.emplace_back(decl);
            return VisitAction::WALK_CHILDREN;
        }
        auto scopeAPILevel = APILevelAnnoInfo();
        for (auto it = scopeDecl.rbegin(); it != scopeDecl.rend(); ++it) {
            scopeAPILevel = Parse(**it);
            if (scopeAPILevel.since != 0) {
                break;
            }
        }
        if (auto iae = DynamicCast<IfAvailableExpr>(node)) {
            scopeAPILevel.since = scopeAPILevel.since == 0 ? globalLevel : scopeAPILevel.since;
            CheckIfAvailableExpr(*iae, scopeAPILevel);
            return VisitAction::SKIP_CHILDREN;
        }
        if (!CheckNode(node, scopeAPILevel)) {
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
