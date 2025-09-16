// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typecheck apis for Generic.
 */

#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace TypeCheckUtil;
using namespace AST;

void TypeChecker::TypeCheckerImpl::CheckUpperBound(ASTContext& ctx, const GenericConstraint& genericConstraint)
{
    // The generic constraint is as: type <: upperBound.
    // The upperBound must be class like decl or primitive type.
    CheckReferenceTypeLegality(ctx, *genericConstraint.type);
    for (auto& upperBoundPtr : genericConstraint.upperBounds) {
        auto upperBound = upperBoundPtr.get();
        if (upperBound == nullptr) {
            continue;
        }
        CheckReferenceTypeLegality(ctx, *upperBound);
    }
}

void TypeChecker::TypeCheckerImpl::CheckGenericConstraints(ASTContext& ctx, const Generic& generic)
{
    if (generic.genericConstraints.empty()) {
        return;
    }
    for (auto& genericConstraint : generic.genericConstraints) {
        CheckUpperBound(ctx, *genericConstraint);
        // Check if left value of generic constraints is in the typeParameters.
        bool found = false;
        if (genericConstraint->type == nullptr) {
            continue;
        }
        for (auto& typeParameter : generic.typeParameters) {
            if (genericConstraint->type->ref.identifier.Val() == typeParameter->identifier.Val()) {
                found = true;
                break;
            }
        }
        if (!found) {
            diag.Diagnose(*genericConstraint->type, DiagKind::sema_generics_type_variable_not_defined,
                genericConstraint->type->ref.identifier.Val().c_str());
        }
    }
}

bool TypeChecker::TypeCheckerImpl::HasIncompleteStaticFuncOrProp(
    const ASTContext& ctx, InheritableDecl& cd, std::vector<Ptr<Decl>>& staticMemberFuncsOrProps)
{
    if (!cd.TestAttr(Attribute::ABSTRACT) && cd.astKind != ASTKind::INTERFACE_DECL) {
        return false;
    }
    for (auto& decl : staticMemberFuncsOrProps) {
        CJC_NULLPTR_CHECK(decl);
        auto candidates = FieldLookup(ctx, &cd, decl->identifier, {.file = cd.curFile});
        auto found = std::find_if(candidates.begin(), candidates.end(), [this, decl](auto& candidate) -> bool {
            bool isImpl = false;
            if (auto srcFunc = DynamicCast<FuncDecl*>(candidate); srcFunc && decl->IsFunc()) {
                auto fd = RawStaticCast<FuncDecl*>(decl);
                isImpl = IsOverrideOrShadow(typeManager, *srcFunc, *fd) && !srcFunc->TestAttr(Attribute::ABSTRACT);
            } else if (auto srcProp = DynamicCast<PropDecl*>(candidate);
                       srcProp && decl->astKind == ASTKind::PROP_DECL) {
                auto pd = RawStaticCast<PropDecl*>(decl);
                isImpl = IsOverrideOrShadow(typeManager, *srcProp, *pd) && !srcProp->TestAttr(Attribute::ABSTRACT);
            }
            return isImpl;
        });
        if (found == candidates.end()) {
            return true;
        }
    }
    return false;
}

namespace {
void CollectStaticMember(const InheritableDecl& id, std::vector<Ptr<Decl>>& ret)
{
    for (auto& it : id.GetMemberDecls()) {
        if (it->IsFuncOrProp() && it->TestAttr(Attribute::STATIC)) {
            ret.push_back(it.get());
        }
    }
    // Look up parent interface.
    for (auto& inheritedType : id.inheritedTypes) {
        auto inherDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(inheritedType->ty);
        if (auto interfaceDecl = DynamicCast<InterfaceDecl*>(inherDecl); interfaceDecl) {
            CollectStaticMember(*interfaceDecl, ret);
        }
    }
}

inline std::unordered_map<Ptr<Ty>, size_t> GetTyArgsIndexMap(const std::vector<Ptr<Ty>>& tyArgs)
{
    std::unordered_map<Ptr<Ty>, size_t> indexMap;
    for (size_t i = 0; i < tyArgs.size(); ++i) {
        indexMap[tyArgs[i]] = i;
    }
    return indexMap;
}
} // namespace

bool TypeChecker::TypeCheckerImpl::CheckInstTyWithUpperbound(
    const ASTContext& ctx, TypeSubst& typeMapping, const NameReferenceExpr& expr)
{
    if (typeMapping.empty()) {
        return true; // Errors must be reported before.
    }
    std::unordered_map<Ptr<Ty>, size_t> indexMap = GetTyArgsIndexMap(expr.instTys);
    auto typeArgs = expr.GetTypeArgs();
    // If generic parameter is instantiated by a type that has unimplemented static function, including static funcion
    // in interface and abstract class.
    for (auto& it : typeMapping) {
        auto gTy = RawStaticCast<GenericsTy*>(it.first);
        bool isInstSatisfyConstraints = Utils::All(gTy->upperBounds, [this, &it, &typeMapping](auto& ub) {
            auto ubInst = typeManager.GetInstantiatedTy(ub, typeMapping);
            return typeManager.IsSubtype(it.second, ubInst);
        });
        if (!isInstSatisfyConstraints) {
            return true;
        }
        std::vector<Ptr<Decl>> staticMemberFuncsOrProps;
        // Collect all static functions in interface declarations.
        for (auto& upper : gTy->upperBounds) {
            if (auto decl = DynamicCast<InheritableDecl>(Ty::GetDeclPtrOfTy(upper))) {
                CollectStaticMember(*decl, staticMemberFuncsOrProps);
            }
        }
        // If there is no static member, no further check is required.
        if (staticMemberFuncsOrProps.empty()) {
            continue;
        }
        // If the generic argument is instantiated as an interface and the upper bound of the generic constraint is an
        // interface that contains static members, an error is reported directly.
        // Need to be modified after the default implementation of static functions in the interface is supported.
        auto iTy = DynamicCast<InterfaceTy*>(it.second);
        auto isInstByInterface = iTy != nullptr;
        if (isInstByInterface) {
            std::vector<Ptr<Decl>> staticMembers = {};
            CollectStaticMember(*iTy->decl, staticMembers);
            isInstByInterface = HasIncompleteStaticFuncOrProp(ctx, *iTy->decl, staticMembers);
        }
        auto isInstByNothing = it.second->IsNothing(); // The Nothing type does not have any members.
        if (isInstByInterface || isInstByNothing) {
            std::string typeString =
                isInstByNothing ? "'Nothing'" : "interface or abstract class '" + it.second->String() + "'";
            auto builder = diag.Diagnose(typeArgs.empty() ? StaticCast<Node>(expr) : *typeArgs[indexMap[it.second]],
                DiagKind::sema_cannot_instantiated_by_incomplete_type, gTy->String(), typeString);
            if (isInstByNothing) {
                builder.AddNote("'Nothing' type has no members");
            }
            return false;
        }
    }
    return true;
}

// This checking method is performed after sema type completed.
bool TypeChecker::TypeCheckerImpl::CheckInstTypeCompleteness(const ASTContext& ctx, const NameReferenceExpr& expr)
{
    auto target = TypeCheckUtil::GetRealTarget(expr.GetTarget());
    auto genericDecl = target ? (target->TestAttr(Attribute::CONSTRUCTOR) ? target->outerDecl : target) : nullptr;
    if (!genericDecl) {
        return true; // Errors must be reported before.
    }
    TypeSubst typeMapping = GenerateTypeMapping(*genericDecl, expr.instTys);
    if (!CheckInstTyWithUpperbound(ctx, typeMapping, expr)) {
        return false;
    }
    auto extends = typeManager.GetAllExtendsByTy(*genericDecl->ty);
    for (auto extend : extends) {
        TypeSubst extendMapping = GenerateTypeMapping(*extend, expr.instTys);
        if (!CheckInstTyWithUpperbound(ctx, extendMapping, expr)) {
            return false;
        }
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::CheckCallGenericDeclInstantiation(
    Ptr<const Decl> d, const std::vector<Ptr<AST::Type>>& typeArgs, const Expr& checkNode)
{
    if (!d) {
        return false;
    }
    std::vector<Ptr<Ty>> typeArgTys;
    Position diagPos;
    // RefExpr, MemberAccess 's typeArgTys maybe synthesized by type infer, which are saved in instTys of Expr Node.
    if (!typeArgs.empty()) {
        diagPos = (*typeArgs.begin())->begin;
    } else {
        diagPos = checkNode.begin;
    }
    typeArgTys = TypeCheckUtil::GetInstanationTys(checkNode);
    auto genericDecl = d->GetGeneric();
    if (!genericDecl || genericDecl->typeParameters.size() != typeArgTys.size()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_argument_no_match, checkNode, diagPos);
        return false;
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::CheckGenericDeclInstantiation(Ptr<const Decl> d,
    const std::variant<std::vector<Ptr<Type>>, std::vector<Ptr<Ty>>>& arguments, const Node& checkNode)
{
    size_t index = arguments.index();
    if (!d || !Ty::IsTyCorrect(d->ty) || index == std::variant_npos) {
        return false;
    }

    std::vector<Ptr<Ty>> typeArgs;
    std::vector<Ptr<Type>> typeNodes;
    bool isTypeNode = index == 0;
    if (isTypeNode) {
        typeNodes = std::get<0>(arguments);
        std::for_each(typeNodes.begin(), typeNodes.end(), [&typeArgs](auto it) { typeArgs.emplace_back(it->ty); });
    } else {
        typeArgs = std::get<1>(arguments);
    }
    auto genericParams = GetDeclTypeParams(*d);
    bool invalid = typeArgs.empty() || genericParams.size() != typeArgs.size();
    if (invalid) {
        auto range = MakeRange(checkNode.begin, checkNode.end.IsZero() ? checkNode.begin + 1 : checkNode.end);
        diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_argument_no_match, checkNode, range);
        return false;
    }
    TypeSubst  instantiateMap;
    if (auto ma = DynamicCast<const MemberAccess*>(&checkNode); ma && ma->baseExpr) {
        MultiTypeSubst instMap;
        // Collect typeMapping of baseExpr of member access, eg: A<T0>.foo<T1>.
        GenerateTypeMappingForBaseExpr(*ma, instMap);
        instantiateMap = MultiTypeSubstToTypeSubst(instMap);
    }
    // NOTE: member of extend with incompatible generic constraint will be filtered early by 'FilterTargetsInExtend'.
    auto genericDecl = d->GetGeneric();
    if (!genericDecl) {
        return true; // Extend of instantiated type.
    }
    auto typeMapping = GenerateTypeMapping(*d, typeArgs);
    instantiateMap.merge(typeMapping);
    std::unordered_map<Ptr<Ty>, size_t> indexMap = GetTyArgsIndexMap(typeArgs);
    // Check generic constraints.
    for (auto& gc : genericDecl->genericConstraints) {
        auto instTy = typeManager.GetInstantiatedTy(gc->type->ty, instantiateMap);
        if (!Ty::IsTyCorrect(instTy)) {
            return false;
        }
        if (auto gty = DynamicCast<GenericsTy>(instTy); gty && !gty->isUpperBoundLegal) {
            continue; // If instantiated ty is generic type with invalid upper bounds, do not report error.
        }
        for (const auto& upperBound : gc->upperBounds) {
            auto upperBoundTy = typeManager.GetInstantiatedTy(upperBound->ty, instantiateMap);
            if (!Ty::IsTyCorrect(upperBoundTy)) {
                return false;
            }
            bool isSameTyButCType = instTy == upperBoundTy && instTy->IsCType();
            if (!typeManager.IsSubtype(instTy, upperBoundTy, true, false) || isSameTyButCType) {
                auto& node = isTypeNode && !typeNodes[indexMap[instTy]]->TestAttr(Attribute::COMPILER_ADD)
                    ? *typeNodes[indexMap[instTy]]
                    : checkNode;
                diag.Diagnose(node, DiagKind::sema_generic_type_argument_not_match_constraint, d->ty->String())
                    .AddNote(*gc, DiagKind::sema_which_constraint_not_match, instTy->String(),
                             "'" + upperBoundTy->String() + "'");
                return false;
            }
        }
    }
    return true;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetGenericType(Decl& d, const std::vector<Ptr<Type>>& typeArgs)
{
    // For GenericParam check typeArgs
    if (auto gp = DynamicCast<GenericParamDecl*>(&d); gp) {
        if (!typeArgs.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_argument_no_match, *typeArgs[0]);
            return d.ty;
        }
    }

    auto generic = d.GetGeneric();
    if (!generic) {
        return d.ty;
    }
    if (typeArgs.size() != generic->typeParameters.size()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_argument_no_match, *typeArgs[0]);
        return d.ty;
    }
    // Build generic type mapping.
    TypeSubst typeMapping;
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        typeMapping[StaticCast<TyVar*>(generic->typeParameters[i]->ty)] = typeArgs[i]->ty;
    }
    // Instantiate the typeParameters of base function.
    return typeManager.GetInstantiatedTy(d.ty, typeMapping);
}

void TypeChecker::TypeCheckerImpl::CheckGenericExpr(Expr& expr)
{
    auto exprTarget = expr.GetTarget();
    auto realTarget = GetRealTarget(&expr, exprTarget);
    auto typeArgs = expr.GetTypeArgs();
    if (!realTarget || (typeArgs.empty() && TypeCheckUtil::GetInstanationTys(expr).empty())) {
        return;
    }
    if (exprTarget->astKind == ASTKind::TYPE_ALIAS_DECL) {
        std::vector<Ptr<Ty>> diffs = GetUnusedTysInTypeAlias(*StaticAs<ASTKind::TYPE_ALIAS_DECL>(exprTarget));
        Utils::EraseIf(typeArgs, [&diffs](auto type) { return Utils::In(type->ty, diffs); });
    }

    expr.ty = GetGenericType(*realTarget, typeArgs);
    if (!CheckGenericDeclInstantiation(realTarget, typeArgs, expr)) {
        expr.ty = TypeManager::GetInvalidTy();
        return;
    }
}

SubstPack TypeChecker::TypeCheckerImpl::GenerateGenericTypeMapping(const ASTContext& ctx, const Expr& expr)
{
    SubstPack typeMapping;
    // Generate typeMapping by given expression node.
    if (auto ma = DynamicCast<const MemberAccess*>(&expr); ma) {
        if (auto target = ma->GetTarget(); target && ma->isExposedAccess && IsGenericUpperBoundCall(expr, *target)) {
            typeManager.GenerateTypeMappingForUpperBounds(typeMapping, *ma, *target);
        } else {
            GenerateTypeMappingForBaseExpr(*ma, typeMapping);
        }
    }
    if (auto re = DynamicCast<const RefExpr*>(&expr); re) {
        auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, re->scopeName);
        if (sym && sym->node->IsNominalDecl()) { // Symbol guarantees sym->node not null.
            // Sema ty of structure declaration should be set in PreCheck stage.
            if (!Ty::IsTyCorrect(sym->node->ty)) {
                return typeMapping;
            }
            typeManager.GenerateGenericMapping(typeMapping, *sym->node->ty);
        }
    }
    return typeMapping;
}
