// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the MergeCusAnno.
 */

#include "MergeAnnoFromCjd.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/PrintNode.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/Casting.h"
#include "cangjie/Utils/ICEUtil.h"
#include "cangjie/Utils/SafePointer.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
bool IsSameType(Ptr<Type> lt, Ptr<Ty> rty);
// Only extend's extendedType or class/struct/enum/interface/extend's inheritedType can be in.
bool IsSameType(Ptr<Type> lt, Ptr<Type> rt)
{
    CJC_ASSERT(Ty::IsTyCorrect(rt->ty));
    switch (lt->astKind) {
        case ASTKind::REF_TYPE: {
            auto lrt = StaticCast<RefType>(lt);
            if (rt->astKind != ASTKind::REF_TYPE) {
                return false;
            }
            auto rrtName = rt->ty->IsPrimitive() ? rt->ty->String() : rt->ty->name;
            if (lrt->ref.identifier.Val() != rrtName) {
                return false;
            }
            if (lrt->typeArguments.size() != rt->ty->typeArgs.size()) {
                return false;
            }
            for (size_t i = 0; i < lrt->typeArguments.size(); ++i) {
                if (!IsSameType(lrt->typeArguments[i].get(), rt->ty->typeArgs[i])) {
                    return false;
                }
            }
            break;
        }
        case ASTKind::QUALIFIED_TYPE: {
            auto lqt = StaticCast<QualifiedType>(lt);
            if (rt->astKind != ASTKind::QUALIFIED_TYPE) {
                return false;
            }
            auto rqt = StaticCast<QualifiedType>(rt);
            if (lqt->field.Val() != rqt->field.Val()) {
                return false;
            }
            if (!IsSameType(lqt->baseType.get(), rqt->baseType.get())) {
                return false;
            }
            break;
        }
        case ASTKind::OPTION_TYPE: {
            if (!IsSameType(lt, rt->ty)) {
                return false;
            }
            break;
        }
        case ASTKind::PRIMITIVE_TYPE: {
            auto lpt = StaticCast<PrimitiveType>(lt);
            auto lptName = lpt->str;
            auto rtyName = rt->ty->IsPrimitive() ? rt->ty->String() : rt->ty->name;
            lptName = lptName == "Rune" ? "UInt8" : lptName;
            rtyName = rtyName == "Rune" ? "UInt8" : rtyName;
            if (lptName != rtyName) {
                return false;
            }
            if (lpt->kind != rt->ty->kind) {
                return false;
            }
            break;
        }
        default:
            break;
    }
    return true;
}

bool IsSameType(Ptr<Type> lt, Ptr<Ty> rty)
{
    switch (lt->astKind) {
        case ASTKind::REF_TYPE: {
            auto lrt = StaticCast<RefType>(lt);
            auto rtyName = rty->IsPrimitive() ? rty->String() : rty->name;
            // If the identifier is the name after the type alias, the comparison will not succeed.
            if (lrt->ref.identifier.Val() != rtyName) {
                return false;
            }
            if (lrt->typeArguments.size() != rty->typeArgs.size()) {
                return false;
            }
            for (size_t i = 0; i < lrt->typeArguments.size(); ++i) {
                if (!IsSameType(lrt->typeArguments[i].get(), rty->typeArgs[i])) {
                    return false;
                }
            }
            break;
        }
        case ASTKind::QUALIFIED_TYPE: {
            auto lqt = StaticCast<QualifiedType>(lt);
            if (lqt->field.Val() != rty->name) {
                return false;
            }
            break;
        }
        case ASTKind::FUNC_TYPE: {
            if (!rty->IsFunc()) {
                return false;
            }
            auto lft = StaticCast<FuncType>(lt);
            auto rfty = StaticCast<FuncTy>(rty);
            if (lft->paramTypes.size() != rfty->paramTys.size()) {
                return false;
            }
            for (size_t i = 0; i < lft->paramTypes.size(); ++i) {
                if (!IsSameType(lft->paramTypes[i].get(), rfty->paramTys[i])) {
                    return false;
                }
            }
            if (!IsSameType(lft->retType.get(), rfty->retTy)) {
                return false;
            }
            break;
        }
        case ASTKind::TUPLE_TYPE: {
            if (!rty->IsTuple()) {
                return false;
            }
            auto ltt = StaticCast<TupleType>(lt);
            auto rtty = StaticCast<TupleTy>(rty);
            if (ltt->fieldTypes.size() != rtty->typeArgs.size()) {
                return false;
            }
            for (size_t i = 0; i < ltt->fieldTypes.size(); ++i) {
                if (!IsSameType(ltt->fieldTypes[i].get(), rtty->typeArgs[i])) {
                    return false;
                }
            }
            break;
        }
        case ASTKind::OPTION_TYPE: {
            if (!rty->IsCoreOptionType()) {
                return false;
            }
            auto lot = StaticCast<OptionType>(lt);
            auto roty = StaticCast<EnumTy>(rty);
            if (!IsSameType(lot->componentType.get(), roty->typeArgs[0])) {
                return false;
            }
            break;
        }
        case ASTKind::VARRAY_TYPE: {
            if (rty->kind != TypeKind::TYPE_VARRAY) {
                return false;
            }
            auto lat = StaticCast<VArrayType>(lt);
            auto rat = StaticCast<VArrayTy>(rty);
            if (!IsSameType(lat->typeArgument.get(), rat->typeArgs[0])) {
                return false;
            }
            auto lct = DynamicCast<ConstantType>(lat->constantType.get());
            CJC_NULLPTR_CHECK(lct);
            auto lce = DynamicCast<LitConstExpr>(lct->constantExpr.get());
            CJC_NULLPTR_CHECK(lce);
            if (lce->stringValue != std::to_string(rat->size)) {
                return false;
            }
            break;
        }
        case ASTKind::PRIMITIVE_TYPE: {
            if (!rty->IsPrimitive()) {
                return false;
            }
            auto lpt = StaticCast<PrimitiveType>(lt);
            auto rpty = StaticCast<PrimitiveTy>(rty);
            if (lpt->str != rpty->String()) {
                return false;
            }
            break;
        }
        default:
            break;
    }
    return true;
}

bool IsSameGeneric(Ptr<Generic> lg, Ptr<Generic> rg)
{
    if (lg->typeParameters.size() != rg->typeParameters.size()) {
        return false;
    }
    for (size_t i = 0; i < lg->typeParameters.size(); ++i) {
        if (lg->typeParameters[i]->identifier.Val() != rg->typeParameters[i]->identifier.Val()) {
            return false;
        }
    }
    // The extension declaration inserts the upper bound one more time.
    if (lg->genericConstraints.empty() != rg->genericConstraints.empty()) {
        return false;
    }
    if (lg->genericConstraints.size() > rg->genericConstraints.size()) {
        return false;
    }
    auto& lgc = lg->genericConstraints;
    auto& rgc = rg->genericConstraints;
    for (size_t i = 0; i < lg->genericConstraints.size(); ++i) {
        if (!IsSameType(lgc[i]->type.get(), rgc[i]->type->ty)) {
            return false;
        }
        if (lgc[i]->upperBounds.size() != rgc[i]->upperBounds.size()) {
            return false;
        }
        for (size_t j = 0; j < lgc[i]->upperBounds.size(); ++j) {
            if (!IsSameType(lgc[i]->upperBounds[j].get(), rgc[i]->upperBounds[j]->ty)) {
                return false;
            }
        }
    }
    return true;
}

bool IsSameFuncByIdentifier(Ptr<FuncBody> lb, Ptr<FuncBody> rb)
{
    if ((lb->generic == nullptr) != (rb->generic == nullptr)) {
        return false;
    }
    if (lb->generic && !IsSameGeneric(lb->generic.get(), rb->generic.get())) {
        return false;
    }
    if (lb->paramLists[0]->params.size() != rb->paramLists[0]->params.size()) {
        return false;
    }
    for (size_t i = 0; i < lb->paramLists[0]->params.size(); ++i) {
        if (lb->paramLists[0]->params[i]->identifier.Val() != rb->paramLists[0]->params[i]->identifier.Val()) {
            return false;
        }
        if (!IsSameType(lb->paramLists[0]->params[i]->type.get(), rb->paramLists[0]->params[i]->ty)) {
            return false;
        }
        CJC_ASSERT(rb->ty->IsFunc());
        auto lFuncTy = StaticCast<FuncTy>(rb->ty);
        if (lb->retType && !IsSameType(lb->retType.get(), lFuncTy->retTy)) {
            return false;
        }
    }
    return true;
}

bool IsSameDeclByIdentifier(Ptr<Decl> l, Ptr<Decl> r);

void ColllectPattern(Ptr<Pattern> pattern, std::unordered_map<Ptr<Decl>, Ptr<Decl>>& topDeclMapping,
    std::vector<OwnedPtr<Annotation>>& annos)
{
    switch (pattern->astKind) {
        case ASTKind::VAR_PATTERN: {
            auto vp = StaticCast<VarPattern>(pattern);
            for (auto& anno : annos) {
                vp->varDecl->annotations.emplace_back(ASTCloner::Clone<Annotation>(anno.get()));
            }
            topDeclMapping.emplace(vp->varDecl.get(), nullptr);
            break;
        }
        case ASTKind::TUPLE_PATTERN: {
            auto ltp = StaticCast<TuplePattern>(pattern);
            for (size_t i = 0; i < ltp->patterns.size(); ++i) {
                ColllectPattern(ltp->patterns[i].get(), topDeclMapping, annos);
            }
            break;
        }
        case ASTKind::ENUM_PATTERN: {
            auto lep = StaticCast<EnumPattern>(pattern);
            for (size_t i = 0; i < lep->patterns.size(); ++i) {
                ColllectPattern(lep->patterns[i].get(), topDeclMapping, annos);
            }
        }
        case ASTKind::VAR_OR_ENUM_PATTERN: {
            auto lvep = StaticCast<VarOrEnumPattern>(pattern);
            ColllectPattern(lvep->pattern.get(), topDeclMapping, annos);
            break;
        }
        case ASTKind::TYPE_PATTERN:
        case ASTKind::EXCEPT_TYPE_PATTERN:
        default:
            break;
    }
}

// NOTE: l from .cj.d, r from .cjo.
bool IsSameDeclByIdentifier(Ptr<Decl> l, Ptr<Decl> r)
{
    auto lId = l->astKind == ASTKind::PRIMARY_CTOR_DECL ? "init" : l->identifier.Val();
    auto lKind = l->astKind == ASTKind::PRIMARY_CTOR_DECL ? ASTKind::FUNC_DECL : l->astKind;
    bool fastQuit =
        lId != r->identifier.Val() || lKind != r->astKind || (l->generic == nullptr) != (r->generic == nullptr);
    if (fastQuit) {
        return false;
    }
    if (l->generic && !IsSameGeneric(l->generic.get(), r->generic.get())) {
        return false;
    }
    switch (l->astKind) {
        case ASTKind::FUNC_DECL: {
            auto lfd = StaticCast<FuncDecl>(l);
            auto rfd = StaticCast<FuncDecl>(r);
            if (!IsSameFuncByIdentifier(lfd->funcBody.get(), rfd->funcBody.get())) {
                return false;
            }
            break;
        }
        case ASTKind::PRIMARY_CTOR_DECL: {
            auto pcd = StaticCast<PrimaryCtorDecl>(l);
            auto fd = StaticCast<FuncDecl>(r);
            if (!IsSameFuncByIdentifier(pcd->funcBody.get(), fd->funcBody.get())) {
                return false;
            }
            break;
        }
        case ASTKind::EXTEND_DECL: {
            auto led = StaticCast<ExtendDecl>(l);
            auto red = StaticCast<ExtendDecl>(r);
            if (!IsSameType(led->extendedType.get(), red->extendedType.get())) {
                return false;
            }
            if (led->inheritedTypes.size() != red->inheritedTypes.size()) {
                return false;
            }
            for (size_t i = 0; i < led->inheritedTypes.size(); ++i) {
                if (!IsSameType(led->inheritedTypes[i].get(), red->inheritedTypes[i].get())) {
                    return false;
                }
            }
            break;
        }
        case ASTKind::MACRO_EXPAND_DECL: {
            auto lmed = StaticCast<MacroExpandDecl>(l);
            auto rmed = StaticCast<MacroExpandDecl>(r);
            if (lmed->invocation.fullName != rmed->invocation.fullName ||
                lmed->invocation.identifier != rmed->invocation.identifier) {
                return false;
            }
            break;
        }
        case ASTKind::VAR_DECL:
        case ASTKind::PROP_DECL: {
            auto lvd = StaticCast<VarDecl>(l);
            auto rvd = StaticCast<VarDecl>(r);
            if (lvd->type && !IsSameType(lvd->type.get(), rvd->ty)) {
                return false;
            }
            break;
        }
        default:
            break;
    }
    return true;
}

/**
 * 1) Type declarations (class, struct, enum, interface)
 * 2) Extension declarations
 * 3) Top-level function declarations
 * 4) Top-level variable declarations
 */
void MergeTopLevelDecl(
    Ptr<Package> target, Ptr<Package> source, std::unordered_map<Ptr<Decl>, Ptr<Decl>>& topDeclMapping)
{
    auto topDeclMapInsert = [&topDeclMapping](OwnedPtr<Decl>& toplevelDecl) {
        if (toplevelDecl->astKind == ASTKind::MAIN_DECL) {
            return;
        }
        if (auto vwpd = DynamicCast<VarWithPatternDecl>(toplevelDecl.get())) {
            ColllectPattern(vwpd->irrefutablePattern.get(), topDeclMapping, vwpd->annotations);
            return;
        }
        topDeclMapping.emplace(toplevelDecl, nullptr);
    };
    IterateToplevelDecls(*source, topDeclMapInsert);
    auto topDeclMapMatch = [&topDeclMapping](OwnedPtr<Decl>& toplevelDecl) {
        // Short circuit.
        if (toplevelDecl->astKind == ASTKind::BUILTIN_DECL || !toplevelDecl->IsExportedDecl()) {
            return;
        }
        auto found = std::find_if(topDeclMapping.begin(), topDeclMapping.end(), [&toplevelDecl](auto l) {
            if (l.second) {
                return false;
            }
            return IsSameDeclByIdentifier(l.first, toplevelDecl);
        });
        if (found == topDeclMapping.end()) {
            return;
        }
        found->second = toplevelDecl;
        for (auto& anno : found->first->annotations) {
            if (anno->kind != AnnotationKind::CUSTOM) {
                continue;
            }
            found->second->annotations.emplace_back(std::move(anno));
        }
        found->first->annotations.clear();
    };
    IterateToplevelDecls(*target, topDeclMapMatch);
}

/**
 * 1) Constructor declarations
 * 2) Member function declarations
 * 3) Member variable declarations
 * 4) Member property declarations
 * 5) Enum constructor declarations
 */
void MergeMemberDecl(std::pair<const Ptr<Decl>, Ptr<Decl>>& declPair)
{
    std::unordered_map<Ptr<Decl>, Ptr<Decl>> memberMapping;
    for (auto& member : declPair.first->GetMemberDeclPtrs()) {
        memberMapping.emplace(member, nullptr);
    }
    for (auto& member : declPair.second->GetMemberDeclPtrs()) {
        auto found = std::find_if(memberMapping.begin(), memberMapping.end(),
            [&member](auto l) { return IsSameDeclByIdentifier(l.first, member); });
        if (found == memberMapping.end()) {
            continue;
        }
        found->second = member;
        for (auto& anno : found->first->annotations) {
            if (anno->kind != AnnotationKind::CUSTOM) {
                continue;
            }
            found->second->annotations.emplace_back(std::move(anno));
        }
        found->first->annotations.clear();
    }
    // 3. Parameters in member functions/constructors
    for (auto memberPair : memberMapping) {
        if (!memberPair.second) {
            continue;
        }
        if (!memberPair.first->IsFunc()) {
            continue;
        }
        auto s = StaticCast<FuncDecl>(memberPair.first);
        auto t = StaticCast<FuncDecl>(memberPair.second);
        for (size_t i = 0; i < s->funcBody->paramLists[0]->params.size(); ++i) {
            for (auto& anno : s->funcBody->paramLists[0]->params[i]->annotations) {
                if (anno->kind != AnnotationKind::CUSTOM) {
                    continue;
                }
                t->funcBody->paramLists[0]->params[i]->annotations.emplace_back(std::move(anno));
            }
            s->funcBody->paramLists[0]->params[i]->annotations.clear();
        }
    }
}
} // namespace

void Cangjie::MergeCusAnno(Ptr<Package> target, Ptr<Package> source)
{
    // source to target, source from '.cj.d', target from '.cjo'.
    std::unordered_map<Ptr<Decl>, Ptr<Decl>> topDeclMapping;
    MergeTopLevelDecl(target, source, topDeclMapping);
    for (auto declPair : topDeclMapping) {
        if (!declPair.second) {
            continue;
        }
        MergeMemberDecl(declPair);
    }
}
