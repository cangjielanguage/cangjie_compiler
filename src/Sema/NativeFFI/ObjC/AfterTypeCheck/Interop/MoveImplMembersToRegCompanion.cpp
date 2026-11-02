// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements moving instance fields, static members and the finalizer from @ObjCImpl declarations
 * to their corresponding registry companion classes.
 */

#include "Context.h"
#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/ASTQuery.h"
#include "cangjie/AST/Utils.h"
#include <unordered_map>
#include <vector>

namespace Cangjie::Interop::ObjC {

using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

namespace {

/**
 * A moved member and the proxy that takes its place in the @ObjCImpl.
 *
 * Both kinds of proxy share one lifecycle: the proxy is bound to the impl as soon as it is created, while its
 * insertion is postponed until every reference in the package has been re-resolved to it. Inserting earlier
 * would let the rewrite below re-resolve the proxy's own body to itself.
 *
 * A field is proxied by a `PropDecl` forwarding to the registry companion, a static function by a `FuncDecl`
 * of the same signature forwarding there. The finalizer, which the user cannot name, moves without a proxy.
 */
class ProxiedMembers {
public:
    void Add(Decl& origin, OwnedPtr<Decl> proxy) noexcept
    {
        byOrigin[&origin] = proxy.get();
        proxies.push_back(std::move(proxy));
    }

    Ptr<Decl> Find(Ptr<const Decl> origin) const noexcept
    {
        auto found = byOrigin.find(origin);
        return found == byOrigin.end() ? nullptr : found->second;
    }

    /// The proxies in the order their originals were declared.
    std::vector<OwnedPtr<Decl>>& InDeclarationOrder() noexcept
    {
        return proxies;
    }

private:
    std::vector<OwnedPtr<Decl>> proxies;
    std::unordered_map<Ptr<const Decl>, Ptr<Decl>> byOrigin;
};

/// The two classes a member travels between, plus the impl's `$reg` field that instance proxies read through.
struct MoveTarget {
    ClassDecl& impl;
    ClassDecl& regCompanion;
    VarDecl& regCompField;
};

/**
 * The registry companions mirror the impls' inheritance chain, and a moved member is reached from the
 * companions of the impl's subclasses, so it can no longer be private to its original owner.
 */
void OpenUpForRegCompanions(Decl& moved)
{
    moved.DisableAttr(Attribute::PRIVATE, Attribute::INTERNAL, Attribute::PUBLIC);
    moved.EnableAttr(Attribute::PROTECTED);
}

bool RewireMovedField(VarDecl& vd, const MoveTarget& target, InteropContext& ctx, ProxiedMembers& proxied)
{
    if (IsObjCExportRegistryCompanionField(vd)) {
        return false;
    }

    // Access to private static const is a simple load/store in CHIR, so we can leave it as it is
    if (vd.TestAttr(Attribute::STATIC) && vd.isConst) {
        return false;
    }

    auto proxyProp = CreatePropDecl();
    ctx.astInserter.BindTo(target.impl, *proxyProp);

    if (vd.TestAttr(Attribute::STATIC)) {
        // Static state follows the finalizer and the static functions into the registry companion, so that
        // the impl is left holding nothing but proxies. A static field always carries its own initializer -
        // the parser rejects a static initializer in an @ObjCImpl - so it moves without losing it.
        ctx.astTransformer.TransformToObjCImplProxyProp(*proxyProp, vd, target.regCompanion);
    } else {
        // The proxy inherits the visibility and the mutability the user declared. It is built before the
        // field is turned into a `var` below, so that a `let` field keeps a get-only prop and stays
        // read-only on the Objective-C side.
        ctx.astTransformer.TransformToObjCImplProxyProp(*proxyProp, vd, target.regCompField);

        // The moved field itself is always a `var` carrying a value of its own from the start: the registry
        // companion is built by a generated constructor that knows nothing about the user's fields, and the
        // user constructor stays behind in the impl, from where it writes straight into `$reg.<field>` -
        // past the get-only prop above.
        vd.isVar = true;
        if (!vd.initializer) {
            vd.initializer = CreateZeroValue(ctx.importManager, ctx.typeManager, *vd.GetTy());
        }
    }

    OpenUpForRegCompanions(vd);
    proxied.Add(vd, std::move(proxyProp));

    return true;
}

bool RewireMovedFunc(FuncDecl& fd, const MoveTarget& target, InteropContext& ctx, ProxiedMembers& proxied)
{
    if (fd.IsFinalizer()) {
        // The finalizer owns the moved state and is never named, so it moves without a proxy.
        return true;
    }

    // Were a static initializer allowed, it would have to travel with the static fields it assigns: left
    // behind, it would leave them unset for anything reading them through the registry companion.
    CJC_ASSERT_WITH_MSG(!IsStaticInitializer(fd), "@ObjCImpl static initializer is rejected by the parser");

    if (fd.TestAttr(Attribute::STATIC)) {
        // A static function stays reachable from the finalizer, so it needs a proxy left behind.
        auto proxyFd = ASTCloner::Clone(Ptr(&fd));
        // Cloned before the visibility is opened up, so the proxy keeps the one the user declared.
        ctx.astInserter.BindTo(target.impl, *proxyFd);
        ctx.astTransformer.TransformToObjCImplStaticProxyFunc(*proxyFd, fd, target.regCompanion);

        OpenUpForRegCompanions(fd);
        proxied.Add(fd, std::move(proxyFd));

        return true;
    }

    // Non-static member functions stay in the @ObjCImpl.
    return false;
}

/**
 * Where a reference is read from decides how a member that has moved is reached from there.
 *
 * Only a class body itself opens a region: testing the node's type instead would match every expression *of*
 * that type as well - `this`, a local of the impl type - and leaving the first of them would close the region
 * for the rest of the class body. @ObjCImpls and their companions are top-level, so regions do not nest.
 */
enum class Region {
    ELSEWHERE,     ///< through the proxy that stayed behind in the @ObjCImpl
    IMPL,          ///< straight through `$reg` or the companion class - the impl owns both
    REG_COMPANION, ///< the member is a sibling here; only the qualifier it carried along can be stale
};

Region RegionOf(const ClassDecl& cd)
{
    if (IsObjCExportRegistryCompanion(cd)) {
        return Region::REG_COMPANION;
    }
    return cd.IsObjCExport() ? Region::IMPL : Region::ELSEWHERE;
}

/**
 * Whether the member is named on some other instance - `other.<member>` - rather than on the class itself or
 * on `this`, which inside a registry companion already denotes the object that holds the moved member.
 */
bool IsNamedOnAnotherInstance(Node& node)
{
    auto ma = As<ASTKind::MEMBER_ACCESS>(&node);
    if (!ma) {
        return false;
    }

    auto base = As<ASTKind::REF_EXPR>(ma->baseExpr.get());
    if (base && (base->isThis || base->isSuper)) {
        return false;
    }

    return !base || DynamicCast<ClassDecl>(base->ref.target) == nullptr;
}

/**
 * `Impl.<member>` -> `Impl$reg.<member>`.
 *
 * A member travels into the companion with the qualifiers the user wrote untouched, so a static call spelled
 * `A.staticFunc()` still names the @ObjCImpl even though the callee now lives in `A$reg`. The class named
 * this way is not necessarily the one the member ended up in - one @ObjCImpl can name another's static, and a
 * subclass can name a static it inherits - so the qualifier becomes the companion of the class the user
 * *named*. That keeps a static dispatch pointed at the same class while satisfying CHIR, which wants the
 * receiver type to be the callee's owner or below it.
 */
void RequalifyToRegCompanion(const InteropContext& ctx, Node& node)
{
    auto ma = As<ASTKind::MEMBER_ACCESS>(&node);
    if (!ma) {
        // An unqualified name resolves against the enclosing class, which moved along with the member.
        return;
    }

    // `this.<member>` and `super.<member>` name the instance, not the class, and keep working.
    auto base = As<ASTKind::REF_EXPR>(ma->baseExpr.get());
    if (!base || base->isThis || base->isSuper) {
        return;
    }

    auto named = ctx.implToRegCompanion.find(base->ref.target);
    if (named == ctx.implToRegCompanion.end()) {
        return;
    }

    ma->baseExpr = WithinFile(CreateRefExpr(*named->second), node.curFile);
}

/**
 * The member a call names on the object of the enclosing class, or nullptr when it names something else.
 *
 * A bare name resolves against that class, and inside a registry companion `this` and `super` denote the
 * very same object, so all three read alike. A call that names a class or another instance carries a
 * receiver of its own, which stays the one the user wrote.
 */
Ptr<Decl> GetCalleeNamedOnSelf(const CallExpr& call) noexcept
{
    if (auto ref = As<ASTKind::REF_EXPR>(call.baseFunc.get())) {
        return ref->ref.target;
    }

    auto ma = As<ASTKind::MEMBER_ACCESS>(call.baseFunc.get());
    auto base = ma ? As<ASTKind::REF_EXPR>(ma->baseExpr.get()) : nullptr;
    return base && (base->isThis || base->isSuper) ? ma->target : nullptr;
}

/**
 * The declaration in an imported @ObjCImpl's registry companion that one of its proxies stands in for.
 *
 * A field is proxied by a property of the same name, so an accessor leads back to the field that moved; a
 * static function is proxied by a forwarder of the same name and signature.
 */
Ptr<Decl> FindImportedMovedMember(const InteropContext& ctx, ClassDecl& impl, Decl& proxy) noexcept
{
    auto regCompanion = ctx.GetRegCompanion(impl);
    if (!regCompanion) {
        return nullptr;
    }

    if (auto accessor = As<ASTKind::FUNC_DECL>(&proxy); accessor && accessor->propDecl) {
        const auto& movedName = accessor->propDecl->identifier.Val();
        return GetMemberDecl<ASTKind::VAR_DECL>(
            *regCompanion, [&movedName](const VarDecl& moved) { return moved.identifier.Val() == movedName; });
    }

    return GetMemberDecl<ASTKind::FUNC_DECL>(*regCompanion, [&proxy](const FuncDecl& moved) {
        return moved.identifier.Val() == proxy.identifier.Val() && moved.GetTy() == proxy.GetTy();
    });
}

void RetargetCallee(CallExpr& call, FuncDecl& callee) noexcept
{
    if (auto ref = As<ASTKind::REF_EXPR>(call.baseFunc.get())) {
        ref->ref.target = &callee;
    } else {
        StaticAs<ASTKind::MEMBER_ACCESS>(call.baseFunc.get())->target = &callee;
    }
    call.resolvedFunction = &callee;
}

/**
 * Resolves a call naming a proxy of an imported @ObjCImpl to the declaration that proxy stands in for.
 *
 * An imported impl went through this handler in its own package, so a name from here lands on the proxy it
 * left behind, which the impl still owns. Everywhere else in this package that owner is a supertype of
 * whatever the call is made on, but the companions inherit from one another and never from the impls, so
 * from inside one the proxy is out of reach - while the declaration it stands for is not, having been
 * inherited along with the parent companion. Resolving to it is what a name of an in-package impl does here
 * already, and it spares the moved member the hop back out through its proxy.
 *
 * A field ends up read straight from the companion again, so the accessor call the property desugaring left
 * behind is replaced whole: by the field for a read, by an assignment to it for a write.
 */
void ResolveToImportedMovedMember(const InteropContext& ctx, CallExpr& call)
{
    auto proxy = GetCalleeNamedOnSelf(call);
    if (!proxy || !IsObjCExportMovedMemberProxy(*proxy)) {
        return;
    }

    // A proxy of this package is not concerned: a name of one still points at the moved declaration itself,
    // which is where `proxied` below leaves it.
    auto impl = GetOwnerClass(*proxy);
    if (!impl || impl->fullPackageName == ctx.pkg.fullPackageName) {
        return;
    }

    auto moved = FindImportedMovedMember(ctx, *impl, *proxy);
    if (!moved) {
        return;
    }

    if (auto movedFunc = As<ASTKind::FUNC_DECL>(moved.get())) {
        RetargetCallee(call, *movedFunc);
        return;
    }

    auto field = WithinFile(CreateRefExpr(*StaticAs<ASTKind::VAR_DECL>(moved.get())), call.curFile);
    OwnedPtr<Expr> access = std::move(field);
    if (StaticAs<ASTKind::FUNC_DECL>(proxy.get())->isSetter) {
        CJC_ASSERT_WITH_MSG(!call.args.empty(), "expected the value a property setter is called with");
        access = CreateAssignExpr(
            std::move(access), std::move(call.args[0]->expr), {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
        access->curFile = call.curFile;
    }

    access->sourceExpr = &call;
    call.desugarExpr = std::move(access);
}

/**
 * `Impl.<field>` -> `Impl$reg.<field>`, `this.<field>` -> `$reg.<field>`.
 *
 * Reports whether the access was rewritten: a field named on some other instance - `other.<field>` - has no
 * direct form here and goes through the proxy like any reference from outside.
 */
bool RewriteAsDirectFieldAccess(Node& node, VarDecl& userField, const Decl& proxy)
{
    auto isStatic = userField.TestAttr(Attribute::STATIC);
    auto curFile = node.curFile;

    OwnedPtr<Expr> receiver;
    if (isStatic) {
        receiver = WithinFile(CreateRefExpr(*GetOwnerClass(userField)), curFile);
    } else {
        receiver = WithinFile(CreateRefExpr(*GetObjCImplRegCompanionField(*GetOwnerClass(proxy))), curFile);
    }

    auto fieldAccess = WithinFile(CreateMemberAccess(std::move(receiver), userField), curFile);

    if (auto ref = As<ASTKind::REF_EXPR>(&node)) {
        fieldAccess->sourceExpr = ref;
        ref->desugarExpr = std::move(fieldAccess);
        return true;
    }

    auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(&node);
    auto baseRef = As<ASTKind::REF_EXPR>(ma->baseExpr.get());
    if (isStatic || (baseRef && baseRef->isThis)) {
        fieldAccess->sourceExpr = ma;
        ma->desugarExpr = std::move(fieldAccess);
        return true;
    }

    return false;
}

/**
 * Re-resolves every reference to a moved member.
 *
 * Inside the impl a moved field is reached directly through the registry companion, because the impl owns the
 * `$reg` instance and the companion class alike; inside the companion the member is a sibling and only its
 * qualifier can be stale; everywhere else the reference goes through the proxy.
 */
void RewriteObjCImplMembersAccess(InteropContext& ctx, const ProxiedMembers& proxied)
{
    auto region = Region::ELSEWHERE;
    bool hasPropsResolved = false;

    Walker(
        &ctx.pkg,
        [&region, &ctx, &hasPropsResolved, &proxied](Ptr<Node> node) {
            if (!node->IsSamePackage(ctx.pkg)) {
                return VisitAction::SKIP_CHILDREN;
            }

            if (auto classDecl = As<ASTKind::CLASS_DECL>(node)) {
                region = RegionOf(*classDecl);
            }

            // A call keeps its own reference to the callee, besides the one in its base expression. Inside a
            // companion both have to stay pointed at the moved declaration itself, or the call would forward
            // back out to the proxy it is standing next to - and a member that moved out of an imported impl
            // has to be resolved there in the first place.
            if (auto call = As<ASTKind::CALL_EXPR>(node); call && call->resolvedFunction) {
                if (region == Region::REG_COMPANION) {
                    ResolveToImportedMovedMember(ctx, *call);
                } else if (auto proxy = proxied.Find(call->resolvedFunction)) {
                    call->resolvedFunction = StaticAs<ASTKind::FUNC_DECL>(proxy);
                }
            }

            Ptr<Decl>* targetSlot = nullptr;
            if (auto ref = As<ASTKind::REF_EXPR>(node)) {
                targetSlot = &ref->ref.target;
            } else if (auto ma = As<ASTKind::MEMBER_ACCESS>(node)) {
                targetSlot = &ma->target;
            }

            if (!targetSlot || !(*targetSlot)) {
                return VisitAction::WALK_CHILDREN;
            }

            auto target = *targetSlot;
            auto proxy = proxied.Find(target);
            if (!proxy) {
                return VisitAction::WALK_CHILDREN;
            }

            // Inside the companion the member is a sibling, so a name resolved against the enclosing class
            // or against the class itself is already right and only its qualifier can be stale. A field
            // named on some other instance has no direct form here either, and falls through to the proxy.
            if (region == Region::REG_COMPANION && !IsNamedOnAnotherInstance(*node)) {
                RequalifyToRegCompanion(ctx, *node);
                return VisitAction::WALK_CHILDREN;
            }

            if (region == Region::IMPL && target->astKind == ASTKind::VAR_DECL &&
                RewriteAsDirectFieldAccess(*node, *StaticAs<ASTKind::VAR_DECL>(target), *proxy)) {
                return VisitAction::SKIP_CHILDREN;
            }

            // The children are still walked, because the base expression of a member access may itself refer
            // to a moved member.
            *targetSlot = proxy;
            hasPropsResolved = hasPropsResolved || target->astKind == ASTKind::VAR_DECL;
            return VisitAction::WALK_CHILDREN;
        },
        [&region](Ptr<Node> node) {
            if (Is<ClassDecl>(node)) {
                region = Region::ELSEWHERE;
            }
            return VisitAction::KEEP_DECISION;
        })
        .Walk();

    if (hasPropsResolved) {
        ctx.desugarPropRef(ctx.pkg);
    }
}

} // namespace

void MoveImplMembersToRegCompanion::HandleImpl(InteropContext& ctx)
{
    ProxiedMembers proxied;
    for (auto& impl : ctx.impls) {
        if (impl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }

        auto regCompanion = ctx.implToRegCompanion.at(impl);
        CJC_NULLPTR_CHECK(regCompanion);
        auto regCompanionField = GetObjCImplRegCompanionField(*impl);
        CJC_NULLPTR_CHECK(regCompanionField);
        MoveTarget target{*impl, *regCompanion, *regCompanionField};

        auto& implMembers = impl->GetMemberDecls();
        for (auto memberIt = implMembers.begin(); memberIt != implMembers.end();) {
            auto member = memberIt->get();
            bool shouldMove = false;

            switch (member->astKind) {
                case ASTKind::VAR_DECL:
                    shouldMove = RewireMovedField(*StaticAs<ASTKind::VAR_DECL>(member), target, ctx, proxied);
                    break;
                case ASTKind::FUNC_DECL:
                    shouldMove = RewireMovedFunc(*StaticAs<ASTKind::FUNC_DECL>(member), target, ctx, proxied);
                    break;
                default:
                    break;
            }

            if (shouldMove) {
                ctx.astInserter.InsertInto(*regCompanion, std::move(*memberIt));
                memberIt = implMembers.erase(memberIt);
            } else {
                ++memberIt;
            }
        }
    }

    RewriteObjCImplMembersAccess(ctx, proxied);

    // Only now, with every reference re-resolved to it, is a proxy safe to insert - see ProxiedMembers.
    for (auto& proxy : proxied.InDeclarationOrder()) {
        ctx.astInserter.InsertInto(*StaticCast<ClassDecl>(proxy->outerDecl), std::move(proxy));
    }
}
} // namespace Cangjie::Interop::ObjC
