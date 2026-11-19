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
#include "../ArkTSInterop.h"
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

// A foreign parent that already failed reported its own diagnostic; the child
// relation must not restate it. A broken local interface is not a foreign
// parent and keeps the ordinary relation refusal.
bool IsBrokenForeignInterfaceParent(Ptr<Decl> parent)
{
    return parent && parent->TestAttr(Attribute::IS_BROKEN) &&
        (IsArkTSPreparedInterface(*parent) ||
            (parent->TestAttr(Attribute::FOREIGN) && parent->astKind == ASTKind::INTERFACE_DECL));
}

// One return-type refusal, shared by the class and interface signature paths.
// The diagnostic anchors on the written return type when there is one, and on
// the declaration otherwise (a constructor-like spelling).
void DiagnoseUnsupportedReturnType(DiagnosticEngine& diag, FuncDecl& fd, Ptr<Type> retType)
{
    if (retType && !retType->begin.IsZero()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, *retType,
            Ty::ToString(retType->GetTy()), fd.identifier.Val());
    } else {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, fd, "", fd.identifier.Val());
    }
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
        if (IsArkTSPreparedInterface(*current)) {
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
    auto desc = ArkTSClassifyType(type->GetTy(), ci->importManager);
    // Attach the prepared member value table to every enum leaf, so the wrapper
    // generator can spell the value mapping without reaching into the checker.
    // A prepared enum whose declaration was refused (payload, generic, ...) has
    // no table entry and stays non-generatable through the empty member list.
    std::function<ArkTSTypeDesc(Ptr<Type>)> classifyType = [this, &ctx](Ptr<Type> payloadType) {
        return ArkTSClassifyPosition(ctx, payloadType);
    };
    std::function<ArkTSTypeDesc(Ptr<Ty>)> classifyTy = [this, &classifyType, &classifyTy](Ptr<Ty> payloadTy) {
        auto payload = ArkTSClassifyType(payloadTy, ci->importManager);
        Native::FFI::ArkTS::AttachEnumMembers(
            payload, classifyType, classifyTy, ci->importManager);
        return payload;
    };
    Native::FFI::ArkTS::AttachEnumMembers(desc, classifyType, classifyTy, ci->importManager);
    return desc;
}

// Shared state of one prepared ArkTS class or interface lowering step: the
// member-installation helpers below use the owner to rebind `this` and to
// anchor diagnostics, and the remaining fields hold the values substituted
// into the generated bodies.
struct TypeChecker::TypeCheckerImpl::ArkTSDeclLoweringContext {
    Decl& owner;
    ArkTSReceiverKind receiver;
    std::string module;
    std::string className;
    std::vector<std::string> namespaces;
    DiagKindRefactor invalidKind{DiagKindRefactor::sema_arkts_foreign_class_invalid};
};

// Lower every prepared @ArkTS foreign declaration. The pass has seven
// ordered steps; each helper owns one step or one declaration:
// 1. validate the @ArkTS options of the package before any body exists;
// 2. refuse concrete implementers of a prepared foreign interface;
// 3. lower prepared foreign classes;
// 4. propagate broken foreign-interface parents to children;
// 5. lower prepared foreign interfaces;
// 6. propagate again, for children visited before their parent in step 5;
// 7. rebuild the default-argument functions of lowered class members.
// Refusal marks the declaration broken and produces no body, so no member is
// left bodyless without a diagnostic.
void TypeChecker::TypeCheckerImpl::LowerArkTSClasses(ASTContext& ctx)
{
    auto& pkg = *ctx.curPackage;
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    // Step 1. Validate every @ArkTS-carrying declaration before any
    // class/member body is generated: the named-default rule marks an
    // offending bodyless member IS_BROKEN, and the member loops below must
    // observe that mark so a refused member never gets a wrapper body.
    // Running this after class lowering would generate the body first and
    // suppress the diagnostic. LowerArkTSFunctions no longer repeats this
    // call.
    ArkTSDeclOptionsDiagnose(pkg, diag);

    // Step 2. Only the generated implementation class may implement a
    // prepared foreign interface.
    RejectArkTSForeignInterfaceImplementers(pkg);

    // Step 3. Lower prepared @ArkTS foreign classes.
    LowerArkTSClassBodies(ctx, pkg);

    // Step 4. A prepared interface that fails can be the parent of another
    // interface; propagate its broken mark before lowering children.
    PropagateBrokenArkTSInterfaceParents(pkg);

    // Step 5. Lower prepared @ArkTS foreign interfaces.
    LowerArkTSInterfaceBodies(ctx, pkg);

    // Step 6. Propagate again: a child declared earlier or in another file
    // may have been visited before the parent refused in step 5.
    PropagateBrokenArkTSInterfaceParents(pkg);

    // Step 7. Member bodies now exist, so a member with a defaulted parameter
    // is no longer treated as bodyless by the generic default-function pass.
    LowerArkTSClassDefaultArguments(ctx, pkg);
}

// Step 2. Refuse every concrete implementer of a prepared foreign interface:
// the interface dispatches through an ArkTS object, while a Cangjie
// declaration has no shared reference. The generated implementation class,
// a foreign interface itself (abstract, may inherit another prepared
// interface), an interface declaration and an extend declaration are not
// implementers for this rule; extends reach it from PreCheckExtend.
void TypeChecker::TypeCheckerImpl::RejectArkTSForeignInterfaceImplementers(Package& pkg)
{
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            if (decl->TestAttr(Attribute::IS_BROKEN) || IsArkTSInterfaceImpl(*decl) ||
                IsArkTSPreparedInterface(*decl) || decl->astKind == ASTKind::INTERFACE_DECL ||
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
}

// Step 3. Walk every prepared wrapper class, in file and declaration order.
void TypeChecker::TypeCheckerImpl::LowerArkTSClassBodies(ASTContext& ctx, Package& pkg)
{
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto cd = DynamicCast<ClassDecl*>(decl.get());
            if (!cd || !IsArkTSClassWrapper(*cd) || cd->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            LowerArkTSClass(ctx, *cd, *file, pkg);
        }
    }
}

// One prepared wrapper class: parent rules, options, module resolution and
// the member bodies, in that order. Every refusal returns without generating
// the remaining bodies of this class.
void TypeChecker::TypeCheckerImpl::LowerArkTSClass(
    ASTContext& ctx, ClassDecl& cd, File& file, Package& pkg)
{
    if (!CheckArkTSClassParents(cd)) {
        return;
    }
    std::unordered_map<std::string, std::string> options;
    ArkTSOptionStatus optionStatus;
    if (!ArkTSDeclOptions(cd, options, &optionStatus)) {
        DiagnoseArkTSOptionProblem(diag, cd, options, optionStatus);
        cd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    std::string module;
    if (!ResolveArkTSModule(cd, options, pkg, diag, DiagKindRefactor::sema_arkts_foreign_class_invalid, module)) {
        cd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    ArkTSDeclLoweringContext classCtx{cd, ArkTSReceiverFor(options.count("module") != 0, module), module,
        options.count("foreignName") ? options.at("foreignName") : cd.identifier.Val(),
        ArkTSNamespaceSegments(options.count("namespace") ? options.at("namespace") : std::string())};
    LowerArkTSClassFuncMembers(ctx, classCtx, cd, file);
    LowerArkTSClassPropMembers(ctx, classCtx, cd, file);
}

// Parent rules of one wrapper class. A wrapper parent is the R-D3 supported
// shape: the child keeps only the ArkTS object its own constructor builds and
// forwards it to the root wrapper's storage through super(...), so a wrapper
// parent is not refused here. The authority design requires every parent to
// carry @ArkTS; an unannotated parent would make the wrapper reference a class
// the ArkTS side does not have. Cangjie's own inheritance checks (the parent
// must be open, first-position superclass and the like) run in the generic
// class checking.
bool TypeChecker::TypeCheckerImpl::CheckArkTSClassParents(ClassDecl& cd)
{
    auto parents = UserParentDecls(cd);
    for (auto& parent : parents) {
        if (!parent->HasAnno(AnnotationKind::ARKTS) && !parent->TestAttr(Attribute::ARKTS)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
                "parent type '" + parent->identifier.Val() + "' must be an @ArkTS class or interface");
            cd.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
    }
    // A child wrapper owns no reference field: every constructor body
    // generated for it reaches the root wrapper's storage through super(...).
    // Preparation guarantees such a class a wrapper parent; a class whose
    // parents resolved here to no wrapper at all means the preparation
    // spelling match was wrong, and the super(...) delegations would resolve
    // against a parent constructor that does not exist. Refuse the class
    // instead of generating that body - preparation's own refusals above (a
    // broken parent breaks the child there) make this unreachable for every
    // shape preparation can still judge.
    if (!OwnsReferenceField(cd) &&
        std::none_of(parents.begin(), parents.end(), [](Ptr<Decl> parent) { return IsArkTSClassWrapper(*parent); })) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, cd, cd.identifier.Val(),
            "a wrapper class without its own reference field must have an @ArkTS wrapper parent");
        cd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    return true;
}

// Function members of one wrapper class, in declaration order.
void TypeChecker::TypeCheckerImpl::LowerArkTSClassFuncMembers(
    ASTContext& ctx, ArkTSDeclLoweringContext& classCtx, ClassDecl& cd, File& file)
{
    for (auto& member : cd.GetMemberDecls()) {
        auto fd = DynamicCast<FuncDecl*>(member.get());
        // A parsed member always carries a FuncBody. A bodyless FuncDecl can
        // only come from a malformed AST, where there is no user spelling to
        // anchor a diagnostic on; skip it like the function path instead of
        // dereferencing a null FuncBody.
        if (!fd || !fd->funcBody) {
            continue;
        }
        LowerArkTSClassFunc(ctx, classCtx, cd, file, *fd);
    }
}

// One function member of a wrapper class. The generated constructor or
// method body is built from the classified signature; every refusal marks
// only the member, so the class may still lower its remaining members.
void TypeChecker::TypeCheckerImpl::LowerArkTSClassFunc(
    ASTContext& ctx, ArkTSDeclLoweringContext& classCtx, ClassDecl& cd, File& file, FuncDecl& fd)
{
    if (IsWrappingConstructor(fd) || IsReferenceAccessor(fd)) {
        return;
    }
    // A member the option fold already refused (for example a named parameter
    // without a default) is broken and must not receive a generated body.
    if (fd.TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    // An operator member ('operator func []', 'operator func +', ...) has no
    // method-name spelling: the member wrapper would call the operator's
    // source token as a method (for example callMethod("[]")), which is not
    // the ArkTS operator contract. Refuse the member before any signature
    // classification.
    if (fd.op != TokenKind::ILLEGAL) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, fd, cd.identifier.Val(),
            "operator members on foreign class are not supported");
        fd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    bool constructor = fd.TestAttr(Attribute::CONSTRUCTOR);
    std::vector<ArkTSTypeDesc> types;
    std::vector<Ptr<FuncParam>> params;
    ArkTSTypeDesc resultType;
    if (!AnalyzeArkTSClassFuncSignature(ctx, fd, types, params, resultType)) {
        fd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    // The rest scan covers every flattened position of a class member (the
    // function path scans its first list only); the position must be the
    // trailing Array<T'>, its element already validated by the parameter
    // analysis above.
    size_t restIndex = ArkTSRestPosition(params, params.size());
    if (restIndex != ARKTS_NO_REST &&
        (restIndex + 1 != types.size() || types[restIndex].kind != ArkTSTypeDesc::Kind::ARRAY_COPY)) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_arkts_foreign_class_invalid, fd, cd.identifier.Val(), "unsupported rest parameter");
        fd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    // ArkTS optional parameters are the trailing named parameters that carry
    // a default value (`x!: Option<T> = None`), the same written-spelling gate
    // the function path uses: only such a parameter may be omitted by the
    // caller, and a positional Option (the required nullable argument of the
    // with-rest/open form) is still always forwarded.
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
        // A child wrapper (no field of its own) moves the whole construction
        // into the super(...) thunk; a root wrapper keeps the direct field
        // assignment.
        auto form = OwnsReferenceField(cd) ? ArkTSConstructorForm::DIRECT : ArkTSConstructorForm::SUPER_THUNK;
        code = ArkTSClassConstructorSource(
            types, classCtx.namespaces.size(), prefix, restIndex, classCtx.receiver, optionalStart, classRefs, form);
    } else {
        code = ArkTSClassMethodSource(types, resultType, classCtx.namespaces.size(), prefix, restIndex,
            fd.TestAttr(Attribute::STATIC) ? ArkTSMemberReceiver::STATIC : ArkTSMemberReceiver::INSTANCE,
            classCtx.receiver, optionalStart, classRefs);
    }
    if (code.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, fd, cd.identifier.Val(),
            "unsupported member signature");
        fd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    InstallArkTSMemberBody(ctx, classCtx, fd, file, code, params, GetArkTSMemberName(fd), classRefs);
}

// Classify the signature of one class member. The return type is classified
// before the parameters but its diagnostic is emitted after theirs, matching
// the order a reader of the source sees; a constructor skips the return-type
// check. Returns false when any position is unsupported; the caller marks the
// member broken.
bool TypeChecker::TypeCheckerImpl::AnalyzeArkTSClassFuncSignature(ASTContext& ctx, FuncDecl& fd,
    std::vector<ArkTSTypeDesc>& types, std::vector<Ptr<FuncParam>>& params, ArkTSTypeDesc& resultType)
{
    auto& fb = *fd.funcBody;
    bool constructor = fd.TestAttr(Attribute::CONSTRUCTOR);
    resultType = ArkTSClassifyPosition(ctx, fb.retType.get());
    bool supported = ClassifyArkTSFuncParameters(ctx, fd, types, params);
    if (!constructor && !ArkTSTypeDescGeneratable(resultType)) {
        DiagnoseUnsupportedReturnType(diag, fd, fb.retType.get());
        supported = false;
    }
    return supported;
}

// Property members of one wrapper class, in declaration order.
void TypeChecker::TypeCheckerImpl::LowerArkTSClassPropMembers(
    ASTContext& ctx, ArkTSDeclLoweringContext& classCtx, ClassDecl& cd, File& file)
{
    for (auto& member : cd.GetMemberDecls()) {
        auto pd = DynamicCast<PropDecl*>(member.get());
        // A broken property (or one prepared for generated accessors) gets no
        // body; the class may still lower its other members.
        if (!pd || pd->TestAttr(Attribute::COMPILER_ADD) || pd->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        LowerArkTSClassProp(ctx, classCtx, file, *pd);
    }
}

// One property of a wrapper class. A @WriteOnly property keeps its (private)
// getter body: the accessor remains part of the lowered property contract and
// a non-abstract member must not stay bodyless. Reads are rejected by the
// write-only reference rule, not by dropping the body.
void TypeChecker::TypeCheckerImpl::LowerArkTSClassProp(
    ASTContext& ctx, ArkTSDeclLoweringContext& classCtx, File& file, PropDecl& pd)
{
    ArkTSTypeDesc desc;
    if (!ClassifyArkTSPropertyType(ctx, pd, desc)) {
        return;
    }
    auto receiver = pd.TestAttr(Attribute::STATIC) ? ArkTSMemberReceiver::STATIC : ArkTSMemberReceiver::INSTANCE;
    std::string name = GetArkTSMemberName(pd);
    for (auto& getter : pd.getters) {
        if (!getter->funcBody) {
            continue;
        }
        std::unordered_map<std::string, Ptr<Decl>> classRefs;
        auto code = ArkTSClassPropertySource(desc, false, receiver, classCtx.namespaces.size(),
            ArkTSForeignLocalPrefix({}), classCtx.receiver, classRefs);
        if (code.empty()) {
            getter->EnableAttr(Attribute::IS_BROKEN);
            continue;
        }
        InstallArkTSMemberBody(ctx, classCtx, *getter, file, code, {}, name, classRefs);
    }
    for (auto& setter : pd.setters) {
        if (!setter->funcBody || setter->funcBody->paramLists.empty() ||
            setter->funcBody->paramLists.front()->params.empty()) {
            continue;
        }
        std::unordered_map<std::string, Ptr<Decl>> classRefs;
        auto code = ArkTSClassPropertySource(desc, true, receiver, classCtx.namespaces.size(),
            ArkTSForeignLocalPrefix({}), classCtx.receiver, classRefs);
        if (code.empty()) {
            setter->EnableAttr(Attribute::IS_BROKEN);
            continue;
        }
        InstallArkTSMemberBody(ctx, classCtx, *setter, file, code,
            {setter->funcBody->paramLists.front()->params.front().get()}, name, classRefs);
    }
}

// Steps 4 and 6. A prepared interface that fails (preparation or lowering)
// can be the parent of another interface. Propagate the broken mark to a
// fixpoint, so a child declared earlier or in another file does not escape
// the check when its parent fails later in the same pass, and mark each
// child's generated implementation class broken too.
void TypeChecker::TypeCheckerImpl::PropagateBrokenArkTSInterfaceParents(Package& pkg)
{
    for (bool changed = true; changed;) {
        changed = false;
        for (auto& file : pkg.files) {
            for (auto& decl : file->decls) {
                auto id = DynamicCast<InterfaceDecl*>(decl.get());
                if (!id || !IsArkTSPreparedInterface(*id) || id->TestAttr(Attribute::IS_BROKEN)) {
                    continue;
                }
                for (auto& inherited : id->inheritedTypes) {
                    auto parent = ResolveArkTSAliasTarget(Ty::GetDeclPtrOfTy(inherited->GetTy()));
                    if (!IsBrokenForeignInterfaceParent(parent)) {
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
}

// Step 5. Walk every prepared foreign interface, in file and declaration
// order.
void TypeChecker::TypeCheckerImpl::LowerArkTSInterfaceBodies(ASTContext& ctx, Package& pkg)
{
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto id = DynamicCast<InterfaceDecl*>(decl.get());
            if (!id || !IsArkTSPreparedInterface(*id) || id->TestAttr(Attribute::IS_BROKEN)) {
                continue;
            }
            LowerArkTSInterface(ctx, *id, *file, pkg);
        }
    }
}

// One prepared foreign interface: options, module, parents, member bodies and
// the generated implementation class, in that order. Every member becomes a
// default implementation over this.ref(); the implementation class is a plain
// class with an already complete body and needs no further lowering.
void TypeChecker::TypeCheckerImpl::LowerArkTSInterface(
    ASTContext& ctx, InterfaceDecl& id, File& file, Package& pkg)
{
    std::unordered_map<std::string, std::string> options;
    ArkTSOptionStatus optionStatus;
    if (!ArkTSDeclOptions(id, options, &optionStatus)) {
        DiagnoseArkTSOptionProblem(diag, id, options, optionStatus);
        id.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    std::string module;
    if (!ResolveArkTSModule(id, options, pkg, diag, DiagKindRefactor::sema_arkts_foreign_interface_invalid, module)) {
        id.EnableAttr(Attribute::IS_BROKEN);
        if (auto implDecl = ArkTSInterfaceImpl(id, ci->importManager)) {
            MarkBrokenInterfaceImpl(*implDecl);
        }
        return;
    }
    ArkTSDeclLoweringContext declCtx{id, ArkTSReceiverFor(options.count("module") != 0, module), module,
        options.count("foreignName") ? options.at("foreignName") : id.identifier.Val(),
        ArkTSNamespaceSegments(options.count("namespace") ? options.at("namespace") : std::string()),
        DiagKindRefactor::sema_arkts_foreign_interface_invalid};
    if (!CheckArkTSInterfaceParents(id)) {
        // The implementation class exists before this refusal, so it must be
        // marked broken here as well; otherwise later inheritance/initialization
        // checks report it as a second, unrelated diagnostic.
        if (auto implDecl = ArkTSInterfaceImpl(id, ci->importManager)) {
            MarkBrokenInterfaceImpl(*implDecl);
        }
        return;
    }
    LowerArkTSInterfaceFuncMembers(ctx, declCtx, id, file);
    LowerArkTSInterfacePropMembers(ctx, declCtx, id, file);
    if (id.TestAttr(Attribute::IS_BROKEN)) {
        if (auto implDecl = ArkTSInterfaceImpl(id, ci->importManager)) {
            MarkBrokenInterfaceImpl(*implDecl);
        }
    }
}

// Parent rules of one prepared interface. Only another prepared foreign
// interface may be a parent: its members already dispatch through the same
// accessor shape. A foreign parent that already failed reported its own
// diagnostic, so the relation is not restated as a second error.
bool TypeChecker::TypeCheckerImpl::CheckArkTSInterfaceParents(InterfaceDecl& id)
{
    for (auto& inherited : id.inheritedTypes) {
        auto parent = ResolveArkTSAliasTarget(Ty::GetDeclPtrOfTy(inherited->GetTy()));
        if (IsBrokenForeignInterfaceParent(parent)) {
            id.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
        if (!parent || !IsArkTSPreparedInterface(*parent)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, id, id.identifier.Val(),
                "a foreign interface may only inherit another foreign interface");
            id.EnableAttr(Attribute::IS_BROKEN);
            return false;
        }
    }
    return true;
}

// Function members of one prepared interface, in declaration order.
void TypeChecker::TypeCheckerImpl::LowerArkTSInterfaceFuncMembers(
    ASTContext& ctx, ArkTSDeclLoweringContext& declCtx, InterfaceDecl& id, File& file)
{
    for (auto& member : id.GetMemberDecls()) {
        auto fd = DynamicCast<FuncDecl*>(member.get());
        if (!fd || !fd->funcBody || IsReferenceAccessor(*fd)) {
            continue;
        }
        LowerArkTSInterfaceFunc(ctx, declCtx, id, file, *fd);
    }
}

// One function member of a prepared interface: a call signature, an index
// signature, or an ordinary method, dispatched through this.ref(). A refusal
// marks the member and the interface; the remaining members are still
// lowered.
void TypeChecker::TypeCheckerImpl::LowerArkTSInterfaceFunc(
    ASTContext& ctx, ArkTSDeclLoweringContext& declCtx, InterfaceDecl& id, File& file, FuncDecl& fd)
{
    const bool indexOperator = fd.TestAttr(Attribute::OPERATOR) && fd.identifier.Val() == "[]";
    const bool callOperator = fd.TestAttr(Attribute::OPERATOR) && fd.identifier.Val() == "()";
    std::vector<ArkTSTypeDesc> types;
    std::vector<Ptr<FuncParam>> params;
    ArkTSTypeDesc resultType;
    size_t restIndex = ARKTS_NO_REST;
    if (!AnalyzeArkTSInterfaceSignature(ctx, fd, id.identifier.Val(),
            DiagKindRefactor::sema_arkts_foreign_interface_invalid,
            "optional parameters on foreign interface members are not supported", types, params, resultType,
            restIndex)) {
        id.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    const auto prefix = ArkTSForeignLocalPrefix(params);
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    std::string code;
    if (indexOperator) {
        if (restIndex != ARKTS_NO_REST) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, fd, id.identifier.Val(),
                "an index signature must not use a rest parameter");
            fd.EnableAttr(Attribute::IS_BROKEN);
            id.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
        const ArkTSTypeDesc* valueType = nullptr;
        bool setter = false;
        if (types.size() == 1) {
            if (resultType.kind != ArkTSTypeDesc::Kind::OPTIONAL || resultType.elements.size() != 1) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, fd, id.identifier.Val(),
                    "an index getter must return Option<T>");
                fd.EnableAttr(Attribute::IS_BROKEN);
                id.EnableAttr(Attribute::IS_BROKEN);
                return;
            }
            valueType = &resultType.elements.front();
        } else if (types.size() == 2) {
            if (!params[1]->isNamedParam) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, fd, id.identifier.Val(),
                    "an index setter's value parameter must be named");
                fd.EnableAttr(Attribute::IS_BROKEN);
                id.EnableAttr(Attribute::IS_BROKEN);
                return;
            }
            if (resultType.kind != ArkTSTypeDesc::Kind::SCALAR || resultType.scalar != "Unit") {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, fd, id.identifier.Val(),
                    "an index setter must return Unit");
                fd.EnableAttr(Attribute::IS_BROKEN);
                id.EnableAttr(Attribute::IS_BROKEN);
                return;
            }
            valueType = &types[1];
            setter = true;
        } else {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, fd, id.identifier.Val(),
                "an index signature takes one key or a key and a named value");
            fd.EnableAttr(Attribute::IS_BROKEN);
            id.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
        code = ArkTSInterfaceIndexSource(types[0], *valueType, setter, prefix, classRefs);
    } else if (callOperator) {
        if (restIndex != ARKTS_NO_REST) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, fd, id.identifier.Val(),
                "a call signature must not use a rest parameter");
            fd.EnableAttr(Attribute::IS_BROKEN);
            id.EnableAttr(Attribute::IS_BROKEN);
            return;
        }
        code = ArkTSInterfaceCallSource(types, resultType, prefix, classRefs);
    } else {
        // Interface members reject optional parameters, so the trailing
        // optional run is empty.
        code = ArkTSClassMethodSource(types, resultType, 0, prefix, restIndex, ArkTSMemberReceiver::INSTANCE,
            declCtx.receiver, params.size(), classRefs, "interface");
    }
    if (code.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, fd, id.identifier.Val(),
            "unsupported member signature");
        fd.EnableAttr(Attribute::IS_BROKEN);
        id.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    InstallArkTSMemberBody(ctx, declCtx, fd, file, code, params, GetArkTSMemberName(fd), classRefs);
    if (fd.funcBody->body && !fd.TestAttr(Attribute::IS_BROKEN)) {
        fd.DisableAttr(Attribute::ABSTRACT);
        fd.EnableAttr(Attribute::DEFAULT, Attribute::INTERFACE_IMPL);
    }
}

// Classify the signature of one interface member. Optional parameters are
// refused before any position is classified; the rest position is written
// only on success. Returns false when the member is refused; the caller marks
// the interface broken.
bool TypeChecker::TypeCheckerImpl::AnalyzeArkTSInterfaceSignature(ASTContext& ctx, FuncDecl& fd,
    const std::string& ownerName, DiagKindRefactor invalidKind, const char* optionalReason,
    std::vector<ArkTSTypeDesc>& types, std::vector<Ptr<FuncParam>>& params, ArkTSTypeDesc& resultType,
    size_t& restIndex)
{
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
    bool supported = ClassifyArkTSFuncParameters(ctx, fd, types, params);
    resultType = ArkTSClassifyPosition(ctx, fb.retType.get());
    if (!ArkTSTypeDescGeneratable(resultType)) {
        DiagnoseUnsupportedReturnType(diag, fd, fb.retType.get());
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
}

// Property members of one prepared interface, in declaration order. This
// path deliberately has no COMPILER_ADD filter: the generated accessor pair
// is part of the property contract processed below.
void TypeChecker::TypeCheckerImpl::LowerArkTSInterfacePropMembers(
    ASTContext& ctx, ArkTSDeclLoweringContext& declCtx, InterfaceDecl& id, File& file)
{
    for (auto& member : id.GetMemberDecls()) {
        auto pd = DynamicCast<PropDecl*>(member.get());
        if (!pd) {
            continue;
        }
        LowerArkTSInterfaceProp(ctx, declCtx, id, file, *pd);
    }
}

// One property of a prepared interface. A failed accessor marks the accessor,
// the property and the interface; the property is marked DEFAULT only when
// every accessor lowered.
void TypeChecker::TypeCheckerImpl::LowerArkTSInterfaceProp(
    ASTContext& ctx, ArkTSDeclLoweringContext& declCtx, InterfaceDecl& id, File& file, PropDecl& pd)
{
    ArkTSTypeDesc desc;
    if (!ClassifyArkTSPropertyType(ctx, pd, desc)) {
        id.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    std::string name = GetArkTSMemberName(pd);
    for (auto& getter : pd.getters) {
        if (!getter->funcBody) {
            continue;
        }
        std::unordered_map<std::string, Ptr<Decl>> classRefs;
        auto code = ArkTSClassPropertySource(desc, false, ArkTSMemberReceiver::INSTANCE, 0, ArkTSForeignLocalPrefix({}),
            declCtx.receiver, classRefs);
        if (code.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, pd, id.identifier.Val(),
                "unsupported property signature");
            getter->EnableAttr(Attribute::IS_BROKEN);
            pd.EnableAttr(Attribute::IS_BROKEN);
            id.EnableAttr(Attribute::IS_BROKEN);
            continue;
        }
        InstallArkTSMemberBody(ctx, declCtx, *getter, file, code, {}, name, classRefs);
        if (getter->TestAttr(Attribute::IS_BROKEN)) {
            pd.EnableAttr(Attribute::IS_BROKEN);
        }
        if (getter->funcBody->body && !getter->TestAttr(Attribute::IS_BROKEN)) {
            getter->DisableAttr(Attribute::ABSTRACT);
            getter->EnableAttr(Attribute::DEFAULT, Attribute::INTERFACE_IMPL);
        }
    }
    for (auto& setter : pd.setters) {
        if (!setter->funcBody || setter->funcBody->paramLists.empty() ||
            setter->funcBody->paramLists.front()->params.empty()) {
            continue;
        }
        std::unordered_map<std::string, Ptr<Decl>> classRefs;
        auto code = ArkTSClassPropertySource(desc, true, ArkTSMemberReceiver::INSTANCE, 0, ArkTSForeignLocalPrefix({}),
            declCtx.receiver, classRefs);
        if (code.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_interface_invalid, pd, id.identifier.Val(),
                "unsupported property signature");
            setter->EnableAttr(Attribute::IS_BROKEN);
            pd.EnableAttr(Attribute::IS_BROKEN);
            id.EnableAttr(Attribute::IS_BROKEN);
            continue;
        }
        InstallArkTSMemberBody(ctx, declCtx, *setter, file, code,
            {setter->funcBody->paramLists.front()->params.front().get()}, name, classRefs);
        if (setter->TestAttr(Attribute::IS_BROKEN)) {
            pd.EnableAttr(Attribute::IS_BROKEN);
        }
        if (setter->funcBody->body && !setter->TestAttr(Attribute::IS_BROKEN)) {
            setter->DisableAttr(Attribute::ABSTRACT);
            setter->EnableAttr(Attribute::DEFAULT, Attribute::INTERFACE_IMPL);
        }
    }
    if (!pd.TestAttr(Attribute::IS_BROKEN)) {
        pd.DisableAttr(Attribute::ABSTRACT);
        pd.EnableAttr(Attribute::DEFAULT, Attribute::INTERFACE_IMPL);
    }
}

// Step 7. Build and type-check the default-argument functions of every
// lowered class member against the final parameter types. A broken class is a
// class-level refusal (its members were never lowered and its default
// functions, if any, were built by the generic pass), so the post-pass leaves
// it alone. Member-level refusals no longer mark the class: a broken member
// is skipped individually, so one refused member never suppresses the
// default functions of its siblings.
void TypeChecker::TypeCheckerImpl::LowerArkTSClassDefaultArguments(ASTContext& ctx, Package& pkg)
{
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
                LowerArkTSClassMemberDefaultArguments(ctx, *cd, *fd);
            }
        }
    }
}

// Default-argument functions of one lowered class member.
void TypeChecker::TypeCheckerImpl::LowerArkTSClassMemberDefaultArguments(
    ASTContext& ctx, ClassDecl& cd, FuncDecl& fd)
{
    bool hasDefault = false;
    for (auto& list : fd.funcBody->paramLists) {
        for (auto& param : list->params) {
            hasDefault = hasDefault || param->assignment != nullptr;
        }
    }
    if (!hasDefault) {
        return;
    }
    // A mapping-only spelling can leave HAS_INITIAL set without a desugar
    // declaration; clear it so the pass rebuilds one.
    for (auto& list : fd.funcBody->paramLists) {
        for (auto& param : list->params) {
            if (param->assignment && !param->desugarDecl && param->TestAttr(Attribute::HAS_INITIAL)) {
                param->DisableAttr(Attribute::HAS_INITIAL);
            }
        }
    }
    // The default-argument function clones the preceding parameters and
    // copies their own Ty, so the manual pass must publish the resolved
    // parameter types first. Otherwise the second and later defaults carry an
    // UnknownType parameter.
    for (auto& list : fd.funcBody->paramLists) {
        for (auto& param : list->params) {
            if (param->type && Ty::IsTyCorrect(param->type->GetTy())) {
                param->SetTy(param->type->GetTy());
            }
        }
    }
    // The shared default-parameter pass rejects optional parameters on
    // open/default members. Do not rebuild the mapping-only default function
    // here, otherwise the same rule is reported twice for one parameter list.
    if (fd.TestAttr(Attribute::OPEN) || fd.TestAttr(Attribute::DEFAULT)) {
        fd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    GetSingleParamFunc(fd);
    for (auto& list : fd.funcBody->paramLists) {
        for (auto& param : list->params) {
            if (!param->desugarDecl || !param->desugarDecl->funcBody) {
                continue;
            }
            auto& fb = *param->desugarDecl->funcBody;
            if (fb.retType) {
                SetTypeTy(ctx, *fb.retType);
            }
            // Only the canonical ArkTS optional spelling is supported: a
            // default of None. Build that expression with the resolved Option
            // type instead of relying on a late scope lookup.
            Ptr<Ty> paramTy = param->type ? param->type->GetTy() : nullptr;
            auto original = DynamicCast<RefExpr*>(param->assignment.get());
            bool noneDefault = param->assignment && original && original->ref.identifier.Val() == "None" &&
                Ty::IsTyCorrect(paramTy) && paramTy->IsCoreOptionType() && !paramTy->typeArgs.empty();
            if (!noneDefault) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, fd, cd.identifier.Val(),
                    "optional member parameters must default to None");
                fd.EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            for (auto& statement : fb.body->body) {
                auto returned = DynamicCast<ReturnExpr*>(statement.get());
                if (!returned) {
                    continue;
                }
                returned->expr = CreateOptionNoneRef(*ci->importManager, typeManager, paramTy->typeArgs.front());
                CopyNodeScopeInfo(returned->expr.get(), param->assignment.get());
                Walker(returned->expr.get(), [&](Ptr<Node> node) {
                    node->curFile = fd.curFile;
                    return VisitAction::WALK_CHILDREN;
                }).Walk();
                returned->refFuncBody = &fb;
                break;
            }
            // The default function is created after the symbol table, so bind
            // its single return expression and types directly instead of
            // re-entering body checking.
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
            // The desugar function takes every preceding parameter, so the
            // function type must use that list; a fixed empty list breaks the
            // second and later defaulted parameters.
            std::vector<Ptr<Ty>> paramTys;
            for (auto& previous : fb.paramLists.front()->params) {
                // The clone is created by the generic default pass after the
                // earlier type walk, so its type node still needs resolving
                // here.
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
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_class_invalid, fd, cd.identifier.Val(),
                    "cannot lower the member's default argument");
                fd.EnableAttr(Attribute::IS_BROKEN);
            }
        }
    }
}

// Install one generated member body and rebind its placeholders. `member`
// owns the body; `params` are the references the argument placeholders bind
// to.
void TypeChecker::TypeCheckerImpl::InstallArkTSMemberBody(ASTContext& ctx, ArkTSDeclLoweringContext& lowering,
    FuncDecl& member, File& file, const std::string& code, const std::vector<Ptr<FuncParam>>& params,
    const std::string& memberName, const std::unordered_map<std::string, Ptr<Decl>>& classRefs)
{
    auto& fb = *member.funcBody;
    // A repeated pass (LSP/incremental AST reuse) may see the body this
    // lowering already installed. Preparation rejects user-written bodies, so
    // an existing body is ours; keep it instead of overwriting or failing.
    // LowerArkTSFunctions skips already-bodied wrappers the same way.
    if (fb.body) {
        return;
    }
    ASTFactory factory(diag, ci->GetSourceManager());
    fb.body = factory.CreateForeignBody(member, code);
    if (!fb.body) {
        diag.DiagnoseRefactor(lowering.invalidKind, member, lowering.owner.identifier.Val(),
            "cannot generate the wrapper body");
        // A generated-body failure belongs to this member only; the other
        // class members still lower. An interface default implementation owns
        // the member contract, so the interface itself is broken too.
        member.EnableAttr(Attribute::IS_BROKEN);
        if (lowering.invalidKind == DiagKindRefactor::sema_arkts_foreign_interface_invalid) {
            lowering.owner.EnableAttr(Attribute::IS_BROKEN);
        }
        return;
    }
    std::unordered_map<std::string, std::string> literals{{ARKTS_PLACEHOLDER_MODULE, lowering.module},
        {ARKTS_PLACEHOLDER_CLASS, lowering.className}, {ARKTS_PLACEHOLDER_MEMBER, memberName}};
    for (size_t i = 0; i < lowering.namespaces.size(); ++i) {
        literals[std::string(ARKTS_PLACEHOLDER_NAMESPACE) + std::to_string(i) + "__"] = lowering.namespaces[i];
    }
    // Type placeholders are merged into the long-lived support map only after
    // the body passes BindArkTSSupportReferences.
    std::unordered_map<Ptr<const Node>, Ptr<Decl>> pendingRefs;
    Walker(fb.body.get(), [&](Ptr<Node> node) {
        node->curFile = &file;
        node->begin = member.begin;
        node->end = member.end;
        if (auto literal = DynamicCast<LitConstExpr*>(node); literal && literals.count(literal->stringValue)) {
            literal->stringValue = literals.at(literal->stringValue);
        }
        // Type placeholders carry a reference element in generated annotations
        // (Array<T>, Option<T>): bind the reference type and register it like a
        // support declaration, so name resolution and speculative re-checks
        // keep the class.
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
                ref->ref.target = &lowering.owner;
                ref->SetTy(lowering.owner.GetTy());
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
    // diagnostic names the user-written declaration instead: the class for a
    // constructor, the property for an accessor, the member name otherwise.
    std::string diagnosticName = memberName;
    if (member.TestAttr(Attribute::CONSTRUCTOR)) {
        diagnosticName = lowering.owner.identifier.Val();
    } else if (member.propDecl != nullptr) {
        // An accessor always has a property name in practice; keep the member
        // name rather than overwriting it with an empty string.
        const auto propName = member.propDecl->identifier.Val();
        if (!propName.empty()) {
            diagnosticName = propName;
        }
    }
    if (!BindArkTSSupportReferences(member, *fb.body, *ci->importManager, diag, pendingRefs, diagnosticName)) {
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
}

// Classify every flattened parameter position of one foreign signature.
// Positions after the first failure are still classified and appended, so the
// caller keeps the same arity and one diagnostic is emitted per bad position.
// Returns false when at least one position is unsupported; the caller marks
// the member broken.
bool TypeChecker::TypeCheckerImpl::ClassifyArkTSFuncParameters(ASTContext& ctx, FuncDecl& fd,
    std::vector<ArkTSTypeDesc>& types, std::vector<Ptr<FuncParam>>& params)
{
    bool supported = true;
    for (auto& list : fd.funcBody->paramLists) {
        for (auto& param : list->params) {
            auto desc = ArkTSClassifyPosition(ctx, param->type.get());
            if (!ArkTSTypeDescGeneratable(desc) || ArkTSTypeDescContainsNothing(desc)) {
                auto& position = param->type ? static_cast<Node&>(*param->type) : static_cast<Node&>(*param);
                auto deferredName = param->type ? ArkTSDeferredExternalTypeName(param->type->GetTy()) : std::string{};
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
    return supported;
}

// Classify one property type, shared by the class and interface paths. The
// three-way diagnostic keeps the function path's spelling: a deferred
// external type wins, a writable property reports the parameter shape, and a
// read-only one reports the return shape. Returns false and marks the property
// broken when the type is not generatable.
bool TypeChecker::TypeCheckerImpl::ClassifyArkTSPropertyType(ASTContext& ctx, PropDecl& pd, ArkTSTypeDesc& desc)
{
    desc = ArkTSClassifyPosition(ctx, pd.type.get());
    if (ArkTSTypeDescGeneratable(desc) && !ArkTSTypeDescContainsNothing(desc)) {
        return true;
    }
    if (pd.type) {
        auto deferredName = ArkTSDeferredExternalTypeName(pd.type->GetTy());
        if (!deferredName.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_external_type_deferred, *pd.type, deferredName);
        } else if (pd.isVar) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_param_type_unsupported, *pd.type,
                pd.identifier.Val(), pd.identifier.Val(), Ty::ToString(pd.type->GetTy()));
        } else {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, *pd.type,
                Ty::ToString(pd.type->GetTy()), pd.identifier.Val());
        }
    }
    pd.EnableAttr(Attribute::IS_BROKEN);
    return false;
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
