// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/IncrementalCompilation/Utils.h"

namespace Cangjie::IncrementalCompilation {
using namespace Cangjie::AST;
void PrintDecl(std::ostream& out, const Decl& decl)
{
    if (!decl.identifier.Empty()) {
        out << decl.identifier.Val() << ' ';
    }
    out << decl.rawMangleName << ' ';
    if (decl.curFile) {
        out << decl.curFile->fileName << ' ';
    }
    if (!decl.begin.IsZero()) {
        out << decl.begin.line;
    }
}

bool IsVirtual(const Decl& decl)
{
    if (IsImported(decl)) {
        return decl.TestAttr(Attribute::OPEN) || decl.TestAttr(Attribute::ABSTRACT);
    }
    if (!decl.outerDecl) {
        return false;
    }
    if (decl.outerDecl->astKind != ASTKind::CLASS_DECL &&
        decl.outerDecl->astKind != ASTKind::INTERFACE_DECL) {
        return false;
    }
    if (decl.outerDecl->astKind == ASTKind::INTERFACE_DECL && !decl.TestAttr(Attribute::STATIC)) {
        return true;
    }
    if (decl.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::ENUM_CONSTRUCTOR)) {
        return false;
    }
    if (decl.outerDecl->TestAttr(Attribute::ABSTRACT) && !decl.TestAttr(Attribute::STATIC)) {
        if (auto f = DynamicCast<FuncDecl*>(&decl); f && !f->funcBody->body) {
            return true;
        }
        if (auto p = DynamicCast<PropDecl*>(&decl); p && p->getters.empty() && p->setters.empty()) {
            return true;
        }
    }
    return decl.TestAttr(Attribute::OPEN) || decl.TestAttr(Attribute::ABSTRACT);
}

bool IsTyped(const Decl& decl)
{
    if (decl.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        return true;
    }
    if (auto patternDecl = DynamicCast<const VarWithPatternDecl*>(&decl)) {
        return patternDecl->type || IsImported(*patternDecl);
    }
    if (auto varDecl = DynamicCast<const VarDecl*>(&decl)) {
        if (varDecl->outerDecl) {
            if (auto pat = DynamicCast<const VarWithPatternDecl*>(varDecl->outerDecl)) {
                return IsTyped(*pat);
            }
        }
        return varDecl->type || IsImported(*varDecl);
    }
    if (decl.astKind == ASTKind::FUNC_DECL) {
        auto& funcDecl = static_cast<const FuncDecl&>(decl);
        return funcDecl.funcBody->retType || funcDecl.isGetter || funcDecl.isSetter ||
            funcDecl.TestAttr(Attribute::CONSTRUCTOR) || funcDecl.IsFinalizer();
    }
    return true;
}

std::vector<Ptr<Decl>> GetMembers(const Decl& decl)
{
    if (auto p = DynamicCast<EnumDecl*>(&decl)) {
        std::vector<Ptr<Decl>> res(p->members.size());
        for (size_t i{0}; i < res.size(); ++i) {
            res[i] = p->members[i].get();
        }
        return res;
    }
    if (auto p = DynamicCast<PropDecl*>(&decl)) {
        std::vector<Ptr<Decl>> res{};
        for (auto& getter : p->getters) {
            res.push_back(getter.get());
        }
        for (auto& setter : p->setters) {
            res.push_back(setter.get());
        }
        return res;
    }
    if (auto vp = DynamicCast<VarWithPatternDecl*>(&decl)) {
        std::vector<Ptr<Decl>> res{};
        Walker(vp->irrefutablePattern.get(), [&res](auto node) {
            if (auto varDecl = DynamicCast<VarDecl*>(node)) {
                res.push_back(varDecl);
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        return res;
    }
    return decl.GetMemberDeclPtrs();
}

bool IsOOEAffectedDecl(const Decl& decl)
{
    CJC_ASSERT(!decl.TestAttr(Attribute::IMPORTED) || decl.TestAttr(Attribute::FROM_COMMON_PART));
    // variable with initialiser or function with body can be affected by order. Other decls do not contain
    // expression and therefore cannot be affected by order.
    if (auto var = DynamicCast<VarDeclAbstract>(&decl)) {
        return var->initializer;
    }
    if (auto func = DynamicCast<FuncDecl>(&decl)) {
        return func->funcBody->body;
    }
    if (decl.astKind == ASTKind::MACRO_DECL || decl.astKind == ASTKind::MAIN_DECL ||
        decl.astKind == ASTKind::PRIMARY_CTOR_DECL) {
        return true;
    }
    return false;
}
} // namespace Cangjie::IncrementalCompilation
