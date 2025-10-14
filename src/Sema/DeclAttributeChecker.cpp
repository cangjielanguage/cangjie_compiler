// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the functions to check decl attributes.
 */

#include "TypeCheckerImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace Meta;

namespace {
class DeclAttributeChecker {
public:
    DeclAttributeChecker(const GlobalOptions& options, DiagnosticEngine& diag, Decl& decl)
        : opts{options}, diag(diag), decl_(decl)
    {
    }

    ~DeclAttributeChecker() = default;

    /** Dispatch checking function for decl to be checked. */
    void Check() const;

private:
    void SetCommonAttributes(Decl& d) const;
    void CheckClassAttribute(ClassDecl& cd) const;
    void CheckInterfaceAttribute(InterfaceDecl& id) const;
    void CheckEnumAttribute(EnumDecl& ed) const;
    void CheckStructAttribute(StructDecl& sd) const;
    void CheckExtendAttribute(ExtendDecl& ed) const;
    void CheckPropDeclAttributes(const PropDecl& pd) const;
    void CheckGenericFuncDeclAttributes(const FuncDecl& fd) const;
    void CheckAttributesForPropAndFuncDeclInClass(const ClassDecl& cd, Decl& member) const;
    void CheckCJMPAttributesForPropAndFuncDeclInClass(const ClassDecl& cd, Decl& member) const;

    static void SetContextAttribute(const Decl& decl, Decl& memberDecl)
    {
        auto found = ctxAttrMap.find(decl.astKind);
        if (found != ctxAttrMap.end()) {
            memberDecl.EnableAttr(found->second);
        }
    }

    const GlobalOptions& opts;
    DiagnosticEngine& diag;
    Decl& decl_;

    inline static std::unordered_map<ASTKind, Attribute> ctxAttrMap = {
        {ASTKind::INTERFACE_DECL, Attribute::IN_CLASSLIKE},
        {ASTKind::CLASS_DECL, Attribute::IN_CLASSLIKE},
        {ASTKind::ENUM_DECL, Attribute::IN_ENUM},
        {ASTKind::STRUCT_DECL, Attribute::IN_STRUCT},
        {ASTKind::EXTEND_DECL, Attribute::IN_EXTEND},
    };
};

void SetParentDecl(Decl& structDecl, const FuncDecl& fd)
{
    if (!fd.funcBody) {
        return;
    }
    auto outerDecl = &structDecl;
    if (auto ed = DynamicCast<ExtendDecl*>(outerDecl); ed && ed->extendedType) {
        auto extendedDecl = Ty::GetDeclPtrOfTy(ed->extendedType->ty);
        if (!extendedDecl) {
            return;
        }
        outerDecl = extendedDecl;
    }
    if (auto cld = DynamicCast<ClassLikeDecl*>(outerDecl); cld) {
        fd.funcBody->parentClassLike = cld;
    } else if (auto sd = DynamicCast<StructDecl*>(outerDecl); sd) {
        fd.funcBody->parentStruct = sd;
    } else if (auto ed = DynamicCast<EnumDecl*>(outerDecl); ed) {
        fd.funcBody->parentEnum = ed;
    }
}

void SetContextDecl(Decl& structDecl, Decl& member)
{
    member.outerDecl = &structDecl;
    if (auto pd = DynamicCast<PropDecl*>(&member); pd) {
        auto setDecl = [&structDecl](auto& it) {
            it->outerDecl = &structDecl;
            SetParentDecl(structDecl, *it);
        };
        std::for_each(pd->getters.begin(), pd->getters.end(), setDecl);
        std::for_each(pd->setters.begin(), pd->setters.end(), setDecl);
    } else if (auto fd = DynamicCast<FuncDecl*>(&member); fd) {
        SetParentDecl(structDecl, *fd);
        if (fd->funcBody->paramLists.empty()) {
            return;
        }
        for (auto& param : fd->funcBody->paramLists[0]->params) {
            if (param->desugarDecl) {
                SetParentDecl(structDecl, *param->desugarDecl);
            }
        }
    }
}

/** Mark static attribute and parent decl recursively. */
void InheritMemberAttrRecursively(Decl& memberDecl, bool isStatic)
{
    auto visitor = [isStatic](Ptr<Node> node) -> VisitAction {
        if (node->IsNominalDecl()) {
            return VisitAction::SKIP_CHILDREN; // Ignore for invalid nesting type decl.
        }
        switch (node->astKind) {
            case ASTKind::FUNC_BODY: {
                if (isStatic) {
                    node->EnableAttr(Attribute::STATIC);
                }
                break;
            }
            case ASTKind::FUNC_DECL: {
                auto fd = RawStaticCast<FuncDecl*>(node);
                if (isStatic) {
                    fd->EnableAttr(Attribute::STATIC);
                }
                if (fd->propDecl && !fd->TestAttr(Attribute::IMPLICIT_ADD)) {
                    fd->CloneAttrs(*fd->propDecl);
                    fd->DisableAttr(Attribute::MUT);
                }
                break;
            }
            default:
                break;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&memberDecl, visitor);
    walker.Walk();
}

void SetDeclInternal(Decl& d)
{
    if (d.TestAnyAttr(Attribute::PROTECTED, Attribute::PRIVATE, Attribute::PUBLIC) ||
        d.astKind == ASTKind::EXTEND_DECL) {
        return;
    }
    d.EnableAttr(Attribute::INTERNAL);
}
} // namespace

void DeclAttributeChecker::SetCommonAttributes(Decl& d) const
{
    CJC_ASSERT(d.IsNominalDecl());
    SetDeclInternal(d);
    for (auto memberDecl : d.GetMemberDeclPtrs()) {
        CJC_ASSERT(memberDecl);
        if (d.TestAttr(Attribute::FOREIGN)) {
            memberDecl->EnableAttr(Attribute::FOREIGN);
        }
        SetDeclInternal(*memberDecl);
        SetContextDecl(d, *memberDecl);
        SetContextAttribute(d, *memberDecl);
        InheritMemberAttrRecursively(*memberDecl, memberDecl->TestAttr(Attribute::STATIC));
    }
}

void DeclAttributeChecker::CheckInterfaceAttribute(InterfaceDecl& id) const
{
    CJC_NULLPTR_CHECK(id.body);
    if (id.TestAttr(Attribute::SEALED)) {
        id.EnableAttr(Attribute::OPEN, Attribute::PUBLIC);
    }
    for (auto& member : id.body->decls) {
        CJC_ASSERT(member);
        member->EnableAttr(Attribute::PUBLIC); // All members in interface are public.
        if (!member->TestAttr(Attribute::STATIC)) {
            // Instance member func and prop in interface are open.
            member->EnableAttr(Attribute::OPEN);
        }
        if (auto pd = DynamicCast<PropDecl*>(member.get())) {
            auto setMut = [](auto& it) {
                if (it && !it->TestAttr(Attribute::STATIC)) {
                    it->EnableAttr(Attribute::MUT);
                }
            };
            std::for_each(pd->setters.begin(), pd->setters.end(), setMut);
        }
    }
    SetCommonAttributes(id);
}

void DeclAttributeChecker::CheckEnumAttribute(EnumDecl& ed) const
{
    for (auto& constructor : ed.constructors) {
        constructor->EnableAttr(Attribute::PUBLIC);
    }
    SetCommonAttributes(ed);
}

void DeclAttributeChecker::CheckStructAttribute(StructDecl& sd) const
{
    if (!sd.body) {
        return;
    }
    SetCommonAttributes(sd);
    auto setMut = [](auto& it) {
        if (it && !it->TestAttr(Attribute::STATIC)) {
            it->EnableAttr(Attribute::MUT);
        }
    };
    for (auto& member : sd.body->decls) {
        CJC_ASSERT(member);
        if (member->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            continue; // PrimaryCtor was desugared as normal func decl before checking process.
        }
        if (auto pd = DynamicCast<PropDecl*>(member.get()); pd) {
            std::for_each(pd->setters.begin(), pd->setters.end(), setMut);
        }
    }
}

void DeclAttributeChecker::CheckExtendAttribute(ExtendDecl& ed) const
{
    SetCommonAttributes(ed);
    for (auto& member : ed.members) {
        CJC_ASSERT(member);
        const auto& extendedDecl = Ty::GetDeclPtrOfTy(ed.ty);
        Ptr<const Node> mutDecl = nullptr;
        for (auto& modifier : member->modifiers) {
            if (modifier.modifier == TokenKind::MUT) {
                mutDecl = &modifier;
            }
        }
        if (member.get()->TestAttr(Attribute::MUT) && mutDecl) {
            if (extendedDecl && extendedDecl->astKind != ASTKind::STRUCT_DECL &&
                member->astKind != ASTKind::PROP_DECL) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_invalid_mut_modifier_extend_of_struct, *mutDecl, extendedDecl->identifier);
            } else if (ed.ty->IsPrimitive()) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_invalid_mut_modifier_extend_of_struct, *mutDecl, ed.ty->String());
            }
        }
        if (auto pd = DynamicCast<PropDecl*>(member.get()); pd) {
            bool needMut = !pd->TestAttr(Attribute::STATIC) && ed.ty && ed.ty->IsStruct();
            auto setMut = [needMut](auto& it) {
                if (it && needMut) {
                    it->EnableAttr(Attribute::MUT);
                }
            };
            std::for_each(pd->setters.begin(), pd->setters.end(), setMut);
        }
    }
}

void DeclAttributeChecker::CheckClassAttribute(ClassDecl& cd) const
{
    CJC_NULLPTR_CHECK(cd.body);
    if (cd.TestAttr(Attribute::SEALED)) {
        cd.EnableAttr(Attribute::OPEN, Attribute::PUBLIC);
        if (!cd.TestAttr(Attribute::ABSTRACT)) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::sema_non_abstract_class_cannot_be_sealed, cd, MakeRange(cd.identifier));
        }
    }
    SetCommonAttributes(cd);
    for (auto& memberDecl : cd.body->decls) {
        CJC_ASSERT(memberDecl);
        if (memberDecl->IsFuncOrProp()) {
            CheckAttributesForPropAndFuncDeclInClass(cd, *memberDecl);
        }
    }
}

void DeclAttributeChecker::CheckCJMPAttributesForPropAndFuncDeclInClass(const ClassDecl& cd, Decl& member) const
{
    bool inCJMP = cd.TestAttr(Attribute::COMMON) || cd.TestAttr(Attribute::PLATFORM);
    bool inAbstractCJMP = cd.TestAttr(Attribute::ABSTRACT) && inCJMP;

    bool isCJMP = member.TestAttr(Attribute::COMMON) || member.TestAttr(Attribute::PLATFORM);
    bool isAbstract = member.TestAttr(Attribute::ABSTRACT);

    bool compilerAddedOrConstructor = member.TestAttr(Attribute::COMPILER_ADD) ||
        member.TestAttr(Attribute::CONSTRUCTOR);
    bool hasBody = false;
    std::string memberKind;
    if (member.astKind == ASTKind::FUNC_DECL) {
        memberKind = "function";
        auto func = StaticCast<FuncDecl*>(&member);
        if (func->funcBody && func->funcBody->body) {
            hasBody = true;
        }
    } else {
        memberKind = "property";
        auto prop = StaticCast<PropDecl*>(&member);
        // dummy getters added in case of actually abstract property
        if (!prop->getters.empty() && !prop->getters[0]->TestAttr(Attribute::COMPILER_ADD)) {
            hasBody = true;
        }
    }

    // It can be checked only at parsed, because now no deffirence between modifier and attriubute
    if (hasBody && inCJMP && isAbstract) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_explicitly_abstract_can_not_have_body, member, memberKind);
    }

    bool checkedBefore = member.TestAttr(Attribute::FROM_COMMON_PART);
    // member of `common abstract` must be explicitly marked with common or abstract or have body
    if (inAbstractCJMP && !isCJMP && !isAbstract && !compilerAddedOrConstructor && !hasBody && !checkedBefore) {
        auto cjmpKind = cd.TestAttr(Attribute::COMMON) ? "common" : "platform";
        diag.DiagnoseRefactor(DiagKindRefactor::sema_cjmp_abstract_class_member_has_no_explicit_modifier, member,
            MakeRange(member.identifier), cjmpKind, memberKind, cjmpKind);
    }
}

void DeclAttributeChecker::CheckAttributesForPropAndFuncDeclInClass(const ClassDecl& cd, Decl& member) const
{
    if (member.astKind != ASTKind::PROP_DECL && member.astKind != ASTKind::FUNC_DECL) {
        return;
    }
    if (opts.compileCjd) {
        return;
    }
    auto type = (member.astKind == ASTKind::PROP_DECL) ? "property" : "function";
    // Member decl cannot be abstract when:
    //  1. member decl is static and not foreign;
    //  2. class decl is not abstract and not foreign;
    //  3. member decl can not be modified with 'open' in a non-inheritable object.
    //  4. member decl is not in a mirror
    bool invalidAbstract = member.TestAttr(Attribute::ABSTRACT) &&
        ((member.TestAttr(Attribute::STATIC) && !member.TestAttr(Attribute::FOREIGN)) ||
            (!cd.TestAttr(Attribute::ABSTRACT) && !cd.TestAttr(Attribute::FOREIGN)))
            && !member.TestAttr(Attribute::JAVA_MIRROR) && !cd.TestAttr(Attribute::OBJ_C_MIRROR);
    if (invalidAbstract && !member.TestAttr(Attribute::COMMON_WITH_DEFAULT)) {
        diag.Diagnose(member, DiagKind::sema_missing_func_body, type, member.identifier.Val());
    }
    if (member.TestAttr(Attribute::ABSTRACT) && cd.TestAttr(Attribute::ABSTRACT)) {
        if (!member.TestAttr(Attribute::PUBLIC) && !member.TestAttr(Attribute::PROTECTED)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_member_visibility_in_class, member,
                MakeRange(member.identifier), "abstract", type);
        }
    }

    CheckCJMPAttributesForPropAndFuncDeclInClass(cd, member);

    bool isInheritableClass = cd.TestAttr(Attribute::OPEN) || cd.TestAttr(Attribute::ABSTRACT);
    if (member.TestAttr(Attribute::OPEN)) {
        if (!isInheritableClass) {
            diag.Diagnose(member, DiagKind::sema_ignore_open);
        } else if (!member.TestAttr(Attribute::PUBLIC) && !member.TestAttr(Attribute::PROTECTED)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_member_visibility_in_class, member,
                MakeRange(member.identifier), "open", type);
        } else if (auto pd = DynamicCast<PropDecl*>(&member)) {
            // Inheriting open attribute to getter/setter to guarantee the virtual call of propDecl.
            auto setOpen = [](auto& it) {
                CJC_ASSERT(it);
                it->EnableAttr(Attribute::OPEN);
            };
            std::for_each(pd->setters.begin(), pd->setters.end(), setOpen);
            std::for_each(pd->getters.begin(), pd->getters.end(), setOpen);
        }
    }
    if (auto fd = DynamicCast<FuncDecl*>(&member); fd && fd->IsFinalizer() && isInheritableClass) {
        diag.Diagnose(*fd, DiagKind::sema_finalizer_forbidden_in_class, cd.identifier.Val(),
            cd.TestAttr(Attribute::OPEN) ? "open" : "abstract");
    }
}

void DeclAttributeChecker::CheckPropDeclAttributes(const PropDecl& pd) const
{
    if (pd.outerDecl && pd.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR)) {
        return;
    }

    if (pd.TestAnyAttr(Attribute::ABSTRACT, Attribute::JAVA_MIRROR) || opts.compileCjd) {
        return;
    }
    if (IsCommonWithoutDefault(pd)) {
        return;
    }
    if (pd.isVar) {
        if ((((!pd.TestAttr(Attribute::STATIC) && !pd.TestAttr(Attribute::OVERRIDE)) ||
            (pd.TestAttr(Attribute::STATIC) && !pd.TestAttr(Attribute::REDEF))) &&
            (pd.setters.empty() || pd.getters.empty())) ||
            (!pd.TestAttr(Attribute::ABSTRACT) && pd.setters.empty() && pd.getters.empty())) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_property_must_have_accessors, pd);
        }
    } else {
        if (!pd.setters.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_immutable_property_with_setter, pd);
        }
        if (pd.getters.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_property_must_have_accessors, pd);
        }
    }
}

/**
 * Report error for generic function in interfaces and abstract classes.
 * Report error for generic functions with open modifier in classes.
 * Report error for generic operator overloading functions.
 * Report error for generic functions with override modifier.
 * @param fd the function declaration to be checked.
 */
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void DeclAttributeChecker::CheckGenericFuncDeclAttributes(const FuncDecl& fd) const
{
    if (fd.funcBody == nullptr || fd.funcBody->generic == nullptr) {
        return;
    }
    if (fd.TestAttr(Attribute::OPERATOR)) {
        diag.Diagnose(fd, DiagKind::sema_generic_in_operator_overload);
    }
}
#endif

void DeclAttributeChecker::Check() const
{
    match(decl_)([this](ClassDecl& cd) { CheckClassAttribute(cd); },
        [this](InterfaceDecl& id) { CheckInterfaceAttribute(id); }, [this](EnumDecl& ed) { CheckEnumAttribute(ed); },
        [this](StructDecl& sd) { CheckStructAttribute(sd); }, [this](ExtendDecl& ed) { CheckExtendAttribute(ed); },
        [this](const PropDecl& pd) { CheckPropDeclAttributes(pd); },
        [this](const FuncDecl& fd) { CheckGenericFuncDeclAttributes(fd); }, []() {});
    if (decl_.TestAttr(Attribute::GLOBAL)) {
        SetDeclInternal(decl_);
    }
}

/**
 * Check and set attributes for decls.
 * NOTE: should be called after decl type resolved.
 */
void TypeChecker::TypeCheckerImpl::CheckAllDeclAttributes(const ASTContext& ctx)
{
    std::vector<Symbol*> syms = GetAllDecls(ctx);
    for (auto& sym : syms) {
        if (auto decl = AST::As<ASTKind::DECL>(sym->node)) {
            CJC_ASSERT(!decl->TestAttr(Attribute::IMPORTED) || decl->TestAttr(Attribute::TOOL_ADD) ||
                       decl->TestAttr(Attribute::FROM_COMMON_PART) || decl->TestAttr(Attribute::COMMON));
            DeclAttributeChecker(ci->invocation.globalOptions, diag, *decl).Check();
        }
    }
}
