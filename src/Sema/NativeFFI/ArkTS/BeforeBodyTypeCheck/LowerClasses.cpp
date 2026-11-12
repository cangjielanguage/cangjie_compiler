// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include <unordered_map>
#include <vector>

#include "../../Utils.h"
#include "../FunctionSource.h"
#include "../Utils/ASTFactory.h"
#include "Collector.h"
#include "PlaceholderBinding.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;
using namespace Cangjie::Native::FFI::ArkTS;

namespace {
// The generated wrapping constructor, parsed from the compiler-owned holder
// shape and marked COMPILER_ADD. Its first parameter is the JSObject to store:
//   public init(__cj_arkts_ref: JSObject, _: Unit)
//       { this.__cj_arkts_ref = __cj_arkts_ref }
bool IsWrappingConstructor(const FuncDecl& fd)
{
    return fd.TestAttr(Attribute::COMPILER_ADD) && fd.funcBody && fd.funcBody->paramLists.size() == 1 &&
        !fd.funcBody->paramLists.front()->params.empty() &&
        fd.funcBody->paramLists.front()->params.front()->identifier.Val() == ARKTS_CLASS_CTOR_PARAM;
}

bool IsReferenceAccessor(const FuncDecl& fd)
{
    return fd.identifier.Val() == ARKTS_CLASS_REF_ACCESSOR;
}

// ArkTS member name: @ForeignName wins, the Cangjie identifier is the default.
std::string GetArkTSMemberName(const Decl& decl)
{
    auto name = GetSingleArgumentAnnotationValue(decl, AnnotationKind::FOREIGN_NAME);
    return name ? *name : decl.identifier.Val();
}

// One class-wide lowering context shared by every member.
struct ClassContext {
    ClassDecl& owner;
    ArkTSReceiverKind receiver;
    std::string module;
    std::string className;
    std::vector<std::string> namespaces;
};
} // namespace

// One signature position as an ArkTS type description: the written type is
// resolved through the checker's alias substitution before classification,
// so a type alias position lowers by its target and never as an opaque
// unsupported name. A null type node classifies as the empty (unsupported)
// description. Shared with LowerArkTSFunctions, so a position can never mean
// two things across the two lowering paths.
ArkTSTypeDesc TypeChecker::TypeCheckerImpl::ArkTSClassifyPosition(ASTContext& ctx, Ptr<Type> type)
{
    if (!type) {
        return ArkTSTypeDesc{};
    }
    SetTypeTy(ctx, *type);
    return ArkTSClassifyType(type->GetTy());
}

// Lower every prepared @ArkTS foreign class: generate the wrapping
// constructor's factory and the member function/property bodies. Refusal marks
// the declaration broken and produces no body, so no member is left bodyless
// without a diagnostic.
void TypeChecker::TypeCheckerImpl::LowerArkTSClasses(ASTContext& ctx)
{
    auto& pkg = *ctx.curPackage;
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    // Validate every @ArkTS-carrying declaration before any class/member body
    // is generated: the named-default rule marks an offending bodyless member
    // IS_BROKEN, and the member loops below must observe that mark so a refused
    // member never gets a wrapper body. Running this after class lowering would
    // generate the body first and suppress the diagnostic. LowerArkTSFunctions
    // no longer repeats this call.
    ArkTSDeclOptionsDiagnose(pkg, diag);
    ASTFactory factory(diag, ci->GetSourceManager());

    // Install one generated member body and rebind its placeholders. `member`
    // owns the body; `params` are the references the argument placeholders
    // bind to.
    auto installBody = [&](ClassContext& classCtx, FuncDecl& member, File& file, const std::string& code,
                           const std::vector<Ptr<FuncParam>>& params, const std::string& memberName,
                           const std::unordered_map<std::string, Ptr<Decl>>& classRefs) {
        auto& fb = *member.funcBody;
        // A repeated pass (LSP/incremental AST reuse) may see the body this
        // lowering already installed. Preparation rejects user-written bodies,
        // so an existing body is ours; keep it instead of overwriting or
        // failing. LowerArkTSFunctions skips already-bodied wrappers the same
        // way.
        if (fb.body) {
            return;
        }
        fb.body = factory.CreateForeignBody(member, code);
        if (!fb.body) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, member,
                classCtx.owner.identifier.Val(), "cannot generate the wrapper body");
            classCtx.owner.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
        std::unordered_map<std::string, std::string> literals{
            {ARKTS_PLACEHOLDER_MODULE, classCtx.module}, {ARKTS_PLACEHOLDER_CLASS, classCtx.className},
            {ARKTS_PLACEHOLDER_MEMBER, memberName}};
        for (size_t i = 0; i < classCtx.namespaces.size(); ++i) {
            literals[std::string(ARKTS_PLACEHOLDER_NAMESPACE) + std::to_string(i) + "__"] = classCtx.namespaces[i];
        }
        // Type placeholders are merged into the long-lived support map only
        // after the body passes BindArkTSSupportReferences.
        std::unordered_map<Ptr<const Node>, Ptr<Decl>> pendingRefs;
        Walker(fb.body.get(), [&](Ptr<Node> node) {
            node->curFile = &file;
            node->begin = member.begin;
            node->end = member.end;
            if (auto literal = DynamicCast<LitConstExpr*>(node); literal && literals.count(literal->stringValue)) {
                literal->stringValue = literals.at(literal->stringValue);
            }
            // Type placeholders carry a reference element in generated
            // annotations (Array<T>, Option<T>): bind the reference type and
            // register it like a support declaration, so name resolution and
            // speculative re-checks keep the class.
            if (auto refType = DynamicCast<RefType*>(node)) {
                BindArkTSRefTypePlaceholder(*refType, classRefs, pendingRefs);
                return VisitAction::WALK_CHILDREN;
            }
            if (auto ref = DynamicCast<RefExpr*>(node)) {
                bool rebound = false;
                for (size_t i = 0; i < params.size(); ++i) {
                    if (ref->ref.identifier == ARKTS_PLACEHOLDER_ARGUMENT + std::to_string(i)) {
                        ref->ref.identifier = params[i]->identifier;
                        ref->ref.target = params[i];
                        ref->SetTy(params[i]->type->GetTy());
                        ref->begin = member.end;
                        rebound = true;
                        break;
                    }
                }
                if (rebound) {
                    return VisitAction::WALK_CHILDREN;
                }
                if (ref->ref.identifier == ARKTS_SELF) {
                    ref->isThis = true;
                    ref->ref.identifier = SrcIdentifier("this");
                    ref->ref.target = &classCtx.owner;
                    ref->SetTy(classCtx.owner.GetTy());
                } else if (auto found = classRefs.find(ref->ref.identifier.Val()); found != classRefs.end()) {
                    ref->ref.identifier = found->second->identifier;
                    ref->ref.target = found->second;
                    ref->ref.targets = {found->second};
                    ref->SetTy(found->second->GetTy());
                }
            }
            if (auto returned = DynamicCast<ReturnExpr*>(node)) {
                returned->refFuncBody = &fb;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        // A generated member carries an internal spelling (the wrapping
        // constructor or the 'get'/'set' accessor), so the missing-support
        // diagnostic names the user-written declaration instead: the class for
        // a constructor, the property for an accessor, the member name
        // otherwise.
        std::string diagnosticName = memberName;
        if (member.TestAttr(Attribute::CONSTRUCTOR)) {
            diagnosticName = classCtx.owner.identifier.Val();
        } else if (member.propDecl != nullptr) {
            // An accessor always has a property name in practice; keep the
            // member name rather than overwriting it with an empty string.
            const auto propName = member.propDecl->identifier.Val();
            if (!propName.empty()) {
                diagnosticName = propName;
            }
        }
        if (!BindArkTSSupportReferences(
                member, *fb.body, *ci->importManager, diag, pendingRefs, diagnosticName)) {
            fb.body.reset();
            member.EnableAttr(Attribute::IS_BROKEN);
            fb.EnableAttr(Attribute::IS_BROKEN);
            classCtx.owner.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
        for (auto& [node, target] : pendingRefs) {
            arktsSupportRefs[node] = target;
        }
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
                ctx.AddDeclName({symbol->name, ScopeManagerApi::GetScopeNameWithoutTail(symbol->scopeName)}, *local);
            }
        }
        ctx.currentScopeName = oldScope;
        ctx.currentScopeLevel = oldLevel;
        member.DisableAttr(Attribute::ABSTRACT);
    };

    // Type description of one signature position, after alias resolution;
    // the spelling is shared with the function path, so a position cannot
    // mean two things across the two lowering paths.
    auto classify = [&](Ptr<Type> type) { return ArkTSClassifyPosition(ctx, type); };

    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto cd = DynamicCast<ClassDecl*>(decl.get());
            if (!cd || !IsArkTSClassWrapper(*cd) || cd->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            // Cross-language inheritance needs an ArkTS subclass on the other
            // side and a shared construction path; a wrapper only references
            // the object of its own class, so a wrapper parent is refused.
            for (auto& inherited : cd->inheritedTypes) {
                auto parent = Ty::GetDeclPtrOfTy(inherited->GetTy());
                if (parent && parent->TestAttr(Attribute::ARKTS_CLASS_WRAPPER)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *cd,
                        cd->identifier.Val(), "inheriting another @ArkTS foreign class is not supported");
                    cd->EnableAttr(Attribute::IS_BROKEN);
                    break;
                }
            }
            if (cd->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            std::unordered_map<std::string, std::string> options;
            ArkTSOptionStatus optionStatus;
            if (!ArkTSDeclOptions(*cd, options, &optionStatus)) {
                // A prepared wrapper is skipped by ArkTSDeclOptionsDiagnose
                // (its option fold is owned here), so this is the only place
                // the declaration's own option problem is reported. Keep the
                // problem kinds apart like the function path: a repeated key
                // and an ill-formed namespace have their own messages, and
                // the anchor is the offending argument when the fold has one,
                // else the @ArkTS layer - never annotations.front(), which may
                // be an unrelated annotation.
                auto annotationAt = [&cd]() -> Node& {
                    for (auto& annotation : cd->annotations) {
                        if (annotation->kind == AnnotationKind::ARKTS) {
                            return *annotation;
                        }
                    }
                    return *cd;
                };
                auto& at = optionStatus.at ? *optionStatus.at : annotationAt();
                if (optionStatus.problem == ArkTSOptionProblem::DUPLICATE) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_duplicated, at, optionStatus.option);
                } else if (optionStatus.problem == ArkTSOptionProblem::ILL_FORMED_NAMESPACE) {
                    // The fold assigns the namespace value into `options`
                    // before validating it, so the offending literal is
                    // already available here; fall back to the anchored
                    // literal if that ever changes, never to an empty value.
                    std::string namespaceValue =
                        options.count("namespace") ? options.at("namespace") : std::string();
                    if (namespaceValue.empty() && optionStatus.at != nullptr) {
                        if (auto literal = DynamicCast<LitConstExpr*>(optionStatus.at.get());
                            literal && literal->kind == LitConstKind::STRING) {
                            namespaceValue = literal->stringValue;
                        }
                    }
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_namespace_invalid, at, namespaceValue);
                } else {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_invalid, at);
                }
                cd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            // An explicitly written empty value (only whitespace counts as
            // empty) would flow into requireArkModule(""), getProperty("") or
            // an empty namespace placeholder and only fail at run time, the
            // same mistake the function path rejects. A prepared wrapper
            // class is skipped by ArkTSDeclOptionsDiagnose (its option fold is
            // owned here), so this class path applies the rule itself. The
            // fold above already guarantees every argument is a known string
            // literal; the scan reports on the @ArkTS layer carrying the
            // value, like the declaration-level walk.
            Ptr<Annotation> emptyLayer = nullptr;
            std::string emptyName;
            for (auto& annotation : cd->annotations) {
                if (annotation->kind != AnnotationKind::ARKTS) {
                    continue;
                }
                for (auto& arg : annotation->args) {
                    auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
                    if (!literal || literal->kind != LitConstKind::STRING ||
                        !ArkTSOptionValueEmpty(literal->stringValue)) {
                        continue;
                    }
                    emptyLayer = annotation.get();
                    emptyName = arg->name.Val();
                    break;
                }
                if (emptyLayer != nullptr) {
                    break;
                }
            }
            if (emptyLayer != nullptr) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_empty, *emptyLayer, emptyName);
                cd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            std::string module = options.count("module") ? options.at("module") : ArkTSDefaultModule(pkg.fullPackageName);
            // The function path refuses relative/absolute path spellings before
            // emitting requireArkModule; a prepared class owns its own option
            // fold and must apply the same rule. Existence and named-module
            // resolution stay with the runtime loader.
            if (options.count("module") != 0 && ArkTSModulePathLike(module)) {
                Ptr<Node> moduleNode = nullptr;
                for (auto& annotation : cd->annotations) {
                    if (annotation->kind != AnnotationKind::ARKTS) {
                        continue;
                    }
                    for (auto& arg : annotation->args) {
                        if (arg->name.Val() == "module") {
                            moduleNode = arg->expr.get();
                            break;
                        }
                    }
                    if (moduleNode != nullptr) {
                        break;
                    }
                }
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid,
                    moduleNode ? *moduleNode : static_cast<Node&>(*cd), cd->identifier.Val(),
                    "module option must be an ArkTS module specifier, not a path");
                cd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            if (module.empty()) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_arkts_foreign_class_invalid, *cd, cd->identifier.Val(), "empty module name");
                cd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            ClassContext classCtx{*cd, ArkTSReceiverFor(options.count("module") != 0, module), module,
                options.count("foreignName") ? options.at("foreignName") : cd->identifier.Val(),
                ArkTSNamespaceSegments(options.count("namespace") ? options.at("namespace") : std::string())};
            for (auto& member : cd->GetMemberDecls()) {
                auto fd = DynamicCast<FuncDecl*>(member.get());
                // A parsed member always carries a FuncBody. A bodyless
                // FuncDecl can only come from a malformed AST, where there is
                // no user spelling to anchor a diagnostic on; skip it like the
                // function path instead of dereferencing a null FuncBody.
                if (!fd || !fd->funcBody) {
                    continue;
                }
                if (IsWrappingConstructor(*fd) || IsReferenceAccessor(*fd)) {
                    continue;
                }
                // A member the option fold already refused (for example a
                // named parameter without a default) is broken and must not
                // receive a generated body; the class may still lower its
                // remaining members.
                if (fd->TestAttr(Attribute::IS_BROKEN)) {
                    continue;
                }
                // An operator member ('operator func []', 'operator func +',
                // ...) has no method-name spelling: the member wrapper would
                // call the operator's source token as a method (for example
                // callMethod("[]")), which is not the ArkTS operator
                // contract. Refuse the member before any signature
                // classification; the class may still lower its other
                // members, like every neighboring refusal.
                if (fd->op != TokenKind::ILLEGAL) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *fd,
                        cd->identifier.Val(), "operator members on foreign class are not supported");
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    cd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                auto& fb = *fd->funcBody;
                // Optional (defaulted) parameters need a desugared default
                // function whose body is created before member bodies exist;
                // the wrapper path does not provide one, so refuse the shape
                // instead of emitting a wrapper that cannot be called.
                bool hasOptional = false;
                for (auto& list : fb.paramLists) {
                    for (auto& param : list->params) {
                        hasOptional = hasOptional || param->assignment != nullptr;
                    }
                }
                if (hasOptional) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *fd,
                        cd->identifier.Val(), "optional parameters on foreign class members are not supported");
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    cd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                bool constructor = fd->TestAttr(Attribute::CONSTRUCTOR);
                auto resultType = classify(fb.retType.get());
                std::vector<ArkTSTypeDesc> types;
                std::vector<Ptr<FuncParam>> params;
                bool supported = true;
                for (auto& list : fb.paramLists) {
                    for (auto& param : list->params) {
                        auto desc = classify(param->type.get());
                        if (!ArkTSTypeDescGeneratable(desc) || ArkTSTypeDescContainsNothing(desc)) {
                            auto& position =
                                param->type ? static_cast<Node&>(*param->type) : static_cast<Node&>(*param);
                            auto deferredName =
                                param->type ? ArkTSDeferredExternalTypeName(param->type->GetTy()) : std::string{};
                            if (!deferredName.empty()) {
                                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_external_type_deferred,
                                    position, deferredName);
                            } else {
                                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_param_type_unsupported,
                                    position, param->identifier.Val(), fd->identifier.Val(),
                                    param->type ? Ty::ToString(param->type->GetTy()) : std::string());
                            }
                            supported = false;
                        }
                        types.emplace_back(std::move(desc));
                        params.emplace_back(param.get());
                    }
                }
                if (!constructor && !ArkTSTypeDescGeneratable(resultType)) {
                    if (fb.retType && !fb.retType->begin.IsZero()) {
                        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, *fb.retType,
                            Ty::ToString(fb.retType->GetTy()), fd->identifier.Val());
                    } else {
                        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, *fd, "",
                            fd->identifier.Val());
                    }
                    supported = false;
                }
                if (!supported) {
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    cd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                // The rest scan covers every flattened position of a class
                // member (the function path scans its first list only); the
                // position must be the trailing Array<T'>, its element
                // already validated by the parameter loop above.
                size_t restIndex = ArkTSRestPosition(params, params.size());
                if (restIndex != ARKTS_NO_REST &&
                    (restIndex + 1 != types.size() || types[restIndex].kind != ArkTSTypeDesc::Kind::ARRAY_COPY)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *fd,
                        cd->identifier.Val(), "unsupported rest parameter");
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    cd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                auto prefix = ArkTSForeignLocalPrefix(params);
                std::unordered_map<std::string, Ptr<Decl>> classRefs;
                std::string code;
                if (constructor) {
                    code = ArkTSClassConstructorSource(types, classCtx.namespaces.size(), prefix, restIndex,
                        classCtx.receiver, classRefs);
                } else {
                    code = ArkTSClassMethodSource(types, resultType, classCtx.namespaces.size(), prefix, restIndex,
                        fd->TestAttr(Attribute::STATIC) ? ArkTSMemberReceiver::STATIC : ArkTSMemberReceiver::INSTANCE,
                        classCtx.receiver, classRefs);
                }
                if (code.empty()) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *fd,
                        cd->identifier.Val(), "unsupported member signature");
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    cd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                installBody(classCtx, *fd, *file, code, params, GetArkTSMemberName(*fd), classRefs);
            }
            for (auto& member : cd->GetMemberDecls()) {
                auto pd = DynamicCast<PropDecl*>(member.get());
                // A broken property (or one prepared for generated accessors)
                // gets no body; the class may still lower its other members.
                if (!pd || pd->TestAttr(Attribute::COMPILER_ADD) || pd->TestAttr(Attribute::IS_BROKEN)) {
                    continue;
                }
                auto desc = classify(pd->type.get());
                if (!ArkTSTypeDescGeneratable(desc) || ArkTSTypeDescContainsNothing(desc)) {
                    if (pd->type) {
                        auto deferredName = ArkTSDeferredExternalTypeName(pd->type->GetTy());
                        if (!deferredName.empty()) {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_external_type_deferred, *pd->type,
                                deferredName);
                        } else if (pd->isVar) {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_param_type_unsupported,
                                *pd->type, pd->identifier.Val(), pd->identifier.Val(),
                                Ty::ToString(pd->type->GetTy()));
                        } else {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported,
                                *pd->type, Ty::ToString(pd->type->GetTy()), pd->identifier.Val());
                        }
                    }
                    pd->EnableAttr(Attribute::IS_BROKEN);
                    cd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                auto receiver = pd->TestAttr(Attribute::STATIC) ? ArkTSMemberReceiver::STATIC
                                                               : ArkTSMemberReceiver::INSTANCE;
                std::string name = GetArkTSMemberName(*pd);
                for (auto& getter : pd->getters) {
                    if (!getter->funcBody) {
                        continue;
                    }
                    std::unordered_map<std::string, Ptr<Decl>> classRefs;
                    auto code = ArkTSClassPropertySource(
                        desc, false, receiver, classCtx.namespaces.size(), ArkTSForeignLocalPrefix({}), classCtx.receiver,
                        classRefs);
                    if (code.empty()) {
                        getter->EnableAttr(Attribute::IS_BROKEN);
                        cd->EnableAttr(Attribute::IS_BROKEN);
                        continue;
                    }
                    installBody(classCtx, *getter, *file, code, {}, name, classRefs);
                }
                for (auto& setter : pd->setters) {
                    if (!setter->funcBody || setter->funcBody->paramLists.empty() ||
                        setter->funcBody->paramLists.front()->params.empty()) {
                        continue;
                    }
                    std::unordered_map<std::string, Ptr<Decl>> classRefs;
                    auto code = ArkTSClassPropertySource(
                        desc, true, receiver, classCtx.namespaces.size(), ArkTSForeignLocalPrefix({}), classCtx.receiver,
                        classRefs);
                    if (code.empty()) {
                        setter->EnableAttr(Attribute::IS_BROKEN);
                        cd->EnableAttr(Attribute::IS_BROKEN);
                        continue;
                    }
                    installBody(classCtx, *setter, *file, code,
                        {setter->funcBody->paramLists.front()->params.front().get()}, name, classRefs);
                }
            }
        }
    }
}
