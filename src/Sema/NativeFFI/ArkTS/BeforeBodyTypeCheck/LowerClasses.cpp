// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
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
// The generated wrapping constructors, parsed from the compiler-owned holder
// shape and marked COMPILER_ADD. A root wrapper declares two overloads and a
// child wrapper redeclares both as delegations; either way the first parameter
// is the value the wrapper recovers, spelled by the shared parameter name:
//   public init(__cj_arkts_ref: JSObject, _: Unit) { ... }
//   public init(__cj_arkts_ref: () -> JSObject, _: Unit) { ... }
bool IsWrappingConstructor(const FuncDecl& fd)
{
    return fd.TestAttr(Attribute::COMPILER_ADD) && fd.funcBody && fd.funcBody->paramLists.size() == 1 &&
        !fd.funcBody->paramLists.front()->params.empty() &&
        fd.funcBody->paramLists.front()->params.front()->identifier.Val() == ARKTS_CLASS_CTOR_PARAM;
}

// Whether this wrapper class declares the reference field itself (a root
// wrapper): preparation inserts the field only on a class without a wrapper
// parent, so the generated constructor body must match the members the class
// actually owns - the root assigns its field directly, a child forwards the
// new object through super(...) to the root wrapper that stores it. This only
// mirrors preparation's root/child decision; it cannot detect a preparation
// misjudgment by itself, so the wrapper-parent invariant in LowerArkTSClasses
// refuses a child-shaped class whose resolved parents include no wrapper
// before any super(...) body is generated.
bool OwnsReferenceField(ClassDecl& cd)
{
    for (auto& member : cd.GetMemberDecls()) {
        if (member->astKind == ASTKind::VAR_DECL && member->TestAttr(Attribute::COMPILER_ADD) &&
            member->identifier.Val() == ARKTS_CLASS_REF_FIELD) {
            return true;
        }
    }
    return false;
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

// The user-written parent declarations of a class, in source order. A
// compiler-added superclass (Object) is not user-written and never takes part
// in the wrapper parent rules; a parent without a resolved declaration is
// skipped the same way.
std::vector<Ptr<Decl>> UserParentDecls(const ClassDecl& cd)
{
    std::vector<Ptr<Decl>> parents;
    for (auto& inherited : cd.inheritedTypes) {
        if (inherited->TestAttr(Attribute::COMPILER_ADD)) {
            continue;
        }
        if (auto parent = Ty::GetDeclPtrOfTy(inherited->GetTy())) {
            parents.emplace_back(parent);
        }
    }
    return parents;
}

// Resolve a type alias in an inherited-type position to its target
// declaration. A non-alias declaration is returned unchanged; a broken or
// cyclic alias chain yields null.
Ptr<Decl> ResolveArkTSAliasTarget(Ptr<Decl> decl)
{
    std::unordered_set<const Decl*> seen;
    while (decl && seen.insert(decl).second) {
        auto alias = DynamicCast<TypeAliasDecl*>(decl);
        if (!alias) {
            return decl;
        }
        decl = alias->type ? Ty::GetDeclPtrOfTy(alias->type->GetTy()) : nullptr;
    }
    return nullptr;
}

// The first prepared foreign interface reachable through the inheritance
// graph, or null. An extend declaration contributes its own inherited types,
// so the same walk covers classes, structs, enums and extends. A type alias
// in an inherited-type position is followed to its target.
Ptr<Decl> FindArkTSForeignInterfaceAncestor(const InheritableDecl& decl)
{
    std::vector<Ptr<Decl>> pending;
    auto addParents = [&pending](const InheritableDecl& inheritable) {
        for (auto& inherited : inheritable.inheritedTypes) {
            auto target = inherited ? ResolveArkTSAliasTarget(Ty::GetDeclPtrOfTy(inherited->GetTy())) : nullptr;
            if (target) {
                pending.emplace_back(target);
            }
        }
    };
    addParents(decl);
    std::unordered_set<const Decl*> seen;
    while (!pending.empty()) {
        auto current = pending.back();
        pending.pop_back();
        if (!current || !seen.insert(current).second) {
            continue;
        }
        if (current->TestAttr(Attribute::ARKTS_FOREIGN_INTERFACE)) {
            return current;
        }
        if (auto inheritable = DynamicCast<InheritableDecl*>(current)) {
            addParents(*inheritable);
        }
    }
    return nullptr;
}

// Mark the generated implementation class and its members as cascade-broken:
// the interface's own diagnostic is the actionable one, so inheritance and
// initialization checks must not restate it for the generated class.
void MarkBrokenInterfaceImpl(Decl& impl)
{
    impl.EnableAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN);
    if (auto cd = DynamicCast<ClassDecl*>(&impl)) {
        for (auto& member : cd->GetMemberDecls()) {
            member->EnableAttr(Attribute::HAS_BROKEN);
        }
    }
}

// One class-wide lowering context shared by every member.
struct ClassContext {
    // Classes and interfaces share the member-installation path; the owner is
    // the prepared class or the prepared interface.
    Decl& owner;
    ArkTSReceiverKind receiver;
    std::string module;
    std::string className;
    std::vector<std::string> namespaces;
    DiagKindRefactor invalidKind{DiagKindRefactor::sema_arkts_foreign_class_invalid};
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
    return ArkTSClassifyType(type->GetTy(), ci->importManager);
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
    // Only the generated implementation class may implement a prepared
    // foreign interface: the interface dispatches through an ArkTS object,
    // while a Cangjie declaration has no shared reference. Refuse every other
    // concrete implementer, including one that reaches the interface through
    // a local interface or an extend declaration. A foreign interface itself
    // is abstract and may inherit another prepared interface, so it is not an
    // implementer for this rule.
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            // Extend declarations reach this rule from PreCheckExtend, after
            // their implemented-interface types are resolved.
            if (decl->TestAttr(Attribute::IS_BROKEN) || decl->TestAttr(Attribute::ARKTS_INTERFACE_IMPL) ||
                decl->TestAttr(Attribute::ARKTS_FOREIGN_INTERFACE) || decl->astKind == ASTKind::INTERFACE_DECL ||
                decl->astKind == ASTKind::EXTEND_DECL) {
                continue;
            }
            auto inheritable = DynamicCast<InheritableDecl*>(decl.get());
            if (!inheritable) {
                continue;
            }
            auto foreignInterface = FindArkTSForeignInterfaceAncestor(*inheritable);
            if (!foreignInterface) {
                continue;
            }
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_impl_invalid, *decl,
                decl->identifier.Val(), foreignInterface->identifier.Val());
            decl->EnableAttr(Attribute::IS_BROKEN);
        }
    }


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
            diag.DiagnoseRefactor(classCtx.invalidKind, member, classCtx.owner.identifier.Val(),
                "cannot generate the wrapper body");
            // A generated-body failure belongs to this member only; the other
            // class members still lower. An interface default implementation
            // owns the member contract, so the interface itself is broken too.
            member.EnableAttr(Attribute::IS_BROKEN);
            if (classCtx.invalidKind == DiagKindRefactor::sema_arkts_foreign_interface_invalid) {
                classCtx.owner.EnableAttr(Attribute::IS_BROKEN);
            }
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
            // The missing support belongs to this member's body, so only the
            // member is refused and the class keeps lowering the rest.
            member.EnableAttr(Attribute::IS_BROKEN);
            fb.EnableAttr(Attribute::IS_BROKEN);
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
    auto analyzeInterfaceSignature = [&](FuncDecl& fd, const std::string& ownerName,
                                         DiagKindRefactor invalidKind, const char* optionalReason,
                                         std::vector<ArkTSTypeDesc>& types, std::vector<Ptr<FuncParam>>& params,
                                         ArkTSTypeDesc& resultType, size_t& restIndex) {
        auto& fb = *fd.funcBody;
        bool hasOptional = false;
        for (auto& list : fb.paramLists) {
            for (auto& param : list->params) {
                hasOptional = hasOptional || param->assignment != nullptr;
            }
        }
        if (hasOptional) {
            diag.DiagnoseRefactor(invalidKind, fd, ownerName, optionalReason);
            fd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
        bool supported = true;
        for (auto& list : fb.paramLists) {
            for (auto& param : list->params) {
                auto desc = classify(param->type.get());
                if (!ArkTSTypeDescGeneratable(desc) || ArkTSTypeDescContainsNothing(desc)) {
                    auto& position = param->type ? static_cast<Node&>(*param->type) : static_cast<Node&>(*param);
                    auto deferredName =
                        param->type ? ArkTSDeferredExternalTypeName(param->type->GetTy()) : std::string{};
                    if (!deferredName.empty()) {
                        diag.DiagnoseRefactor(
                            DiagKindRefactor::sema_arkts_foreign_external_type_deferred, position, deferredName);
                    } else {
                        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_param_type_unsupported, position,
                            param->identifier.Val(), fd.identifier.Val(),
                            param->type ? Ty::ToString(param->type->GetTy()) : std::string());
                    }
                    supported = false;
                }
                types.emplace_back(std::move(desc));
                params.emplace_back(param.get());
            }
        }
        resultType = classify(fb.retType.get());
        if (!ArkTSTypeDescGeneratable(resultType)) {
            if (fb.retType && !fb.retType->begin.IsZero()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, *fb.retType,
                    Ty::ToString(fb.retType->GetTy()), fd.identifier.Val());
            } else {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, fd, "", fd.identifier.Val());
            }
            supported = false;
        }
        if (!supported) {
            fd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
        restIndex = ArkTSRestPosition(params, params.size());
        if (restIndex != ARKTS_NO_REST &&
            (restIndex + 1 != types.size() || types[restIndex].kind != ArkTSTypeDesc::Kind::ARRAY_COPY)) {
            diag.DiagnoseRefactor(invalidKind, fd, ownerName, "unsupported rest parameter");
            fd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
        return true;
    };

    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto cd = DynamicCast<ClassDecl*>(decl.get());
            if (!cd || !IsArkTSClassWrapper(*cd) || cd->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            // A wrapper parent is the R-D3 supported shape: the child keeps
            // only the ArkTS object its own constructor builds and forwards it
            // to the root wrapper's storage through super(...), so no wrapper
            // parent is refused here anymore. The remaining parent rules below
            // still apply, and Cangjie's own inheritance checks (the parent
            // must be open, first-position superclass and the like) run in the
            // generic class checking.
            auto parents = UserParentDecls(*cd);
            // The authority design requires every parent to carry @ArkTS; an
            // unannotated parent would make the wrapper reference a class the
            // ArkTS side does not have.
            for (auto& parent : parents) {
                if (!parent->HasAnno(AnnotationKind::ARKTS) &&
                    !parent->TestAttr(Attribute::ARKTS)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *cd,
                        cd->identifier.Val(),
                        "parent type '" + parent->identifier.Val() + "' must be an @ArkTS class or interface");
                    cd->EnableAttr(Attribute::IS_BROKEN);
                    break;
                }
            }
            if (cd->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            // A child wrapper owns no reference field: every constructor body
            // generated for it reaches the root wrapper's storage through
            // super(...). Preparation guarantees such a class a wrapper
            // parent; a class whose parents resolved here to no wrapper at
            // all means the preparation spelling match was wrong, and the
            // super(...) delegations would resolve against a parent
            // constructor that does not exist. Refuse the class instead of
            // generating that body - preparation's own refusals above (a
            // broken parent breaks the child there) make this unreachable
            // for every shape preparation can still judge.
            if (!OwnsReferenceField(*cd) &&
                std::none_of(parents.begin(), parents.end(), [](Ptr<Decl> parent) { return IsArkTSClassWrapper(*parent); })) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *cd,
                    cd->identifier.Val(), "a wrapper class without its own reference field must have an @ArkTS wrapper parent");
                cd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            std::unordered_map<std::string, std::string> options;
            ArkTSOptionStatus optionStatus;
            if (!ArkTSDeclOptions(*cd, options, &optionStatus)) {
                DiagnoseArkTSOptionProblem(diag, *cd, options, optionStatus);
                cd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            std::string module;
            if (!ResolveArkTSModule(*cd, options, pkg, diag, DiagKindRefactor::sema_arkts_foreign_class_invalid,
                    module)) {
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
                    continue;
                }
                auto& fb = *fd->funcBody;
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
                    continue;
                }
                // ArkTS optional parameters are the trailing named parameters
                // that carry a default value (`x!: Option<T> = None`), the
                // same written-spelling gate the function path uses: only such
                // a parameter may be omitted by the caller, and a positional
                // Option (the required nullable argument of the with-rest/open
                // form) is still always forwarded.
                size_t optionalStart = params.size();
                while (optionalStart > 0 && params[optionalStart - 1]->isNamedParam &&
                    params[optionalStart - 1]->assignment != nullptr &&
                    types[optionalStart - 1].kind == ArkTSTypeDesc::Kind::OPTIONAL) {
                    --optionalStart;
                }
                auto prefix = ArkTSForeignLocalPrefix(params);
                std::unordered_map<std::string, Ptr<Decl>> classRefs;
                std::string code;
                if (constructor) {
                    // A child wrapper (no field of its own) moves the whole
                    // construction into the super(...) thunk; a root wrapper
                    // keeps the direct field assignment.
                    auto form = OwnsReferenceField(*cd) ? ArkTSConstructorForm::DIRECT
                                                        : ArkTSConstructorForm::SUPER_THUNK;
                    code = ArkTSClassConstructorSource(types, classCtx.namespaces.size(), prefix, restIndex,
                        classCtx.receiver, optionalStart, classRefs, form);
                } else {
                    code = ArkTSClassMethodSource(types, resultType, classCtx.namespaces.size(), prefix, restIndex,
                        fd->TestAttr(Attribute::STATIC) ? ArkTSMemberReceiver::STATIC : ArkTSMemberReceiver::INSTANCE,
                        classCtx.receiver, optionalStart, classRefs);
                }
                if (code.empty()) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *fd,
                        cd->identifier.Val(), "unsupported member signature");
                    fd->EnableAttr(Attribute::IS_BROKEN);
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
                    continue;
                }
                auto receiver = pd->TestAttr(Attribute::STATIC) ? ArkTSMemberReceiver::STATIC
                                                               : ArkTSMemberReceiver::INSTANCE;
                std::string name = GetArkTSMemberName(*pd);
                // A @WriteOnly property keeps its (private) getter body: the
                // accessor remains part of the lowered property contract and a
                // non-abstract member must not stay bodyless. Reads are
                // rejected by the write-only reference rule, not by dropping
                // the body.
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
                        continue;
                    }
                    installBody(classCtx, *setter, *file, code,
                        {setter->funcBody->paramLists.front()->params.front().get()}, name, classRefs);
                }
            }
        }
    }
    // A prepared interface that fails (preparation or lowering) can be the
    // parent of another interface. Propagate the broken mark before and after
    // the interface loop, so a child declared earlier or in another file does
    // not escape the check when its parent fails later in the same pass, and
    // mark each child's generated implementation class broken too.
    auto propagateBrokenInterfaceParents = [&]() {
        for (bool changed = true; changed;) {
            changed = false;
            for (auto& file : pkg.files) {
                for (auto& decl : file->decls) {
                    auto id = DynamicCast<InterfaceDecl*>(decl.get());
                    if (!id || !id->TestAttr(Attribute::ARKTS_FOREIGN_INTERFACE) ||
                        id->TestAttr(Attribute::IS_BROKEN)) {
                        continue;
                    }
                    for (auto& inherited : id->inheritedTypes) {
                        auto parent = ResolveArkTSAliasTarget(Ty::GetDeclPtrOfTy(inherited->GetTy()));
                        const bool brokenForeignParent = parent && parent->TestAttr(Attribute::IS_BROKEN) &&
                            (parent->TestAttr(Attribute::ARKTS_FOREIGN_INTERFACE) ||
                                (parent->TestAttr(Attribute::FOREIGN) &&
                                    parent->astKind == ASTKind::INTERFACE_DECL));
                        if (!brokenForeignParent) {
                            continue;
                        }
                        id->EnableAttr(Attribute::IS_BROKEN);
                        if (auto implDecl = ArkTSInterfaceImpl(*id, ci->importManager)) {
                            MarkBrokenInterfaceImpl(*implDecl);
                        }
                        changed = true;
                        break;
                    }
                }
            }
        }
    };
    propagateBrokenInterfaceParents();

    // Prepared foreign interfaces: every member becomes a default
    // implementation over this.ref(). The generated implementation class is a
    // plain class with an already complete body and needs no further lowering.
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto id = DynamicCast<InterfaceDecl*>(decl.get());
            if (!id || !id->TestAttr(Attribute::ARKTS_FOREIGN_INTERFACE) || id->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            std::unordered_map<std::string, std::string> options;
            ArkTSOptionStatus optionStatus;
            if (!ArkTSDeclOptions(*id, options, &optionStatus)) {
                DiagnoseArkTSOptionProblem(diag, *id, options, optionStatus);
                id->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            std::string module;
            if (!ResolveArkTSModule(*id, options, pkg, diag, DiagKindRefactor::sema_arkts_foreign_interface_invalid,
                    module)) {
                id->EnableAttr(Attribute::IS_BROKEN);
                if (auto implDecl = ArkTSInterfaceImpl(*id, ci->importManager)) {
                    MarkBrokenInterfaceImpl(*implDecl);
                }
                continue;
            }
            ClassContext declCtx{*id, ArkTSReceiverFor(options.count("module") != 0, module), module,
                options.count("foreignName") ? options.at("foreignName") : id->identifier.Val(),
                ArkTSNamespaceSegments(options.count("namespace") ? options.at("namespace") : std::string())};
            declCtx.invalidKind = DiagKindRefactor::sema_arkts_foreign_interface_invalid;
            // Only another prepared foreign interface may be a parent: its
            // members already dispatch through the same accessor shape.
            for (auto& inherited : id->inheritedTypes) {
                auto parent = ResolveArkTSAliasTarget(Ty::GetDeclPtrOfTy(inherited->GetTy()));
                // A foreign parent that already failed reported its own
                // diagnostic; do not restate the relation as a second error.
                // A broken local interface is not a foreign parent and keeps
                // the ordinary relation refusal.
                const bool brokenForeignParent = parent && parent->TestAttr(Attribute::IS_BROKEN) &&
                    (parent->TestAttr(Attribute::ARKTS_FOREIGN_INTERFACE) ||
                        (parent->TestAttr(Attribute::FOREIGN) && parent->astKind == ASTKind::INTERFACE_DECL));
                if (brokenForeignParent) {
                    id->EnableAttr(Attribute::IS_BROKEN);
                    break;
                }
                if (!parent || !parent->TestAttr(Attribute::ARKTS_FOREIGN_INTERFACE)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *id,
                        id->identifier.Val(), "a foreign interface may only inherit another foreign interface");
                    id->EnableAttr(Attribute::IS_BROKEN);
                    break;
                }
            }
            if (id->TestAttr(Attribute::IS_BROKEN)) {
                // The implementation class exists before this refusal, so it
                // must be marked broken here as well; otherwise later
                // inheritance/initialization checks report it as a second,
                // unrelated diagnostic.
                if (auto implDecl = ArkTSInterfaceImpl(*id, ci->importManager)) {
                    MarkBrokenInterfaceImpl(*implDecl);
                }
                continue;
            }
            for (auto& member : id->GetMemberDecls()) {
                auto fd = DynamicCast<FuncDecl*>(member.get());
                if (!fd || !fd->funcBody || fd->identifier.Val() == ARKTS_CLASS_REF_ACCESSOR) {
                    continue;
                }
                const bool indexOperator = fd->TestAttr(Attribute::OPERATOR) && fd->identifier.Val() == "[]";
                const bool callOperator = fd->TestAttr(Attribute::OPERATOR) && fd->identifier.Val() == "()";
                std::vector<ArkTSTypeDesc> types;
                std::vector<Ptr<FuncParam>> params;
                ArkTSTypeDesc resultType;
                size_t restIndex = ARKTS_NO_REST;
                if (!analyzeInterfaceSignature(*fd, id->identifier.Val(),
                        DiagKindRefactor::sema_arkts_foreign_interface_invalid,
                        "optional parameters on foreign interface members are not supported", types, params, resultType,
                        restIndex)) {
                    id->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                const auto prefix = ArkTSForeignLocalPrefix(params);
                std::unordered_map<std::string, Ptr<Decl>> classRefs;
                std::string code;
                if (indexOperator) {
                    if (restIndex != ARKTS_NO_REST) {
                        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *fd,
                            id->identifier.Val(), "an index signature must not use a rest parameter");
                        fd->EnableAttr(Attribute::IS_BROKEN);
                        id->EnableAttr(Attribute::IS_BROKEN);
                        continue;
                    }
                    const ArkTSTypeDesc* valueType = nullptr;
                    bool setter = false;
                    if (types.size() == 1) {
                        if (resultType.kind != ArkTSTypeDesc::Kind::OPTIONAL ||
                            resultType.elements.size() != 1) {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *fd,
                                id->identifier.Val(), "an index getter must return Option<T>");
                            fd->EnableAttr(Attribute::IS_BROKEN);
                            id->EnableAttr(Attribute::IS_BROKEN);
                            continue;
                        }
                        valueType = &resultType.elements.front();
                    } else if (types.size() == 2) {
                        if (!params[1]->isNamedParam) {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *fd,
                                id->identifier.Val(), "an index setter's value parameter must be named");
                            fd->EnableAttr(Attribute::IS_BROKEN);
                            id->EnableAttr(Attribute::IS_BROKEN);
                            continue;
                        }
                        if (resultType.kind != ArkTSTypeDesc::Kind::SCALAR || resultType.scalar != "Unit") {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *fd,
                                id->identifier.Val(), "an index setter must return Unit");
                            fd->EnableAttr(Attribute::IS_BROKEN);
                            id->EnableAttr(Attribute::IS_BROKEN);
                            continue;
                        }
                        valueType = &types[1];
                        setter = true;
                    } else {
                        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *fd,
                            id->identifier.Val(), "an index signature takes one key or a key and a named value");
                        fd->EnableAttr(Attribute::IS_BROKEN);
                        id->EnableAttr(Attribute::IS_BROKEN);
                        continue;
                    }
                    code = ArkTSInterfaceIndexSource(types[0], *valueType, setter, prefix, classRefs);
                } else if (callOperator) {
                    if (restIndex != ARKTS_NO_REST) {
                        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *fd,
                            id->identifier.Val(), "a call signature must not use a rest parameter");
                        fd->EnableAttr(Attribute::IS_BROKEN);
                        id->EnableAttr(Attribute::IS_BROKEN);
                        continue;
                    }
                    code = ArkTSInterfaceCallSource(types, resultType, prefix, classRefs);
                } else {
                    // Interface members reject optional parameters, so the
                    // trailing optional run is empty.
                    code = ArkTSClassMethodSource(types, resultType, 0, prefix, restIndex,
                        ArkTSMemberReceiver::INSTANCE, declCtx.receiver, params.size(), classRefs);
                }
                if (code.empty()) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *fd,
                        id->identifier.Val(), "unsupported member signature");
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    id->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                installBody(declCtx, *fd, *file, code, params, GetArkTSMemberName(*fd), classRefs);
                if (fd->funcBody->body && !fd->TestAttr(Attribute::IS_BROKEN)) {
                    fd->DisableAttr(Attribute::ABSTRACT);
                    fd->EnableAttr(Attribute::DEFAULT, Attribute::INTERFACE_IMPL);
                }
            }
            for (auto& member : id->GetMemberDecls()) {
                auto pd = DynamicCast<PropDecl*>(member.get());
                if (!pd) {
                    continue;
                }
                auto desc = classify(pd->type.get());
                if (!ArkTSTypeDescGeneratable(desc) || ArkTSTypeDescContainsNothing(desc)) {
                    if (pd->type) {
                        auto deferredName = ArkTSDeferredExternalTypeName(pd->type->GetTy());
                        if (!deferredName.empty()) {
                            diag.DiagnoseRefactor(
                                DiagKindRefactor::sema_arkts_foreign_external_type_deferred, *pd->type, deferredName);
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
                    id->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                std::string name = GetArkTSMemberName(*pd);
                for (auto& getter : pd->getters) {
                    if (!getter->funcBody) {
                        continue;
                    }
                    std::unordered_map<std::string, Ptr<Decl>> classRefs;
                    auto code = ArkTSClassPropertySource(desc, false, ArkTSMemberReceiver::INSTANCE, 0,
                        ArkTSForeignLocalPrefix({}), declCtx.receiver, classRefs);
                    if (code.empty()) {
                        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *pd,
                            id->identifier.Val(), "unsupported property signature");
                        getter->EnableAttr(Attribute::IS_BROKEN);
                        pd->EnableAttr(Attribute::IS_BROKEN);
                        id->EnableAttr(Attribute::IS_BROKEN);
                        continue;
                    }
                    installBody(declCtx, *getter, *file, code, {}, name, classRefs);
                    if (getter->TestAttr(Attribute::IS_BROKEN)) {
                        pd->EnableAttr(Attribute::IS_BROKEN);
                    }
                    if (getter->funcBody->body && !getter->TestAttr(Attribute::IS_BROKEN)) {
                        getter->DisableAttr(Attribute::ABSTRACT);
                        getter->EnableAttr(Attribute::DEFAULT, Attribute::INTERFACE_IMPL);
                    }
                }
                for (auto& setter : pd->setters) {
                    if (!setter->funcBody || setter->funcBody->paramLists.empty() ||
                        setter->funcBody->paramLists.front()->params.empty()) {
                        continue;
                    }
                    std::unordered_map<std::string, Ptr<Decl>> classRefs;
                    auto code = ArkTSClassPropertySource(desc, true, ArkTSMemberReceiver::INSTANCE, 0,
                        ArkTSForeignLocalPrefix({}), declCtx.receiver, classRefs);
                    if (code.empty()) {
                        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, *pd,
                            id->identifier.Val(), "unsupported property signature");
                        setter->EnableAttr(Attribute::IS_BROKEN);
                        pd->EnableAttr(Attribute::IS_BROKEN);
                        id->EnableAttr(Attribute::IS_BROKEN);
                        continue;
                    }
                    installBody(declCtx, *setter, *file, code,
                        {setter->funcBody->paramLists.front()->params.front().get()}, name, classRefs);
                    if (setter->TestAttr(Attribute::IS_BROKEN)) {
                        pd->EnableAttr(Attribute::IS_BROKEN);
                    }
                    if (setter->funcBody->body && !setter->TestAttr(Attribute::IS_BROKEN)) {
                        setter->DisableAttr(Attribute::ABSTRACT);
                        setter->EnableAttr(Attribute::DEFAULT, Attribute::INTERFACE_IMPL);
                    }
                }
                if (!pd->TestAttr(Attribute::IS_BROKEN)) {
                    pd->DisableAttr(Attribute::ABSTRACT);
                    pd->EnableAttr(Attribute::DEFAULT, Attribute::INTERFACE_IMPL);
                }
            }
            if (id->TestAttr(Attribute::IS_BROKEN)) {
                if (auto implDecl = ArkTSInterfaceImpl(*id, ci->importManager)) {
                    MarkBrokenInterfaceImpl(*implDecl);
                }
            }
        }
    }
    propagateBrokenInterfaceParents();

    // Member bodies now exist, so a member with a defaulted parameter is no
    // longer treated as bodyless by the generic default-function pass. Build
    // and type-check its default-argument functions against the final types.
    // A broken class is a class-level refusal (its members were never lowered
    // and its default functions, if any, were built by the generic pass), so
    // the post-pass leaves it alone. Member-level refusals no longer mark the
    // class: a broken member is skipped individually below, so one refused
    // member never suppresses the default functions of its siblings.
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto cd = DynamicCast<ClassDecl*>(decl.get());
            if (!cd || !IsArkTSClassWrapper(*cd) || cd->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            for (auto& member : cd->GetMemberDecls()) {
                auto fd = DynamicCast<FuncDecl*>(member.get());
                if (!fd || !fd->funcBody || fd->TestAttr(Attribute::IS_BROKEN)) {
                    continue;
                }
                bool hasDefault = false;
                for (auto& list : fd->funcBody->paramLists) {
                    for (auto& param : list->params) {
                        hasDefault = hasDefault || param->assignment != nullptr;
                    }
                }
                if (!hasDefault) {
                    continue;
                }
                // A mapping-only spelling can leave HAS_INITIAL set without a
                // desugar declaration; clear it so the pass rebuilds one.
                for (auto& list : fd->funcBody->paramLists) {
                    for (auto& param : list->params) {
                        if (param->assignment && !param->desugarDecl && param->TestAttr(Attribute::HAS_INITIAL)) {
                            param->DisableAttr(Attribute::HAS_INITIAL);
                        }
                    }
                }
                // The default-argument function clones the preceding
                // parameters and copies their own Ty, so the manual pass must
                // publish the resolved parameter types first. Otherwise the
                // second and later defaults carry an UnknownType parameter.
                for (auto& list : fd->funcBody->paramLists) {
                    for (auto& param : list->params) {
                        if (param->type && Ty::IsTyCorrect(param->type->GetTy())) {
                            param->SetTy(param->type->GetTy());
                        }
                    }
                }
                // The shared default-parameter pass rejects optional parameters
                // on open/default members. Do not rebuild the mapping-only
                // default function here, otherwise the same rule is reported
                // twice for one parameter list.
                if (fd->TestAttr(Attribute::OPEN) || fd->TestAttr(Attribute::DEFAULT)) {
                    fd->EnableAttr(Attribute::IS_BROKEN);
                    continue;
                }
                GetSingleParamFunc(*fd);
                for (auto& list : fd->funcBody->paramLists) {
                    for (auto& param : list->params) {
                        if (!param->desugarDecl || !param->desugarDecl->funcBody) {
                            continue;
                        }
                        auto& fb = *param->desugarDecl->funcBody;
                        if (fb.retType) {
                            SetTypeTy(ctx, *fb.retType);
                        }
                        // Only the canonical ArkTS optional spelling is
                        // supported: a default of None. Build that expression
                        // with the resolved Option type instead of relying on
                        // a late scope lookup.
                        Ptr<Ty> paramTy = param->type ? param->type->GetTy() : nullptr;
                        auto original = DynamicCast<RefExpr*>(param->assignment.get());
                        bool noneDefault = param->assignment && original && original->ref.identifier.Val() == "None" &&
                            Ty::IsTyCorrect(paramTy) && paramTy->IsCoreOptionType() && !paramTy->typeArgs.empty();
                        if (!noneDefault) {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *fd,
                                cd->identifier.Val(), "optional member parameters must default to None");
                            fd->EnableAttr(Attribute::IS_BROKEN);
                            continue;
                        }
                        for (auto& statement : fb.body->body) {
                            auto returned = DynamicCast<ReturnExpr*>(statement.get());
                            if (!returned) {
                                continue;
                            }
                            returned->expr =
                                CreateOptionNoneRef(*ci->importManager, typeManager, paramTy->typeArgs.front());
                            CopyNodeScopeInfo(returned->expr.get(), param->assignment.get());
                            Walker(returned->expr.get(), [&](Ptr<Node> node) {
                                node->curFile = fd->curFile;
                                return VisitAction::WALK_CHILDREN;
                            }).Walk();
                            returned->refFuncBody = &fb;
                            break;
                        }
                        // The default function is created after the symbol
                        // table, so bind its single return expression and
                        // types directly instead of re-entering body checking.
                        for (auto& statement : fb.body->body) {
                            if (auto returned = DynamicCast<ReturnExpr*>(statement.get()); returned) {
                                returned->SetTy(paramTy);
                                if (returned->expr) {
                                    returned->expr->SetTy(paramTy);
                                }
                            }
                        }
                        if (fb.retType) {
                            fb.retType->SetTy(paramTy);
                        }
                        fb.body->SetTy(paramTy);
                        // The desugar function takes every preceding
                        // parameter, so the function type must use that list;
                        // a fixed empty list breaks the second and later
                        // defaulted parameters.
                        std::vector<Ptr<Ty>> paramTys;
                        for (auto& previous : fb.paramLists.front()->params) {
                            // The clone is created by the generic default
                            // pass after the earlier type walk, so its type
                            // node still needs resolving here.
                            if (previous->type) {
                                SetTypeTy(ctx, *previous->type);
                            }
                            if (previous->type && Ty::IsTyCorrect(previous->type->GetTy())) {
                                previous->SetTy(previous->type->GetTy());
                                paramTys.emplace_back(previous->type->GetTy());
                            }
                        }
                        auto funcTy = typeManager.GetFunctionTy(paramTys, paramTy);
                        fb.SetTy(funcTy);
                        param->desugarDecl->SetTy(funcTy);
                        param->desugarDecl->EnableAttr(Attribute::IS_CHECK_VISITED);
                        fb.EnableAttr(Attribute::IS_CHECK_VISITED);
                        if (!fb.body || !Ty::IsTyCorrect(fb.body->GetTy())) {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, *fd,
                                cd->identifier.Val(), "cannot lower the member's default argument");
                            fd->EnableAttr(Attribute::IS_BROKEN);
                        }
                    }
                }
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::RejectArkTSForeignInterfaceExtend(ExtendDecl& ed)
{
    if (ed.TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    auto foreignInterface = FindArkTSForeignInterfaceAncestor(ed);
    if (!foreignInterface) {
        return;
    }
    auto ty = ed.extendedType ? ed.extendedType->GetTy() : nullptr;
    auto extendedDecl = ty ? Ty::GetDeclPtrOfTy(ty) : nullptr;
    // The extended type itself may already have been refused; do not report
    // the same relation twice.
    if (extendedDecl && extendedDecl->TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    auto implName = extendedDecl ? extendedDecl->identifier.Val()
                                 : (ty && Ty::IsTyCorrect(ty) ? Ty::ToString(ty) : std::string("<extend>"));
    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_impl_invalid, ed, implName,
        foreignInterface->identifier.Val());
    ed.EnableAttr(Attribute::IS_BROKEN);
}
