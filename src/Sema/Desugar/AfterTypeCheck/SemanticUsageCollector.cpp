// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/AfterTypeCheck.h"

#include "ExtendBoxMarker.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/IncrementalCompilation/IncrementalScopeAnalysis.h"
#include "cangjie/Mangle/ASTMangler.h"
#include "cangjie/Sema/IncrementalUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

// Incremental compilation only enable in cjnative backend for now.
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
namespace {
class SemanticUsageCollector {
public:
    explicit SemanticUsageCollector(TypeManager& typeManager, const std::vector<Ptr<Package>>& pkgs)
        : tyMgr(typeManager), pkgs(pkgs)
    {
        tyMgr.ClearRecordUsedExtends(); // Unset previous collection status.
    }
    ~SemanticUsageCollector()
    {
        currentTypeDecl = nullptr;
    }

    SemanticInfo CollectInfoUsages()
    {
        for (auto pkg : pkgs) {
            IterateToplevelDecls(*pkg, [this](auto& decl) { CollectForDecl(*decl); });
        }
        return info;
    }

private:
    void CollectForDecl(const Decl& decl)
    {
        if (decl.TestAnyAttr(Attribute::MACRO_INVOKE_FUNC, Attribute::IMPLICIT_ADD, Attribute::ENUM_CONSTRUCTOR)) {
            // 1. ignore macro invoke function and implicit added decls.
            // 2. Enum ctor does not have rawMangledName, it is considered as part of EnumDecl.
            return;
        }
        if (decl.TestAttr(Attribute::INCRE_COMPILE)) {
            info.usages.emplace(&decl, SemaUsage{}); // If decl is unchanged, only create empty entry.
            return;
        }
        if (auto vd = DynamicCast<const VarDecl*>(&decl)) {
            CollectAPIUsage(*vd);
            CollectNameUsage(*vd);
        } else if (auto md = DynamicCast<const MainDecl*>(&decl); md && md->desugarDecl) {
            CollectForDecl(*md->desugarDecl);
        } else if (auto fd = DynamicCast<const FuncDecl*>(&decl)) {
            CollectAPIUsage(*fd);
            CollectNameUsage(*fd);
        } else if (auto id = DynamicCast<const InheritableDecl*>(&decl)) {
            currentTypeDecl = id;
            CollectAPIUsage(*id);
            CollectAnnotationUsage(*id, info.usages[id]);
            CollectRelation(*id);
            for (auto& member : id->GetMemberDeclPtrs()) {
                if (auto pd = DynamicCast<PropDecl*>(member)) {
                    CollectAPIUsage(*pd);
                    std::for_each(pd->getters.begin(), pd->getters.end(), [this](auto& it) { CollectForDecl(*it); });
                    std::for_each(pd->setters.begin(), pd->setters.end(), [this](auto& it) { CollectForDecl(*it); });
                } else {
                    // NOTE: PrimaryCtorDecl will be ignored by default.
                    CollectForDecl(*member);
                }
            }
            currentTypeDecl = nullptr;
        }
    }

    static std::string GetName(const Node& node)
    {
        if (auto rt = DynamicCast<const RefType*>(&node)) {
            return rt->ref.identifier;
        } else if (auto re = DynamicCast<const RefExpr*>(&node)) {
            return re->ref.identifier;
        } else if (auto qt = DynamicCast<const QualifiedType*>(&node)) {
            return qt->field;
        } else if (auto ma = DynamicCast<const MemberAccess*>(&node)) {
            return ma->field;
        }
        return "";
    }

    void CollectUseOfParentByTy(const Ty& ty, UseInfo& usage, NameUsage& nameUsage) const
    {
        // When accessed field is member of a type, we need to collect the accessed type.
        if (auto typeDecl = Ty::GetDeclPtrOfTy(&ty)) {
            (void)nameUsage.parentDecls.emplace(typeDecl->rawMangleName);
            // Only need to collect type of node when it is a baseExpr of memberAccess.
            (void)usage.usedDecls.emplace(typeDecl->rawMangleName);
        } else if (Ty::IsTyCorrect(&ty)) {
            (void)nameUsage.parentDecls.emplace(ASTMangler::MangleBuiltinType(Ty::KindName(ty.kind)));
        }
    }

    void CollectUseOfUnqualifedMember(const Decl& decl, UseInfo& usage) const
    {
        if (decl.IsTypeDecl() || !decl.outerDecl || !decl.outerDecl->IsNominalDecl() ||
            decl.TestAttr(Attribute::CONSTRUCTOR)) {
            return;
        }
        // Collect real parent of accessd parent.
        CJC_NULLPTR_CHECK(decl.outerDecl->ty);
        auto& nameUsage = usage.usedNames[decl.identifier];
        CollectUseOfParentByTy(*decl.outerDecl->ty, usage, nameUsage);
        if (currentTypeDecl && Ty::IsTyCorrect(currentTypeDecl->ty)) {
            // Collect current 'this' parent decl.
            CollectUseOfParentByTy(*currentTypeDecl->ty, usage, nameUsage);
        }
    }

    void CollectUseOfQualifedMember(
        const MemberAccess& ma, const Decl& target, UseInfo& usage, NameUsage& nameUsage) const
    {
        CJC_NULLPTR_CHECK(target.outerDecl);
        auto accessedTy = ma.isExposedAccess ? target.outerDecl->ty : ma.baseExpr->ty;
        if (Ty::IsTyCorrect(accessedTy)) {
            CollectUseOfParentByTy(*accessedTy, usage, nameUsage);
        }
    }

    void CollectForEnumAndStructTypeUse(const Node& node, UseInfo& usage) const
    {
        if (!Ty::IsTyCorrect(node.ty) || (!node.ty->IsEnum() && !node.ty->IsStruct())) {
            return;
        }
        auto ed = Ty::GetDeclPtrOfTy(node.ty);
        CJC_NULLPTR_CHECK(ed);
        usage.usedDecls.emplace(ed->rawMangleName);
    }

    VisitAction CollectUseInfo(const Node& node, UseInfo& usage) const
    {
        CollectForEnumAndStructTypeUse(node, usage);
        auto target = Is<Type>(node) ? Ty::GetDeclPtrOfTy(node.ty) : node.GetTarget();
        if (target == nullptr || target->IsBuiltIn() || target->astKind == ASTKind::PACKAGE_DECL) {
            return VisitAction::WALK_CHILDREN;
        }
        // Ignore decl usage for compiler added decl which does not have raw mangled name.
        if (!target->rawMangleName.empty()) {
            usage.usedDecls.emplace(target->rawMangleName);
        }
        // Also record parent decl's usage when 'target' is constructor.
        if (IsClassOrEnumConstructor(*target)) {
            (void)usage.usedDecls.emplace(target->outerDecl->rawMangleName);
            usage.usedDecls.emplace(target->outerDecl->rawMangleName);
             // When 'target' is compiler added constructor, collect usage of constructed mangledName.
            if (target->TestAttr(Attribute::IMPLICIT_ADD)) {
                usage.usedDecls.emplace(target->outerDecl->rawMangleName + "<init>");
            }
        }
        // We need to collect reference's real name as used name (because the name may be alias).
        auto name = GetName(node);
        if (name.empty()) {
            return VisitAction::WALK_CHILDREN;
        }
        auto& nameUsage = usage.usedNames[name];
        if (node.astKind == ASTKind::REF_TYPE || node.astKind == ASTKind::REF_EXPR) {
            if (target->TestAttr(Attribute::IMPORTED)) {
                nameUsage.hasUnqualifiedUsageOfImported = true;
            } else {
                nameUsage.hasUnqualifiedUsage = true;
            }
            CollectUseOfUnqualifedMember(*target, usage);
        } else if (auto qt = DynamicCast<const QualifiedType*>(&node)) {
            auto qualifier = ASTContext::GetPackageName(qt->baseType.get());
            nameUsage.packageQualifiers.emplace(qualifier);
            return VisitAction::SKIP_CHILDREN;
        } else if (auto ma = DynamicCast<const MemberAccess*>(&node)) {
            CJC_NULLPTR_CHECK(ma->baseExpr);
            if (target->TestAttr(Attribute::GLOBAL)) {
                // Only collect qualifier when target is gloabl decl.
                auto qualifier = ASTContext::GetPackageName(ma->baseExpr.get());
                nameUsage.packageQualifiers.emplace(qualifier);
                return VisitAction::SKIP_CHILDREN;
            }
            CollectUseOfQualifedMember(*ma, *target, usage, nameUsage);
        }
        return VisitAction::WALK_CHILDREN;
    }

    void CollectAPIUsage(const VarDecl& vd)
    {
        if (vd.type) {
            CJC_ASSERT(!vd.rawMangleName.empty());
            CollectAPIUsage(*vd.type, info.usages[&vd].apiUsages);
        }
    }

    void CollectAPIUsage(Type& type, UseInfo& usage) const
    {
        Walker(&type, [this, &usage](auto node) {
            // Ignore implicit added nodes, these will base on user defined code and core package.
            if (node->TestAttr(Attribute::IMPLICIT_ADD)) {
                return VisitAction::SKIP_CHILDREN;
            }
            return CollectUseInfo(*node, usage);
        }).Walk();
    }

    void CollectGenericUsage(const Generic& generic, UseInfo& usage) const
    {
        for (auto& gc : generic.genericConstraints) {
            for (const auto& upperBound : gc->upperBounds) {
                auto decl = Ty::GetDeclPtrOfTy(upperBound->ty);
                if (decl && !decl->rawMangleName.empty()) {
                    usage.usedDecls.emplace(decl->rawMangleName);
                }
            }
        }
    }

    void CollectAPIUsage(const FuncDecl& fd)
    {
        CJC_NULLPTR_CHECK(fd.funcBody);
        CJC_ASSERT(fd.funcBody->paramLists.size() == 1 && !fd.rawMangleName.empty());
        auto& usage = info.usages[&fd].apiUsages;
        for (auto& param : fd.funcBody->paramLists[0]->params) {
            CJC_NULLPTR_CHECK(param->type);
            CollectAPIUsage(*param->type, usage);
        }
        if (fd.funcBody->retType && !fd.funcBody->retType->TestAttr(Attribute::COMPILER_ADD)) {
            CollectAPIUsage(*fd.funcBody->retType, usage);
        }
        if (fd.funcBody->generic) {
            CollectGenericUsage(*fd.funcBody->generic, usage);
        }
    }

    void CollectAPIUsage(const InheritableDecl& id)
    {
        CJC_ASSERT(!id.rawMangleName.empty());
        auto& usage = info.usages[&id].apiUsages;
        for (auto& type : id.inheritedTypes) {
            CollectAPIUsage(*type, usage);
        }
        if (id.generic) {
            CollectGenericUsage(*id.generic, usage);
        }
        auto ed = DynamicCast<const EnumDecl*>(&id);
        if (!ed) {
            return;
        }
        for (auto& ctor : ed->constructors) {
            auto fd = DynamicCast<FuncDecl*>(ctor.get());
            if (!fd) {
                continue;
            }
            CJC_ASSERT(fd->funcBody->paramLists.size() == 1);
            for (auto& param : fd->funcBody->paramLists[0]->params) {
                CJC_NULLPTR_CHECK(param->type);
                CollectAPIUsage(*param->type, usage);
            }
        }
    }

    void CollectRelation(const InheritableDecl& id)
    {
        // Ignore for non-extend decls which does not have 'inheritedTypes'.
        if (id.inheritedTypes.empty() && id.astKind != ASTKind::EXTEND_DECL) {
            return;
        }
        CJC_ASSERT(!id.rawMangleName.empty());
        CJC_NULLPTR_CHECK(id.ty);
        auto decl = Ty::GetDeclPtrOfTy(id.ty);
        CJC_ASSERT(!decl || !decl->rawMangleName.empty());
        auto& relation = decl ? info.relations[decl->rawMangleName]
                              : info.builtInTypeRelations[ASTMangler::MangleBuiltinType(Ty::KindName(id.ty->kind))];
        bool isExtend = false;
        if (Is<const ExtendDecl*>(&id)) {
            relation.extends.emplace(id.rawMangleName);
            isExtend = true;
        }
        auto& inherited = isExtend ? relation.extendedInterfaces : relation.inherits;
        for (auto& type : id.inheritedTypes) {
            auto target = Ty::GetDeclPtrOfTy(type->ty); // Type without target will never be valid inherited type.
            CJC_ASSERT(target && !target->rawMangleName.empty());
            inherited.emplace(target->rawMangleName);
        }
    }

    void CollectAnnotationUsage(const Decl& decl, SemaUsage& usage) const
    {
        if (decl.annotationsArray) {
            for (auto& it : decl.annotationsArray->children) {
                CollectNameUsage(*it, usage);
            }
        }
    }

    void CollectNameUsage(const VarDecl& vd)
    {
        auto& usage = info.usages[&vd];
        if (vd.initializer) {
            // NOTE: To collect boxing relation correctly, we need pass varDecl itself.
            CollectNameUsage(const_cast<VarDecl&>(vd), usage);
        }
        CollectAnnotationUsage(vd, usage);
    }

    void CollectNameUsage(const FuncDecl& fd)
    {
        CJC_NULLPTR_CHECK(fd.funcBody);
        CJC_ASSERT(fd.funcBody->paramLists.size() == 1 && !fd.rawMangleName.empty());
        auto& usage = info.usages[&fd];
        for (auto& param : fd.funcBody->paramLists[0]->params) {
            CJC_NULLPTR_CHECK(param);
            if (param->assignment) {
                CollectNameUsage(*param->assignment, usage);
            }
            CollectAnnotationUsage(*param, usage);
        }
        // Abstract functions may omit func body.
        if (fd.funcBody->body) {
            CollectNameUsage(*fd.funcBody->body, usage);
        }
        if (fd.TestAttr(Attribute::CONSTRUCTOR)) {
            AddUsedBySpecificMemberVars(fd, fd.TestAttr(Attribute::STATIC));
        }
        CollectAnnotationUsage(fd.propDecl ? *StaticCast<Decl>(fd.propDecl) : StaticCast<Decl>(fd), usage);
    }

    void CollectNameUsage(Node& node, SemaUsage& usage) const
    {
        {
            std::lock_guard lockGuard{ExtendBoxMarker::mtx};
            Walker(&node, ExtendBoxMarker::GetMarkExtendBoxFunc(tyMgr)).Walk();
        }
        auto boxedTys = tyMgr.GetAllBoxedTys();
        tyMgr.ClearRecordUsedExtends(); // Unset collection status.
        for (auto ty : boxedTys) {
            usage.boxedTypes.emplace(Sema::GetTypeRawMangleName(*ty));
        }
        auto nodePtr = &node;
        if (node.astKind == ASTKind::VAR_DECL) {
            // After collecting usage of boxing, change node from vardecl to its initializer.
            nodePtr = StaticCast<VarDecl&>(node).initializer.get();
        }
        Walker(nodePtr, [this, &usage](auto n) {
            // Ignore implicit added default argument.
            if (n->TestAttr(Attribute::HAS_INITIAL) && n->astKind == ASTKind::FUNC_ARG) {
                return VisitAction::SKIP_CHILDREN;
            }
            return CollectUseInfo(*n, usage.bodyUsages);
        }).Walk();
    }

    void AddUsedBySpecificMemberVars(const FuncDecl& ctor, bool isStatic)
    {
        CJC_NULLPTR_CHECK(ctor.outerDecl);
        auto& members = ctor.outerDecl->GetMemberDecls();
        for (auto& member : members) {
            CJC_NULLPTR_CHECK(member);
            if (member->astKind != ASTKind::VAR_DECL || member->TestAttr(Attribute::STATIC) != isStatic) {
                continue;
            }
            auto vd = StaticCast<VarDecl>(member.get());
            if (!vd->isMemberParam && !vd->initializer) {
                // Static/non-static variable without initializer should be considered
                // as implicitly using static initializer/instance constructor.
                auto& usage = info.usages[vd];
                usage.bodyUsages.usedDecls.emplace(ctor.rawMangleName);
            }
        }
    }

    TypeManager& tyMgr;
    const std::vector<Ptr<Package>>& pkgs;
    SemanticInfo info;
    Ptr<const InheritableDecl> currentTypeDecl{nullptr};
};
} // namespace
#endif

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
SemanticInfo GetSemanticUsage(TypeManager& typeManager, const std::vector<Ptr<Package>>& pkgs)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    SemanticUsageCollector collector(typeManager, pkgs);
    return collector.CollectInfoUsages();
#endif
}
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck
