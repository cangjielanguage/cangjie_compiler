// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "RewriteJavaImplReferenceWrapperFields.h"
#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "NativeFFI/Java/Utils.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/AST/Walker.h"
#include <algorithm>

namespace Cangjie::Native::FFI::Java {
using namespace Interop::Java;

namespace {

void InsertProxyPropertyGetter(PropDecl& prop, VarDecl& userField, VarDecl& regCompanionField,
    TypeManager& typeManager)
{
    CJC_ASSERT(IsImplReferenceWrapper(*prop.outerDecl));
    prop.getters.emplace_back(WithinFile(MakeOwned<FuncDecl>(), prop.curFile));
    auto& getter = *prop.getters.back();
    getter.identifier = "$" + prop.identifier + "get";
    getter.outerDecl = prop.outerDecl;
    getter.fullPackageName = prop.fullPackageName;
    getter.moduleName = prop.moduleName;
    getter.propDecl = &prop;
    getter.isGetter = true;
    getter.SetTy({typeManager.GetFunctionTy({}, prop.GetTy())});

    getter.CloneAttrs(prop);
    getter.DisableAttr(Attribute::MUT);
    getter.EnableAttr(Attribute::COMPILER_ADD);

    getter.funcBody = WithinFile(MakeOwned<FuncBody>(), prop.curFile);
    auto& getterBody = *getter.funcBody;
    getterBody.SetTy(prop.GetTy());
    getterBody.EnableAttr(Attribute::COMPILER_ADD);
    getterBody.paramLists.push_back(WithinFile(MakeOwned<FuncParamList>(), prop.curFile));
    getterBody.paramLists.begin()->get()->EnableAttr(Attribute::COMPILER_ADD);
    getterBody.funcDecl = &getter;

    getterBody.body = WithinFile(CreateBlock({}, prop.GetTy()), prop.curFile);
    getterBody.body->SetTy(prop.GetTy());

    getterBody.body->body.emplace_back(
        CreateMemberAccess(
            WithinFile(CreateRefExpr(regCompanionField), prop.curFile),
            userField));
}

void InsertProxyPropertySetter(PropDecl& prop, VarDecl& userField, VarDecl& regCompanionField,
    TypeManager& typeManager)
{
    static auto unitTy = TypeManager::GetPrimitiveTy(AST::TypeKind::TYPE_UNIT);

    prop.setters.emplace_back(WithinFile(MakeOwned<FuncDecl>(), prop.curFile));
    auto& setter = *prop.setters.back();
    setter.identifier = "$" + prop.identifier + "set";
    setter.outerDecl = prop.outerDecl;
    setter.propDecl = &prop;
    setter.fullPackageName = prop.fullPackageName;
    setter.moduleName = prop.moduleName;
    setter.isSetter = true;
    setter.SetTy({typeManager.GetFunctionTy({prop.GetTy()}, {unitTy})});
    setter.CloneAttrs(prop);
    setter.DisableAttr(Attribute::MUT);
    setter.EnableAttr(Attribute::COMPILER_ADD);

    setter.funcBody = WithinFile(MakeOwned<FuncBody>(), prop.curFile);
    auto& setterBody = *setter.funcBody;
    setterBody.SetTy({unitTy});
    setterBody.EnableAttr(Attribute::COMPILER_ADD);
    setterBody.paramLists.push_back(WithinFile(MakeOwned<FuncParamList>(), prop.curFile));
    setterBody.paramLists.begin()->get()->EnableAttr(Attribute::COMPILER_ADD);
    setterBody.paramLists.begin()->get()->params.push_back(MakeOwned<FuncParam>());
    auto& setterParam = **setterBody.paramLists.begin()->get()->params.begin();
    setterParam.SetTy(prop.GetTy());
    setterParam.EnableAttr(Attribute::COMPILER_ADD);
    setterParam.identifier = "set";
    setterBody.funcDecl = &setter;

    setterBody.body = WithinFile(CreateBlock({}, prop.GetTy()), prop.curFile);
    setterBody.body->SetTy({unitTy});

    // $reg.<actualField> = set
    setterBody.body->body.emplace_back(WithinFile(
        CreateAssignExpr(CreateMemberAccess(WithinFile(CreateRefExpr(regCompanionField), prop.curFile), userField),
            WithinFile(CreateRefExpr(setterParam), prop.curFile),
            {TypeManager::GetPrimitiveTy(AST::TypeKind::TYPE_UNIT)}),
        prop.curFile));
}

} // namespace

void RewriteJavaImplReferenceWrapperFields::RelocateFields(AfterTypeCheckContext& ctx,
    ClassDecl& refWrapper, ClassDecl& registryCompanion) const
{
    auto& regCompanionField = ctx.GetJavaImplRegistryCompanionReferenceField(refWrapper);
    auto& wrapperMembers = refWrapper.GetMemberDecls();
    for (auto& member : wrapperMembers) {
        if (member->astKind != ASTKind::VAR_DECL || !Is<VarDecl>(member)) {
            continue;
        }

        // Original reference wrapper field
        auto& field = *StaticAs<ASTKind::VAR_DECL>(member.get());

        if (IsJavaImplRegistryCompanionReferenceField(field)) {
            continue;
        }

        generatedProps[&field] = GenerateProxyProperty(field, regCompanionField);

        registryCompanion.GetMemberDecls().emplace_back(std::move(member));
        member = nullptr; // removed and erased from the vector below

        field.outerDecl = &registryCompanion;
        field.isVar = true;
        field.modifiers.clear();
        field.DisableAttr(Attribute::PRIVATE, Attribute::INTERNAL, Attribute::PUBLIC);
        field.EnableAttr(Attribute::PROTECTED);
        if (!field.initializer) {
            field.initializer = utils.CreateZeroValue(field.DataTy(), *field.curFile);
        }
    }

    wrapperMembers.erase(
        std::remove_if(
            wrapperMembers.begin(),
            wrapperMembers.end(),
            [](OwnedPtr<Decl>& member) {
                return member == nullptr;
            }),
        wrapperMembers.end());
}

namespace {
bool IsInsideRefWrapperConstructor(Ptr<Node> node)
{
    auto fd = As<ASTKind::FUNC_DECL>(node);
    if (!fd || !fd->outerDecl || !IsImplReferenceWrapper(*fd->outerDecl)) {
        return false;
    }
    return fd->TestAttr(Attribute::CONSTRUCTOR);
}

bool IsInsideRegistryCompanion(Ptr<Node> node)
{
    auto cd = As<ASTKind::CLASS_DECL>(node);
    if (!cd || !IsImplRegistryCompanion(*cd)) {
        return false;
    }

    return true;
}
}

void RewriteJavaImplReferenceWrapperFields::RewriteFieldAccess(AfterTypeCheckContext& ctx, Package& pkg) const
{
    bool withinRefWrapperConstructor = false; // Used in rewriter visitor as an indicator.
    bool hasPropsResolved = false;

    /*
     * Generated reference wrapper proxy properties should be skipped by this visitor
     * because its bodies are already resolved with the correct way
     * (reference wrapper AST at this stage does not have proxy properties inserted yet until `PushProxyProperties`).
     */
    Walker(&pkg, [&](Ptr<Node> node) {
        // Pre-visitor
        if (!node->IsSamePackage(pkg)) {
            return VisitAction::WALK_CHILDREN;
        }

        // Pre-visit: set flags if corresponding node has been visiting.
        if (IsInsideRefWrapperConstructor(node)) {
            withinRefWrapperConstructor = true;
        } else if (IsInsideRegistryCompanion(node)) {
            return VisitAction::SKIP_CHILDREN;
        }

        Ptr<Decl>* target = nullptr;
        if (auto ref = As<ASTKind::REF_EXPR>(node)) {
            target = &ref->ref.target;
        } else if (auto ma = As<ASTKind::MEMBER_ACCESS>(node)) {
            target = &ma->target;
        }

        if (!target || !(*target) || !(*target)->outerDecl || !IsImplRegistryCompanion(*(*target)->outerDecl)) {
            return VisitAction::WALK_CHILDREN;
        }

        if (!(*target)->IsSamePackage(pkg)) {
            // No need to rewrite access on @JavaImpl fields compiled as another package:
            // It already should be resolved to proxy property.
            return VisitAction::WALK_CHILDREN;
        }

        auto userField = As<ASTKind::VAR_DECL>(*target);
        // Also check astKind since VarDecl could be a PropDecl.
        // Property access should not be re-resolved: it is already correct.
        if (!userField || (*target)->astKind != ASTKind::VAR_DECL) {
            return VisitAction::WALK_CHILDREN;
        }
        auto proxyProp = generatedProps[userField].get();
        CJC_NULLPTR_CHECK(proxyProp);
        auto& refWrapper = *StaticAs<ASTKind::CLASS_DECL>(proxyProp->outerDecl);

        if (withinRefWrapperConstructor) {
            auto isStatic = userField->TestAttr(Attribute::STATIC);

            auto& registryCompanion = *StaticAs<ASTKind::CLASS_DECL>(userField->outerDecl);

            auto maReceiver = isStatic
                ? CreateRefExpr(registryCompanion)
                : CreateRefExpr(ctx.GetJavaImplRegistryCompanionReferenceField(refWrapper));
            AddCurFile(*maReceiver, refWrapper.curFile);

            auto fieldAccess = CreateMemberAccess(std::move(maReceiver), *userField);

            if (auto ref = As<ASTKind::REF_EXPR>(node)) {
                // Re-points ref wrapper field access to registry companion field.
                ref->desugarExpr = std::move(fieldAccess);
                return VisitAction::SKIP_CHILDREN;
            }
            if (auto ma = As<ASTKind::MEMBER_ACCESS>(node)) {
                if (isStatic) {
                    // Re-points `Type.<field>` access to registry companion field.
                    ma->desugarExpr = std::move(fieldAccess);
                    return VisitAction::SKIP_CHILDREN;
                }

                if (auto lvalueRef = As<ASTKind::REF_EXPR>(ma->baseExpr.get()); lvalueRef && lvalueRef->isThis) {
                    // Re-points `this.<field>` access to registry companion field.
                    ma->desugarExpr = std::move(fieldAccess);
                    return VisitAction::SKIP_CHILDREN;
                }
            }
        }
        *target = proxyProp;
        hasPropsResolved = true;

        return VisitAction::SKIP_CHILDREN;
    }, [&withinRefWrapperConstructor](Ptr<Node> node) {
        // Post-visitor: clear flags after visit.
        if (IsInsideRefWrapperConstructor(node)) {
            withinRefWrapperConstructor = false;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();

    if (hasPropsResolved) {
        /*
         * Re-points resolve from field to property.
         * After that, reruns property accessor desugaring on that node.
         * Since property resolve desugaring completes on early compiler stage, it has to be rerun
         * for these nodes specifically.
         */
        desugarPropRef(pkg);
    }
}

OwnedPtr<PropDecl> RewriteJavaImplReferenceWrapperFields::GenerateProxyProperty(VarDecl& userField,
    VarDecl& regCompanionRefField) const
{
    auto propDecl = WithinFile(MakeOwned<PropDecl>(), userField.curFile);
    propDecl->begin = userField.begin;
    propDecl->end = userField.end;
    propDecl->keywordPos = userField.keywordPos;
    propDecl->identifier = userField.identifier;
    propDecl->colonPos = userField.colonPos;
    propDecl->type = std::move(userField.type);
    propDecl->SetTy(userField.GetTy());
    propDecl->CloneAttrs(userField);
    propDecl->EnableAttr(Attribute::COMPILER_ADD);
    propDecl->modifiers.insert(userField.modifiers.begin(), userField.modifiers.end());
    propDecl->isVar = userField.isVar;
    for (auto& anno : userField.annotations) {
        propDecl->annotations.emplace_back(ASTCloner::Clone(anno.get()));
    }
    if (userField.isVar) {
        propDecl->EnableAttr(Attribute::MUT);
        Modifier mut = Modifier(TokenKind::MUT, userField.begin);
        mut.curFile = userField.curFile;
        propDecl->modifiers.insert(std::move(mut));
    }
    propDecl->outerDecl = userField.outerDecl;
    propDecl->fullPackageName = userField.fullPackageName;
    propDecl->moduleName = userField.moduleName;

    InsertProxyPropertyGetter(*propDecl, userField, regCompanionRefField, typeManager);
    if (userField.isVar) {
        InsertProxyPropertySetter(*propDecl, userField, regCompanionRefField, typeManager);
    }
    return propDecl;
}

void RewriteJavaImplReferenceWrapperFields::PushProxyProperties() const
{
    for (auto& [_, proxyProp] : generatedProps) {
        auto& refWrapper = *proxyProp->outerDecl;
        refWrapper.GetMemberDecls().push_back(std::move(proxyProp));
    }
    generatedProps.clear();
}

RewriteJavaImplReferenceWrapperFields::RewriteJavaImplReferenceWrapperFields(TypeManager& typeManager,
    Native::FFI::Java::Utils& utils, std::function<void(AST::Node&)> desugarPropRef)
    : typeManager(typeManager), utils(utils), desugarPropRef(desugarPropRef)
{
}

void RewriteJavaImplReferenceWrapperFields::Process(AfterTypeCheckContext& ctx)
{
    for (auto refWrapper : ctx.GetJavaImplReferenceWrappers()) {
        auto& registryCompanion = ctx.GetJavaImplRegistryCompanion(*refWrapper);
        RelocateFields(ctx, *refWrapper, registryCompanion);
    }
    RewriteFieldAccess(ctx, ctx.pkg);
    PushProxyProperties();
}

} // namespace Cangjie::Native::FFI::Java
