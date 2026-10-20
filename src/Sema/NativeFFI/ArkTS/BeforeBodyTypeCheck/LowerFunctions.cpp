// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include <algorithm>
#include <iterator>

#include "../FunctionSource.h"
#include "../Utils/ASTFactory.h"
#include "Collector.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI::ArkTS;

namespace {
// Before: generated RefExpr/RefType("__cj_arkts_current"/"JSContext"/...) has
// no declaration target; a same-named business declaration may be in scope.
// After: target, candidate targets and Ty identify the expected declaration in
// this file's visible import closure. Already-bound source references stay intact.
// Missing/invalid support reports an error and returns false; no import is added.
// Resolve support names only from this file's normal visible imports. Binding
// both identity and type prevents a same-named source declaration from replacing
// the generated call. Missing dependencies remain compilation errors.
bool BindSupportReferences(Decl& owner, Node& root, ImportManager& imports, DiagnosticEngine& diag)
{
    static const std::unordered_map<std::string, std::string> packages{
        {"__cj_arkts_enter", "cj_arkts_context_v1"}, {"__cj_arkts_current", "cj_arkts_context_v1"},
        {"__cj_arkts_restore", "cj_arkts_context_v1"}, {"BigInt", "std.math.numeric"},
        {"JSContext", "ohos.ark_interop"}, {"JSModule", "ohos.ark_interop"}, {"Exception", "std.core"}};
    bool valid = true;
    Walker(&root, [&](Ptr<Node> node) {
        auto expr = DynamicCast<RefExpr*>(node);
        auto type = DynamicCast<RefType*>(node);
        auto ref = expr ? &expr->ref : type ? &type->ref : nullptr;
        if (!ref || ref->target) {
            return VisitAction::WALK_CHILDREN;
        }
        auto found = packages.find(ref->identifier.Val());
        if (found == packages.end()) {
            return VisitAction::WALK_CHILDREN;
        }
        const auto visible = imports.GetImportedDeclsByName(*owner.curFile, found->first);
        auto target = std::find_if(visible.begin(), visible.end(), [&](auto decl) {
            return decl->fullPackageName == found->second && decl->identifier == found->first;
        });
        if (target == visible.end() || !Ty::IsTyCorrect((*target)->GetTy())) {
            if (valid) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_wrapper_unavailable,
                    owner, "missing imported support declaration '" + found->second + "." + found->first + "'");
            }
            valid = false;
        } else {
            ref->target = *target;
            ref->targets = {*target};
            node->SetTy((*target)->GetTy());
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return valid;
}
} // namespace

// Before AST (after preparation): FuncDecl(echo, ARKTS_FUNCTION_WRAPPER,
// params=[x: Int32], ret=Int32, FuncBody.body=nullptr), with @ArkTS options.
// After AST: the same declaration/signature owns a Block equivalent to:
//   let ctx: JSContext = __cj_arkts_current()
//   let receiver = ctx.requireArkModule("<effective module>").asObject()
//   let arg0 = ctx.number(Float64(x)).toJSValue()
//   let result = receiver.callMethod("<effective foreign name>", [arg0])
//   return Int32(result.toNumber())
// Namespace traversal and other scalar conversions follow FunctionSource.cpp.
// Literal placeholders become AST string values; argument references target the
// original FuncParam; ReturnExpr.refFuncBody targets the original FuncBody.
// Support references are bound only to visible imports and generated locals are
// collected in the parameter scope. No SDK import or source signature is added.
// Imported packages and already-populated bodies are not lowered again.
// Lowering/AST rebinding pseudocode:
//   skip imported packages and declarations not marked for ArkTS lowering
//   resolve declared aliases to semantic scalar types for body generation
//   reject unsupported signatures/options before a bodyless wrapper reaches later phases
//   read literal annotation options; explicit module wins over the declaration package name
//   split namespace into ordered literal placeholders; choose parameter-disjoint locals
//   parse the foreign-body skeleton (see FunctionSource.cpp pseudocode)
//   move its body to the original FuncBody and replace all temporary File refs
//   bind module/name/namespace LitConstExpr values, and typed parameter RefExpr targets
//   bind support references to the explicit import closure, preserving source-name hygiene
//   let normal checking consume the explicit arkts.lang import and
//   its public re-exports through the ordinary package dependency closure
//   rebind ReturnExpr to the original FuncBody, collect generated local symbols
//   in the parameter scope, then restore the collector's previous scope state
void TypeChecker::TypeCheckerImpl::LowerArkTSFunctions(ASTContext& ctx)
{
    auto& pkg = *ctx.curPackage;
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            if (decl->TestAttr(Attribute::COMPILER_ADD) &&
                decl->identifier.Val().rfind("__cj_arkts_", 0) == 0) {
                BindSupportReferences(*decl, *decl, *ci->importManager, diag);
            }
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (!fd || !fd->TestAttr(Attribute::ARKTS_FUNCTION_WRAPPER) || !fd->funcBody || fd->funcBody->body) {
                continue;
            }
            auto& fb = *fd->funcBody;
            auto scalar = [&](Ptr<Type> type) {
                if (!type) {
                    return std::string{};
                }
                SetTypeTy(ctx, *type);
                return ArkTSScalarName(type->GetTy());
            };
            auto resultType = scalar(fb.retType.get());
            std::vector<std::string> types;
            std::vector<Ptr<FuncParam>> params;
            bool supported = !resultType.empty();
            for (auto& list : fb.paramLists) {
                for (auto& param : list->params) {
                    auto type = scalar(param->type.get());
                    supported = supported && !type.empty() && type != "Nothing";
                    types.emplace_back(type);
                    params.emplace_back(param.get());
                }
            }
            std::unordered_map<std::string, std::string> options;
            if (!supported) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_wrapper_unavailable,
                    *fd, "unsupported parameter or return type");
                continue;
            }
            if (!ArkTSFunctionOptions(*fd, options)) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_wrapper_unavailable,
                    *fd, "invalid annotation options");
                continue;
            }
            const auto module =
                options.count("module") ? options.at("module") : ArkTSDefaultModule(pkg.fullPackageName);
            if (module.empty()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_wrapper_unavailable,
                    *fd, "empty module name");
                continue;
            }
            std::unordered_map<std::string, std::string> literals{{"__module__", module},
                {"__name__", options.count("foreignName") ? options.at("foreignName") : fd->identifier.Val()}};
            auto ns = options.count("namespace") ? options.at("namespace") : "";
            size_t depth = 0;
            size_t start = 0;
            while (start < ns.size()) {
                auto end = ns.find('.', start);
                literals["__namespace_" + std::to_string(depth++) + "__"] =
                    ns.substr(start, end == std::string::npos ? end : end - start);
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            auto code = ArkTSForeignWrapperSource(types, resultType, depth, ArkTSForeignLocalPrefix(params));
            Native::FFI::ArkTS::ASTFactory factory(diag, ci->GetSourceManager());
            const auto errorsBefore = diag.GetErrorCount();
            fb.body = factory.CreateForeignBody(*fd, code);
            if (!fb.body) {
                if (diag.GetErrorCount() == errorsBefore) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_wrapper_unavailable,
                        *fd, "failed to construct wrapper body");
                }
                continue;
            }
            Walker(fb.body.get(), [&](Ptr<Node> node) {
                node->curFile = file.get();
                node->begin = fd->begin;
                node->end = fd->end;
                if (auto literal = DynamicCast<LitConstExpr*>(node); literal && literals.count(literal->stringValue)) {
                    literal->stringValue = literals.at(literal->stringValue);
                }
                if (auto ref = DynamicCast<RefExpr*>(node)) {
                    bool argument = false;
                    for (size_t i = 0; i < params.size(); ++i) {
                        if (ref->ref.identifier == "__argument_" + std::to_string(i)) {
                            ref->ref.identifier = params[i]->identifier;
                            ref->ref.target = params[i];
                            ref->SetTy(params[i]->type->GetTy());
                            ref->begin = fd->end;
                            argument = true;
                            break;
                        }
                    }
                    if (argument) {
                        return VisitAction::WALK_CHILDREN;
                    }
                }
                if (auto returned = DynamicCast<ReturnExpr*>(node)) {
                    returned->refFuncBody = &fb;
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
            if (!BindSupportReferences(*fd, *fb.body, *ci->importManager, diag)) {
                fb.body.reset();
                fd->EnableAttr(Attribute::IS_BROKEN);
                fb.EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            // Match Collector::CollectFuncBody: locals share parameter scope.
            auto oldScope = ctx.currentScopeName;
            auto oldLevel = ctx.currentScopeLevel;
            ctx.currentScopeName = params.empty() ? fb.scopeName : params.front()->scopeName;
            if (params.empty()) {
                auto gate = ctx.currentScopeName.find_last_of(ScopeManagerApi::childScopeNameSplit);
                if (gate != std::string::npos) {
                    ctx.currentScopeName[gate] = ScopeManagerApi::scopeNameSplit;
                }
            }
            ctx.currentScopeLevel = fb.scopeLevel + 1;
            Collector collector(scopeManager);
            auto previous = ctx.symbolTable.empty() ? ctx.symbolTable.end() : std::prev(ctx.symbolTable.end());
            for (auto& statement : fb.body->body) {
                collector.BuildSymbolTable(ctx, statement.get(), ci->buildTrie);
            }
            auto first = previous == ctx.symbolTable.end() ? ctx.symbolTable.begin() : std::next(previous);
            for (auto it = first; it != ctx.symbolTable.end(); ++it) {
                auto& symbol = *it;
                if (auto local = DynamicCast<Decl*>(symbol->node)) {
                    ctx.AddDeclName(
                        {symbol->name, ScopeManagerApi::GetScopeNameWithoutTail(symbol->scopeName)}, *local);
                }
            }
            ctx.currentScopeName = oldScope;
            ctx.currentScopeLevel = oldLevel;
        }
    }
}
