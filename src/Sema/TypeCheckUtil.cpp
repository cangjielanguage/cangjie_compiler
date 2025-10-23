// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the utility functions for TypeCheck.
 */

#include "TypeCheckUtil.h"
#include "Promotion.h"

#include <map>
#include <set>

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/ScopeManagerApi.h"
#include "cangjie/AST/Symbol.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"

namespace Cangjie::TypeCheckUtil {
using namespace AST;
namespace {
const std::set<std::string> BUILTIN_OPERATORS = {"@", ".", "[]", "()", "++", "--", "?", "!", "-", "**", "*", "/", "%",
    "+", "<<", ">>", "<", "<=", ">", ">=", "is", "as", "==", "!=", "&", "^", "|", "..", "..=", "&&", "||", "??", "~>",
    "=", "**=", "*=", "/=", "%/", "+=", "-=", "<<=", ">>=", "&=", "^=", "|="};
} // namespace

std::vector<TypeKind> GetIdealTypesByKind(TypeKind type)
{
    if (type == TypeKind::TYPE_IDEAL_INT) {
        return {TypeKind::TYPE_INT8, TypeKind::TYPE_INT16, TypeKind::TYPE_INT32, TypeKind::TYPE_INT_NATIVE,
            TypeKind::TYPE_INT64, TypeKind::TYPE_UINT8, TypeKind::TYPE_UINT16, TypeKind::TYPE_UINT32,
            TypeKind::TYPE_UINT64, TypeKind::TYPE_UINT_NATIVE};
    } else if (type == TypeKind::TYPE_IDEAL_FLOAT) {
        return {TypeKind::TYPE_FLOAT16, TypeKind::TYPE_FLOAT32, TypeKind::TYPE_FLOAT64};
    }
    return {};
}

void UpdateInstTysWithTypeArgs(NameReferenceExpr& expr)
{
    if (!expr.instTys.empty()) {
        return;
    }
    auto typeArgs = expr.GetTypeArgs();
    // Do not update instTys for partial generic typealias case which has intersection type.
    if (HasIntersectionTy(typeArgs)) {
        return;
    }
    for (auto& typeArg : typeArgs) {
        (void)expr.instTys.emplace_back(typeArg->ty);
    }
}

void SetIsNotAlone(Expr& baseExpr)
{
    if (auto nre = DynamicCast<NameReferenceExpr*>(&baseExpr); nre) {
        nre->isAlone = false;
    }
}

bool HasIntersectionTy(const std::vector<Ptr<Type>>& types)
{
    return std::any_of(
        types.begin(), types.end(), [](auto& type) { return type->ty && type->ty->HasIntersectionTy(); });
}

bool NeedFurtherInstantiation(const std::vector<Ptr<Type>>& types)
{
    return types.empty() || HasIntersectionTy(types);
}

void ModifyTargetOfRef(RefExpr& re, Ptr<Decl> decl, const std::vector<Ptr<Decl>>& targets)
{
    ReplaceTarget(&re, decl);
    // If the target of refExpr is FuncDecl, it should not be the real target of RefExpr.
    // The real target will be determined by resolvedFunction in the typecheck of CallExpr.
    re.ref.targets.clear();
    for (auto& it : targets) {
        re.ref.targets.push_back(it);
    }
}

void AddFuncTargetsForMemberAccess(MemberAccess& ma, const std::vector<Ptr<Decl>>& targets)
{
    ma.targets.clear();
    for (auto& decl : targets) {
        if (!decl || (!ma.isPattern && decl->astKind != ASTKind::FUNC_DECL)) {
            continue;
        }
        auto funcDecl = RawStaticCast<FuncDecl*>(decl);
        ma.targets.push_back(funcDecl);
    }
}

void ReplaceTarget(Ptr<Node> node, Ptr<Decl> target, bool insertTarget)
{
    if (target == nullptr && (!node->ty || node->ty->IsNothing())) {
        node->ty = TypeManager::GetInvalidTy();
    }
    auto aliasDecl = As<ASTKind::TYPE_ALIAS_DECL>(target);
    switch (node->astKind) {
        case ASTKind::REF_EXPR: {
            auto re = StaticAs<ASTKind::REF_EXPR>(node);
            re->ref.target = target;
            // Update type alias decl or clear the target.
            if (aliasDecl || !target) {
                re->aliasTarget = aliasDecl;
            }
            break;
        }
        case ASTKind::MEMBER_ACCESS: {
            auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(node);
            ma->target = target;
            // Update type alias decl or clear the target.
            if (aliasDecl || !target) {
                ma->aliasTarget = aliasDecl;
            }
            break;
        }
        case ASTKind::REF_TYPE: {
            auto rt = StaticAs<ASTKind::REF_TYPE>(node);
            rt->ref.target = target;
            break;
        }
        case ASTKind::QUALIFIED_TYPE: {
            auto qt = StaticAs<ASTKind::QUALIFIED_TYPE>(node);
            qt->target = target;
            break;
        }
        case ASTKind::MACRO_EXPAND_EXPR: {
            auto mee = RawStaticCast<MacroExpandExpr*>(node);
            mee->invocation.target = target;
            break;
        }
        case ASTKind::MACRO_EXPAND_DECL: {
            auto med = RawStaticCast<MacroExpandDecl*>(node);
            med->invocation.target = target;
            break;
        }
        case ASTKind::MACRO_EXPAND_PARAM: {
            auto mep = RawStaticCast<MacroExpandParam*>(node);
            mep->invocation.target = target;
            break;
        }
        default:
            break;
    }
    if (node->symbol) {
        node->symbol->UnbindTarget();
        if (insertTarget && node->begin.fileID != 0) {
            node->symbol->target = target;
        }
    }
}

bool IsFuncReturnThisType(const FuncDecl& fd)
{
    return fd.funcBody && fd.funcBody->retType && fd.funcBody->retType->astKind == ASTKind::REF_TYPE &&
        StaticAs<ASTKind::REF_TYPE>(fd.funcBody->retType.get())->ref.identifier == "This";
}

bool CheckThisTypeCompatibility(const FuncDecl& parentFunc, const FuncDecl& childFunc)
{
    // In Class, when a function in child class has overridden relation with the function in parent class,
    // 1. If the return type of parent function is 'This', the return type of the child function must be 'This';
    // 2. If the return type of parent function is not 'This', the return type of the child function can be
    // any other type which is the subtype of the return type of parent function.
    return !IsFuncReturnThisType(parentFunc) || IsFuncReturnThisType(childFunc);
}

bool HasMainDecl(Package& pkg)
{
    bool hasMain = false;
    Walker(&pkg, [&hasMain](auto node) {
        if (auto decl = DynamicCast<Decl*>(node); decl) {
            if (decl->astKind == ASTKind::MAIN_DECL) {
                hasMain = true;
                return VisitAction::STOP_NOW;
            }
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return hasMain;
}

void MarkParamWithInitialValue(Node& root)
{
    auto setFunc = [](Ptr<Node> node) -> VisitAction {
        if (auto fp = DynamicCast<FuncParam*>(node); fp && fp->assignment) {
            node->EnableAttr(Attribute::HAS_INITIAL); // Set initial mark to param which has initial value.
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&root, setFunc).Walk();
}

bool IsOverloadableOperator(TokenKind op)
{
    static const std::unordered_set<TokenKind> overloadableOperators = {
        TokenKind::ADD,
        TokenKind::BITAND,
        TokenKind::BITOR,
        TokenKind::BITXOR,
        TokenKind::DIV,
        TokenKind::EQUAL,
        TokenKind::EXP,
        TokenKind::GE,
        TokenKind::GT,
        TokenKind::LE,
        TokenKind::LSHIFT,
        TokenKind::LSQUARE,
        TokenKind::LT,
        TokenKind::MOD,
        TokenKind::MUL,
        TokenKind::NOT,
        TokenKind::NOTEQ,
        TokenKind::RSHIFT,
        TokenKind::SUB,
    };
    return overloadableOperators.find(op) != overloadableOperators.end();
}

bool CanSkipDiag(const Node& node)
{
    return !Ty::IsTyCorrect(node.ty);
}

bool IsFieldOperator(const std::string& field)
{
    return Utils::In(field, BUILTIN_OPERATORS);
}

std::vector<Ptr<Ty>> GetParamTys(const FuncDecl& fd)
{
    if (fd.TestAttr(Attribute::IMPORTED) && Ty::IsTyCorrect(fd.ty) && fd.ty->IsFunc()) {
        return RawStaticCast<FuncTy*>(fd.ty)->paramTys;
    }
    CJC_NULLPTR_CHECK(fd.funcBody);
    return GetFuncBodyParamTys(*fd.funcBody);
}

std::vector<Ptr<Ty>> GetFuncBodyParamTys(const FuncBody& fb)
{
    if (fb.paramLists.empty()) {
        return {};
    }
    std::vector<Ptr<Ty>> ret;
    for (auto& param : fb.paramLists[0].get()->params) {
        if (param->type) {
            param->ty = param->type->ty;
        }
        ret.emplace_back(param->ty ? param->ty : TypeManager::GetInvalidTy());
    }
    return ret;
}

// Generate type mapping for src is an override or implement of target.
MultiTypeSubst GenerateTypeMappingBetweenFuncs(TypeManager& typeManager, const FuncDecl& src, const FuncDecl& target)
{
    MultiTypeSubst typeMapping;
    if (src.outerDecl && Ty::IsTyCorrect(src.outerDecl->ty)) {
        typeMapping = typeManager.GenerateStructDeclTypeMapping(*src.outerDecl);
    }
    if (target.TestAttr(Attribute::GENERIC) && src.TestAttr(Attribute::GENERIC)) {
        // Solve generic function (eg:`func foo<T>(arr: A<T>): Unit`) 's type identical check.
        MergeTypeSubstToMultiTypeSubst(typeMapping, typeManager.GenerateGenericMappingFromGeneric(target, src));
    }
    return typeMapping;
}

// Check if src is an override or implement of target. DO NOT call 'Synthesize'.
bool IsOverrideOrShadow(TypeManager& typeManager, const FuncDecl& src, const FuncDecl& target, const Ptr<Ty> baseTy,
    const Ptr<AST::Ty> expectInstParent)
{
    if (auto ret = typeManager.GetOverrideCache(&src, &target, baseTy, expectInstParent); ret.has_value()) {
        return ret.value();
    }
    auto srcFt = DynamicCast<FuncTy*>(src.ty);
    auto targetFt = DynamicCast<FuncTy*>(target.ty);
    MultiTypeSubst mts;
    if (expectInstParent) {
        CJC_ASSERT(src.outerDecl && Is<InheritableDecl>(src.outerDecl));
        MergeTypeSubstToMultiTypeSubst(mts, GenerateTypeMappingByTy(target.outerDecl->ty, expectInstParent));
        auto substituteToParent = Promotion(typeManager).Promote(*src.outerDecl->ty, *target.outerDecl->ty);
        for (auto p : substituteToParent) {
            MergeTypeSubstToMultiTypeSubst(mts, GenerateTypeMappingByTy(p, expectInstParent));
        }
        if (target.TestAttr(Attribute::GENERIC) && src.TestAttr(Attribute::GENERIC)) {
            MergeTypeSubstToMultiTypeSubst(mts, typeManager.GenerateGenericMappingFromGeneric(target, src));
        }
    } else {
        mts = GenerateTypeMappingBetweenFuncs(typeManager, src, target);
    }
    std::set<TypeSubst> typeMappings = ExpandMultiTypeSubst(mts, {srcFt, targetFt});
    for (auto typeMapping : typeMappings) {
        auto srcParamTys = srcFt ? srcFt->paramTys : GetParamTys(src);
        auto targetParamTys = targetFt ? targetFt->paramTys : GetParamTys(target);
        if (srcParamTys.size() != targetParamTys.size()) {
            typeManager.AddOverrideCache(src, target, baseTy, expectInstParent, false);
            return false;
        }
        // Only generate typeMapping by base type, if functions' outerDecls are irrelevant.
        // eg: interface I1 { func foo(): Int64 }, interface I2 { func foo(): Int64 }
        // class/interface Type3 <: I1 & I2 { func foo() : Int64 {0}}
        if (Ty::IsTyCorrect(baseTy) && Is<InterfaceDecl>(src.outerDecl) && Is<InterfaceDecl>(target.outerDecl)) {
            auto parentStructTy = typeManager.GetInstantiatedTy(target.outerDecl->ty, typeMapping);
            auto childStructTy = typeManager.GetInstantiatedTy(src.outerDecl->ty, typeMapping);
            if (!typeManager.IsSubtype(childStructTy, parentStructTy)) {
                MultiTypeSubst m;
                typeManager.GenerateGenericMapping(m, *baseTy);
                typeMapping.merge(MultiTypeSubstToTypeSubst(m));
            }
        }
        // Instantiated parameter's types.
        for (auto& it : srcParamTys) {
            it = typeManager.GetInstantiatedTy(it, typeMapping);
        }
        for (auto& it : targetParamTys) {
            it = typeManager.GetInstantiatedTy(it, typeMapping);
        }
        if (typeManager.IsFuncParameterTypesIdentical(srcParamTys, targetParamTys)) {
            bool isCrossPlatform =
                (src.TestAttr(AST::Attribute::COMMON) && target.TestAttr(AST::Attribute::PLATFORM)) ||
                (src.TestAttr(AST::Attribute::PLATFORM) && target.TestAttr(AST::Attribute::COMMON));
            if (isCrossPlatform) {
                continue;
            }
            typeManager.AddOverrideCache(src, target, baseTy, expectInstParent, true);
            return true;
        }
    }
    typeManager.AddOverrideCache(src, target, baseTy, expectInstParent, false);
    return false;
}

// Check if src is an override or implement of target. DO NOT call 'Synthesize'.
bool IsOverrideOrShadow(TypeManager& typeManager, const PropDecl& src, const PropDecl& target, Ptr<Ty> baseTy)
{
    CJC_ASSERT(src.outerDecl);
    MultiTypeSubst mts = typeManager.GenerateStructDeclTypeMapping(*src.outerDecl);
    auto typeMapping = MultiTypeSubstToTypeSubst(mts);
    // Only generate typeMapping by base type, if functions' outerDecls are irrelevant.
    // eg: interface I1 { func foo() : Int64 }, interface I2 { func foo() : Int64}
    // class/interface Type3 <: I1&I2 { func foo() : Int64 {0}}
    if (Ty::IsTyCorrect(baseTy) && Is<InterfaceDecl>(src.outerDecl) && Is<InterfaceDecl>(target.outerDecl)) {
        auto parentStructTy = typeManager.GetInstantiatedTy(target.outerDecl->ty, typeMapping);
        auto childStructTy = typeManager.GetInstantiatedTy(src.outerDecl->ty, typeMapping);
        if (!typeManager.IsSubtype(childStructTy, parentStructTy)) {
            typeManager.GenerateGenericMapping(mts, *baseTy);
            typeMapping.merge(MultiTypeSubstToTypeSubst(mts));
        }
    }
    auto srcTy = src.type ? src.type->ty : src.ty;
    auto targetTy = target.type ? target.type->ty : target.ty;
    bool ret = srcTy == typeManager.GetInstantiatedTy(targetTy, typeMapping);
    if (ret && src.outerDecl == Ty::GetDeclPtrOfTy(baseTy)) {
        typeManager.UpdateTopOverriddenFuncDeclCache(&src, &target);
    }
    return ret;
}

// Check where expr is memberAccess calling interface's member.
bool IsGenericUpperBoundCall(const Expr& expr, Decl& target)
{
    auto ma = DynamicCast<const MemberAccess*>(&expr);
    bool isNotGenericCall =
        !ma || !ma->baseExpr || !ma->baseExpr->ty || !ma->baseExpr->ty->IsGeneric() || !target.outerDecl;
    if (isNotGenericCall) {
        return false;
    }
    auto found = ma->foundUpperBoundMap.find(&target);
    return found != ma->foundUpperBoundMap.end() && !found->second.empty();
}

bool IsNode1ScopeVisibleForNode2(const Node& node1, const Node& node2)
{
    auto scopeName1 = ScopeManagerApi::GetScopeNameWithoutTail(node1.scopeName);
    auto scopeName2 = ScopeManagerApi::GetScopeNameWithoutTail(node2.scopeName);
    bool isScopeVisible = scopeName2.rfind(scopeName1, 0) == 0;
    return isScopeVisible;
}

Ptr<Decl> GetRealTarget(Ptr<Decl> decl)
{
    auto target = decl;
    if (auto aliasDecl = DynamicCast<TypeAliasDecl*>(target);
        aliasDecl && !target->TestAttr(Attribute::IN_REFERENCE_CYCLE) && aliasDecl->type) {
        auto realTarget = aliasDecl->type->GetTarget();
        // It is possible that existing empty realTarget, eg: typealias of primitive type.
        // And it's also possible to existing typealias of another aliasdecl.
        target = realTarget ? GetRealTarget(realTarget) : target;
    }
    return target;
}

std::pair<bool, Ptr<Decl>> GetRealMemberDecl(Decl& decl)
{
    if (auto fd = DynamicCast<FuncDecl*>(&decl); fd && fd->propDecl) {
        return {fd->isGetter, fd->propDecl};
    }
    return {false, &decl};
}

Ptr<Decl> GetUsedMemberDecl(Decl& decl, bool isGetter)
{
    if (auto pd = DynamicCast<PropDecl*>(&decl); pd) {
        // If target is prop decl, return getter/setter func.
        auto& funcs = isGetter ? pd->getters : pd->setters;
        // Spec allows only implement prop's getter or setter
        // for the interface property which have default implementation.
        return funcs.empty() ? RawStaticCast<Decl*>(pd) : funcs[0].get();
    }
    return &decl;
}

static const std::unordered_map<ASTKind, std::string> DECL2STRMAP = {
    {ASTKind::CLASS_DECL, "class"},
    {ASTKind::ENUM_DECL, "enum"},
    {ASTKind::EXTEND_DECL, "extend"},
    {ASTKind::FUNC_DECL, "function"},
    {ASTKind::FUNC_PARAM, "parameter"},
    {ASTKind::INTERFACE_DECL, "interface"},
    {ASTKind::MACRO_DECL, "macro"},
    {ASTKind::MAIN_DECL, "main"},
    {ASTKind::PACKAGE_DECL, "package"},
    {ASTKind::PRIMARY_CTOR_DECL, "primary constructor"},
    {ASTKind::PROP_DECL, "property"},
    {ASTKind::STRUCT_DECL, "struct"},
    {ASTKind::TYPE_ALIAS_DECL, "type alias"},
    {ASTKind::VAR_DECL, "variable"},
    {ASTKind::VAR_WITH_PATTERN_DECL, "variable"},
};

std::string DeclKindToString(const Decl& decl)
{
    auto it = DECL2STRMAP.find(decl.astKind);
    if (it == DECL2STRMAP.end()) {
        return decl.identifier;
    }
    return it->second;
}

std::string GetTypesStr(std::vector<Ptr<AST::Decl>>& decls)
{
    std::string res;
    std::unordered_set<std::string> typeSetCache;
    for (auto it : decls) {
        if (it == nullptr) {
            continue;
        }
        auto str = AST::ASTKIND_TO_STRING_MAP[it->astKind];
        if (typeSetCache.find(str) != typeSetCache.end()) {
            continue;
        }
        typeSetCache.emplace(str);
        res += str + " ";
    }
    return res;
}

namespace {
Ptr<FuncDecl> FindValidPropAccessor(ClassDecl& cd, bool isGetter, const std::string& name)
{
    auto curClass = &cd;
    while (curClass != nullptr) {
        for (auto& it : curClass->GetMemberDecls()) {
            CJC_ASSERT(it);
            if (it->identifier != name) {
                continue;
            }
            auto found = GetUsedMemberDecl(*it, isGetter);
            CJC_ASSERT(found);
            if (found->astKind == ASTKind::FUNC_DECL) {
                return RawStaticCast<FuncDecl*>(found);
            }
        }
        curClass = curClass->GetSuperClassDecl();
    };
    return nullptr;
}
} // namespace

/**
 * Since spec support 'var' propDecl to inherit parent's getter/setter separately that
 * child can only override one of getter/setter.
 * We need to find getter/setter from current class or parent class.
 * return (getter, setter)
 */
std::pair<Ptr<FuncDecl>, Ptr<FuncDecl>> GetUsableGetterSetterForProperty(PropDecl& pd)
{
    return std::make_pair(GetUsableGetterForProperty(pd), GetUsableSetterForProperty(pd));
}

// Returns getter for property
Ptr<FuncDecl> GetUsableGetterForProperty(PropDecl& pd)
{
    Ptr<FuncDecl> getter = nullptr;
    auto cd = DynamicCast<ClassDecl*>(pd.outerDecl);
    if (pd.getters.empty()) {
        if (cd) {
            getter = FindValidPropAccessor(*cd, true, pd.identifier);
        }
    } else {
        getter = pd.getters[0].get();
    }
    return getter;
}

// Returns setter for mutable property
Ptr<FuncDecl> GetUsableSetterForProperty(PropDecl& pd)
{
    CJC_ASSERT(pd.isVar);
    Ptr<FuncDecl> setter = nullptr;
    auto cd = DynamicCast<ClassDecl*>(pd.outerDecl);
    if (pd.setters.empty()) {
        if (cd) {
            setter = FindValidPropAccessor(*cd, false, pd.identifier);
        }
    } else {
        setter = pd.setters[0].get();
    }
    return setter;
}

std::set<Ptr<ExtendDecl>> CollectAllRelatedExtends(TypeManager& tyMgr, InheritableDecl& boxedDecl)
{
    if (boxedDecl.astKind != ASTKind::CLASS_DECL) {
        return tyMgr.GetDeclExtends(boxedDecl);
    }

    std::set<Ptr<ExtendDecl>> allExtends;
    auto curClass = StaticAs<ASTKind::CLASS_DECL>(&boxedDecl);
    do {
        allExtends.merge(tyMgr.GetDeclExtends(*curClass));
        curClass = curClass->GetSuperClassDecl();
    } while (curClass != nullptr);
    return allExtends;
}

size_t CountOptionNestedLevel(const Ty& ty)
{
    size_t level = 0;
    Ptr<const Ty> currentTy = &ty;
    while (currentTy->IsCoreOptionType()) {
        CJC_ASSERT(currentTy->typeArgs.size() == 1);
        CJC_NULLPTR_CHECK(currentTy->typeArgs.front());
        currentTy = currentTy->typeArgs.front();
        level++;
    }
    return level;
}

Ptr<Ty> UnboxOptionType(Ptr<Ty> ty)
{
    Ptr<Ty> optionUnboxTy = ty;
    // Option type allow type auto box.
    while (Ty::IsTyCorrect(optionUnboxTy) && optionUnboxTy->IsCoreOptionType()) {
        // CoreOptionType test guarantees that typeArgs.size == 1.
        optionUnboxTy = optionUnboxTy->typeArgs[0];
    }
    return optionUnboxTy;
}

std::string GetFullInheritedTy(ExtendDecl& extend)
{
    std::string fullType = PosSearchApi::PosToStr(extend.begin);
    for (auto& interface : extend.inheritedTypes) {
        fullType += interface->ty->String();
    }
    return fullType;
}

std::vector<Ptr<FuncDecl>> GetFuncTargets(const Node& node)
{
    switch (node.astKind) {
        case ASTKind::REF_EXPR: {
            std::vector<Ptr<FuncDecl>> funcTargets;
            auto refTargets = StaticCast<const RefExpr&>(node).ref.targets;
            for (auto& it : refTargets) {
                if (auto fd = DynamicCast<FuncDecl*>(it)) {
                    funcTargets.push_back(fd);
                }
            }
            return funcTargets;
        }
        case ASTKind::MEMBER_ACCESS: {
            return StaticCast<const MemberAccess&>(node).targets;
        }
        default:
            return {};
    }
}

Ptr<const Modifier> FindModifier(const Decl& d, TokenKind kind)
{
    Ptr<const Modifier> mod = nullptr;
    for (auto& modifier : d.modifiers) {
        if (modifier.modifier == kind) {
            mod = &modifier;
        }
    }
    return mod;
}

void AddArrayLitConstructor(ArrayLit& al)
{
    auto decl = Ty::GetDeclPtrOfTy(al.ty);
    if (!decl) {
        return;
    }
    for (auto it : decl->GetMemberDeclPtrs()) {
        if (auto fd = DynamicCast<FuncDecl*>(it); fd && IsInstanceConstructor(*fd) && fd->funcBody) {
            // Constructor used for 'ArrayLit' has 3 params.
            if (fd->funcBody->paramLists.empty() || fd->funcBody->paramLists[0]->params.size() != 3) {
                continue;
            }
            auto firstParamTy = fd->funcBody->paramLists[0]->params[0]->ty;
            if (Ty::IsTyCorrect(firstParamTy) && firstParamTy->IsArray()) {
                al.initFunc = fd;
                return;
            }
        }
    }
}

std::optional<std::pair<Ptr<Ty>, size_t>> GetParamTyAccordingToArgName(const FuncDecl& fd, const std::string argName)
{
    CJC_ASSERT(!argName.empty());
    // Null test is done in the caller.
    auto& paramList = fd.funcBody->paramLists[0];
    // Traverse the parameters, find the parameter has same identifier with the named argument.
    for (size_t j = 0; j < paramList->params.size(); ++j) {
        if (paramList->params[j] && paramList->params[j]->identifier == argName) {
            auto ty = paramList->params[j]->type ? paramList->params[j]->type->ty : paramList->params[j]->ty;
            return {std::make_pair(ty, j)};
        }
    }
    return {};
}

std::string GetArgName(const FuncDecl& fd, const FuncArg& arg)
{
    if (arg.TestAttr(Attribute::IMPLICIT_ADD)) {
        // For trailing closure argument, its naming condition always follows the definition.
        if (!fd.funcBody->paramLists[0]->params.empty() && fd.funcBody->paramLists[0]->params.back()->isNamedParam) {
            return fd.funcBody->paramLists[0]->params.back()->identifier;
        } else {
            return "";
        }
    }
    return arg.name;
}

Ptr<Generic> GetCurrentGeneric(const FuncDecl& fd, const CallExpr& ce)
{
    CJC_NULLPTR_CHECK(fd.funcBody);
    auto generic = fd.funcBody->generic.get();
    if (generic == nullptr && fd.outerDecl && IsTypeObjectCreation(fd, ce)) {
        generic = fd.outerDecl->GetGeneric();
    }
    return generic;
}

TyVars GetTyVars(const FuncDecl& fd, const CallExpr& ce, bool ignoreContext)
{
    TyVars res;
    auto curGeneric = GetCurrentGeneric(fd, ce);
    if (curGeneric) {
        for (auto& tyParam : curGeneric->typeParameters) {
            res.emplace(StaticCast<TyVar*>(tyParam->ty));
        }
    }
    // A special case for static function calls or enum constructor.
    // Get the type variables from the class, interface, enum, struct, and
    // extend definitions.
    bool isMemberOfGenericType = false;
    if (auto ma = DynamicCast<MemberAccess*>(ce.baseFunc.get())) {
        CJC_NULLPTR_CHECK(ma->baseExpr);
        auto baseTarget = ma->baseExpr->GetTarget();
        isMemberOfGenericType =
            fd.TestAttr(Attribute::STATIC) && fd.outerDecl && fd.outerDecl->generic && fd.outerDecl->IsNominalDecl();
        isMemberOfGenericType = isMemberOfGenericType ||
            (baseTarget && baseTarget->GetGeneric() && baseTarget->TestAttr(Attribute::ENUM_CONSTRUCTOR));
    }
    if (ignoreContext || !isMemberOfGenericType) {
        return res;
    }
    for (auto& tyParam : fd.outerDecl->generic->typeParameters) {
        res.emplace(StaticCast<TyVar*>(tyParam->ty));
    }
    return res;
}

bool HasTyVarsToSolve(const SubstPack& maps)
{
    for (auto [k, v] : maps.u2i) {
        if (!Utils::InKeys(Ptr(StaticCast<TyVar*>(v)), maps.inst)) {
            return true;
        }
    }
    return false;
}

bool HasUnsolvedTyVars(const TypeSubst& subst, const std::set<Ptr<TyVar>>& tyVars)
{
    // A valid solution should constain substitution for all of type variables
    // and each substituted type should not contain any of type variable.
    return std::any_of(tyVars.begin(), tyVars.end(), [&subst](auto& tyVar) {
        return !Utils::InKeys(tyVar, subst) ||
            std::any_of(subst.begin(), subst.end(), [tyVar](auto it) { return it.second->Contains(tyVar); });
    });
}

TyVars GetTyVarsToSolve(const SubstPack& maps)
{
    TyVars ret;
    for (auto [k, v] : maps.u2i) {
        ret.emplace(StaticCast<TyVar*>(v));
    }
    Utils::EraseIf(ret, [&maps](auto tv) { return Utils::InKeys(tv, maps.inst); });
    return ret;
}

/**
 * This function is only used to collect param types in arguments order and will NOT report diagnostics.
 */
std::vector<Ptr<Ty>> GetParamTysInArgsOrder(TypeManager& tyMgr, const CallExpr& ce, const FuncDecl& fd)
{
    if (!fd.funcBody || fd.funcBody->paramLists.empty()) {
        return {};
    }
    auto& paramList = fd.funcBody->paramLists[0];
    // Record whether the position of parameters has ty.
    std::vector<bool> paramHasTy(paramList->params.size(), false);
    // If a parameter has a default value, it is already specified as ty by default.
    for (size_t i = 0; i < paramList->params.size(); ++i) {
        paramHasTy[i] = paramList->params[i]->TestAttr(Attribute::HAS_INITIAL);
    }

    // Help to record whether named argument has been appear.
    bool namedArgFound = false;
    std::vector<Ptr<Ty>> tyInArgOrder;
    for (size_t i = 0; i < ce.args.size(); ++i) {
        std::string argName = GetArgName(fd, *ce.args[i]);
        if (argName.empty()) {
            // Unnamed argument can not appear after named argument.
            if (namedArgFound) {
                return {};
            }
            // So the index of the unnamed parameter argument should match the index of the formal parameter.
            if (i < paramList->params.size()) {
                CJC_NULLPTR_CHECK(paramList->params[i]);
                tyInArgOrder.emplace_back(paramList->params[i]->ty);
                paramHasTy[i] = true;
            } else if (fd.TestAttr(Attribute::C)) {
                // For C FFI variable-length arguments.
                tyInArgOrder.emplace_back(tyMgr.GetCTypeTy());
            } else if (HasJavaAttr(fd)) {
                // For JavaScript or Java FFI variable-length arguments.
                tyInArgOrder.emplace_back(tyMgr.GetAnyTy());
            } else {
                // Cangjie's variable-length arguments are handled by `ChkVariadicCallExpr`.
                // The number of arguments mismatches the number of parameters here.
                return {};
            }
            continue;
        }
        // Named argument.
        namedArgFound = true;
        if (auto res = GetParamTyAccordingToArgName(fd, argName)) {
            auto [ty, index] = res.value();
            tyInArgOrder.emplace_back(ty);
            CJC_ASSERT(index < paramHasTy.size());
            paramHasTy[index] = true;
        } else {
            return {};
        }    
    }
    for (auto it : std::as_const(paramHasTy)) {
        if (!it) {
            return {};
        }
    }
    return tyInArgOrder;
}

bool IsEnumCtorWithoutTypeArgs(const Expr& expr, Ptr<const Decl> target)
{
    if (!target || !target->TestAttr(Attribute::ENUM_CONSTRUCTOR) || !target->GetGeneric()) {
        return false;
    }
    if (expr.astKind == ASTKind::REF_EXPR) {
        // For enum like 'None'.
        return expr.GetTypeArgs().empty();
    } else if (auto ma = DynamicCast<const MemberAccess*>(&expr);
        ma && ma->baseExpr && ma->baseExpr->IsReferenceExpr()) {
        auto baseTypeArgs = ma->baseExpr->GetTypeArgs();
        auto baseDecl = ma->baseExpr->GetTarget();
        CJC_NULLPTR_CHECK(baseDecl);
        // For enum like 'Option.None' or 'core.Option.None'.
        // 'NeedFurtherInstantiation' is checking for typealias accessing like:
        // enum E<T, K> { EE(K) }
        // type X<K> = E<Int32, K>
        // X.EE(1) -- which also needs type inference.
        // type Y = E<Int32, Int64>
        // Y.EE(1) -- which does not need type inference.
        return NeedFurtherInstantiation(baseTypeArgs) && expr.GetTypeArgs().empty();
    }
    return false;
}

const std::unordered_set<AST::ASTKind> QUESTABLE_NODES{AST::ASTKind::FUNC_ARG, AST::ASTKind::PAREN_EXPR,
    AST::ASTKind::LAMBDA_EXPR, AST::ASTKind::CALL_EXPR, AST::ASTKind::TRAIL_CLOSURE_EXPR};

bool IsQuestableNode(const Node& n)
{
    return QUESTABLE_NODES.count(n.astKind) != 0;
}

const std::unordered_set<AST::ASTKind> PLACEHOLDABLE_NODES{ASTKind::PATTERN, ASTKind::VAR_PATTERN,
    ASTKind::CONST_PATTERN, ASTKind::TUPLE_PATTERN, ASTKind::ENUM_PATTERN, ASTKind::VAR_OR_ENUM_PATTERN,
    ASTKind::TYPE_PATTERN, ASTKind::EXCEPT_TYPE_PATTERN, ASTKind::WILDCARD_PATTERN, ASTKind::CALL_EXPR,
    ASTKind::PAREN_EXPR, ASTKind::MEMBER_ACCESS, ASTKind::REF_EXPR, ASTKind::OPTIONAL_EXPR,
    ASTKind::OPTIONAL_CHAIN_EXPR, ASTKind::MATCH_EXPR, ASTKind::BLOCK, ASTKind::IF_EXPR, ASTKind::TRY_EXPR,
    ASTKind::LAMBDA_EXPR, ASTKind::TRAIL_CLOSURE_EXPR, ASTKind::SPAWN_EXPR, ASTKind::MATCH_CASE,
    ASTKind::MATCH_CASE_OTHER, ASTKind::FUNC_ARG, ASTKind::FUNC_BODY, ASTKind::FUNC_PARAM};

bool AcceptPlaceholderTarget(const AST::Node& n)
{
    return PLACEHOLDABLE_NODES.count(n.astKind) != 0;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
bool IsNeedRuntimeCheck(TypeManager& typeManager, Ty& srcTy, Ty& targetTy)
{
    auto isFinalType = [](Ty& ty) {
        if (ty.IsStruct() || ty.IsEnum() || ty.IsPointer() || ty.IsCString() || ty.IsPrimitive() || Is<VArrayTy>(ty) ||
            ty.IsArray()) {
            return true;
        }
        if (ty.IsClass()) {
            auto decl = Ty::GetDeclPtrOfTy(&ty);
            return decl && !decl->TestAnyAttr(Attribute::ABSTRACT, Attribute::OPEN);
        }
        return false;
    };
    if (isFinalType(srcTy) && isFinalType(targetTy)) {
        auto srcDecl = Ty::GetDeclPtrOfTy(&srcTy);
        auto targetDecl = Ty::GetDeclPtrOfTy(&targetTy);
        return srcDecl == targetDecl;
    }
    return (srcTy.IsClassLike() && targetTy.IsClassLike()) || srcTy.IsGeneric() || targetTy.IsGeneric() ||
        srcTy.HasGeneric() || targetTy.HasGeneric() || typeManager.IsSubtype(&srcTy, &targetTy, true, false) ||
        typeManager.IsSubtype(&targetTy, &srcTy, true, false);
}
#endif

namespace {
Ptr<AST::TypeAliasDecl> GetLastTypeAliasTargetVisit(
    AST::TypeAliasDecl& decl, std::unordered_set<Ptr<TypeAliasDecl>>& visited)
{
    auto target = &decl;
    if (visited.count(target) > 0) {
        return target;
    } else {
        visited.emplace(target);
    }
    if (auto aliasDecl = DynamicCast<AST::TypeAliasDecl*>(target); aliasDecl && aliasDecl->type) {
        auto realTarget = aliasDecl->type->GetTarget();
        if (auto innerAlias = DynamicCast<TypeAliasDecl*>(realTarget); innerAlias && innerAlias->type) {
            target = GetLastTypeAliasTargetVisit(*innerAlias, visited);
        }
    }
    return target;
}
} // namespace

Ptr<AST::TypeAliasDecl> GetLastTypeAliasTarget(AST::TypeAliasDecl& decl)
{
    std::unordered_set<Ptr<TypeAliasDecl>> visited;
    return GetLastTypeAliasTargetVisit(decl, visited);
}

void MergeSubstPack(SubstPack& target, const SubstPack& src)
{
    for (auto [tvu, tvi] : src.u2i) {
        CJC_ASSERT(!(target.u2i.count(tvu) > 0 && target.u2i[tvu] != tvi));
        target.u2i[tvu] = tvi;
    }
    MergeMultiTypeSubsts(target.inst, src.inst);
}

// decide if one type is subtype/supertype of all types. subtype or supertype is specified by lessThan
bool LessThanAll(Ptr<Ty> ty, const std::set<Ptr<Ty>>& tys, const std::function<bool(Ptr<Ty>, Ptr<Ty>)>& lessThan)
{
    return std::all_of(tys.cbegin(), tys.cend(), [ty, &lessThan](Ptr<Ty> element) { return lessThan(ty, element); });
}

Ptr<Ty> FindSmallestTy(const std::set<Ptr<Ty>>& tys, const std::function<bool(Ptr<Ty>, Ptr<Ty>)>& lessThan)
{
    if (tys.empty()) {
        return TypeManager::GetNothingTy();
    }
    Ptr<Ty> bubble = nullptr;
    // bubble over one or two tys that are not min by each iteration
    for (Ptr<Ty> ty : tys) {
        if (!bubble) {
            bubble = ty;
        } else {
            if (lessThan(bubble, ty)) {
                continue;
            } else if (lessThan(ty, bubble)) {
                bubble = ty;
            } else {
                bubble = nullptr;
            }
        }
    }
    // bubble is the only possible ty that is min, but not necessarily so, therefore need to verify
    if (bubble && LessThanAll(bubble, tys, lessThan)) {
        return bubble;
    } else {
        return TypeManager::GetInvalidTy();
    }
}

void TryEnforceCandidate(TyVar& tv, const std::set<Ptr<Decl>>& candidates, TypeManager& tyMgr)
{
    if (candidates.empty()) {
        return;
    }
    std::set<Ptr<Ty>> declTys;
    for (auto d : candidates) {
        declTys.emplace(d->ty);
    }
    auto pro = Promotion(tyMgr);
    auto isSuperDecl = [&pro](Ptr<Ty> sup, Ptr<Ty> sub) { return sub && sup && !pro.Promote(*sub, *sup).empty(); };
    // try to find the most general type
    auto uniq = FindSmallestTy(declTys, isSuperDecl);
    if (Ty::IsTyCorrect(uniq)) {
        tyMgr.ConstrainByCtor(tv, *uniq);
    } else {
        // fill type arguments of found type with placeholder tyvars, in case it's generic
        // the tyvars may be solved later when inferring the lambda body
        std::vector<Ptr<GenericsTy>> tyArgs;
        tyMgr.constraints[&tv].sum.clear();
        for (auto ty : declTys) {
            tyMgr.AddSumByCtor(tv, *ty, tyArgs);
        }
    }
}

std::set<Ptr<Ty>> TypeMapToTys(const std::map<TypeKind, TypeKind>& m, bool fromKey)
{
    std::set<Ptr<Ty>> result;
    for (auto& [operandKind, retKind] : m) {
        result.insert(TypeManager::GetPrimitiveTy(fromKey ? operandKind : retKind));
    }
    return result;
}

std::set<Ptr<Ty>> GetGenericParamsForDecl(const AST::Decl& decl)
{
    std::set<Ptr<Ty>> ret;
    if (decl.generic) {
        for (auto& gp : decl.generic->typeParameters) {
            ret.insert(gp->ty);
        }
    }
    if (auto ed = DynamicCast<ExtendDecl*>(&decl)) {
        auto target = ed->extendedType->GetTarget();
        if (target) {
            ret.merge(GetGenericParamsForDecl(*GetRealTarget(target)));
        }
    } else if (auto fd = DynamicCast<FuncDecl*>(&decl); fd && fd->funcBody && fd->funcBody->generic) {
        for (auto& gp : fd->funcBody->generic->typeParameters) {
            ret.insert(gp->ty);
        }
    }
    if (auto outer = decl.outerDecl) {
        ret.merge(GetGenericParamsForDecl(*outer));
    }
    return ret;
}

std::set<Ptr<AST::Ty>> GetGenericParamsForTy(const AST::Ty& ty)
{
    if (auto id = Ty::GetDeclPtrOfTy<InheritableDecl>(&ty)) {
        return GetGenericParamsForDecl(*id);
    }
    return {};
}

std::set<Ptr<AST::Ty>> GetGenericParamsForCall(const AST::CallExpr& ce, const AST::FuncDecl& fd)
{
    auto ret = GetGenericParamsForDecl(fd);
    auto base = ce.baseFunc.get();
    while (base) {
        if (base->ty) {
            ret.merge(GetGenericParamsForTy(*base->ty));
        }
        if (auto ma = DynamicCast<MemberAccess*>(base)) {
            if (ma->target) {
                ret.merge(GetGenericParamsForDecl(*ma->target));
            }
            base = ma->baseExpr;
        } else if (auto ce0 = DynamicCast<CallExpr*>(base)) {
            base = ce0->baseFunc;
        } else if (auto re = DynamicCast<RefExpr*>(base)) {
            if (re->ref.target) {
                ret.merge(GetGenericParamsForDecl(*re->ref.target));
            }
            base = nullptr;
        } else {
            base = nullptr;
        }
    }
    return ret;
}

std::optional<std::pair<Ptr<FuncDecl>, Ptr<Ty>>> FindInitDecl(const InheritableDecl& decl, TypeManager& typeManager,
    std::vector<OwnedPtr<Expr>>& valueArgs, const std::vector<Ptr<Ty>> instTys)
{
    std::vector<Ptr<Ty>> valueParamTys;
    std::transform(
        valueArgs.begin(), valueArgs.end(), std::back_inserter(valueParamTys), [](auto& arg) { return arg->ty; });

    return FindInitDecl(decl, typeManager, valueParamTys, instTys);
}

std::optional<std::pair<Ptr<FuncDecl>, Ptr<Ty>>> FindInitDecl(const InheritableDecl& decl, TypeManager& typeManager,
    const std::vector<Ptr<Ty>> valueParamTys, const std::vector<Ptr<Ty>> instTys)
{
    auto initFuncDecl = GetMemberDecl<FuncDecl>(decl, "init", valueParamTys, typeManager);

    if (!initFuncDecl) {
        return std::nullopt;
    }

    return std::make_pair(
        initFuncDecl, typeManager.GetInstantiatedTy(initFuncDecl->ty, GenerateTypeMapping(decl, instTys)));
}

OwnedPtr<CallExpr> CreateInitCall(const std::pair<Ptr<FuncDecl>, Ptr<Ty>> initDeclInfo,
    std::vector<OwnedPtr<Expr>>& valueArgs, File& curFile, const std::vector<Ptr<Ty>> instTys)
{
    std::vector<OwnedPtr<FuncArg>> valueFuncArgs;
    std::transform(valueArgs.begin(), valueArgs.end(), std::back_inserter(valueFuncArgs),
        [](auto& arg) { return CreateFuncArg(std::move(arg)); });

    auto call = MakeOwned<CallExpr>();
    auto initDecl = initDeclInfo.first;
    auto ty = initDeclInfo.second;
    auto refExpr = CreateRefExpr(*initDecl);
    refExpr->ty = ty;
    refExpr->curFile = &curFile;
    refExpr->instTys = instTys;
    call->baseFunc = std::move(refExpr);
    call->curFile = &curFile;
    call->resolvedFunction = initDecl;
    CJC_ASSERT(ty && ty->IsFunc());
    call->ty = StaticCast<FuncTy>(ty)->retTy;
    call->args = std::move(valueFuncArgs);
    return call;
}

OwnedPtr<ThrowExpr> CreateThrowException(
    const ClassDecl& exceptionDecl, std::vector<OwnedPtr<Expr>> args, File& curFile, TypeManager& typeManager)
{
    auto throwExpr = MakeOwned<ThrowExpr>();
    throwExpr->expr = CreateInitCall(FindInitDecl(exceptionDecl, typeManager, args).value(), args, curFile);
    throwExpr->ty = TypeManager::GetNothingTy();
    throwExpr->curFile = &curFile;
    return throwExpr;
}

OwnedPtr<GenericParamDecl> CreateGenericParamDecl(Decl& decl, const std::string& name, TypeManager& typeManager)
{
    auto typeParam = MakeOwned<GenericParamDecl>();
    typeParam->identifier = name;
    typeParam->ty = typeManager.GetGenericsTy(*typeParam);
    typeParam->outerDecl = &decl;
    return typeParam;
}

OwnedPtr<GenericParamDecl> CreateGenericParamDecl(Decl& decl, TypeManager& typeManager)
{
    return CreateGenericParamDecl(decl, "T", typeManager);
}

Ptr<FuncDecl> GenerateGetTypeForTypeParamIntrinsic(Package& pkg, TypeManager& typeManager, Ptr<Ty> strTy)
{
    auto file = pkg.files[0].get();
    auto retTy = IS_GENERIC_INSTANTIATION_ENABLED ? strTy : typeManager.GetCStringTy();
    auto funcTy = typeManager.GetFunctionTy({}, retTy);
    auto decl = MakeOwned<FuncDecl>();
    auto funcBody = MakeOwned<FuncBody>();
    funcBody->paramLists.emplace_back(CreateFuncParamList(std::vector<OwnedPtr<FuncParam>>{}));
    funcBody->retType = MakeOwned<RefType>();
    funcBody->retType->ty = retTy;
    funcBody->generic = MakeOwned<Generic>();
    funcBody->generic->typeParameters.emplace_back(CreateGenericParamDecl(*decl, typeManager));
    funcBody->ty = funcTy;

    decl->curFile = file;
    decl->identifier = GET_TYPE_FOR_TYPE_PARAMETER_FUNC_NAME;
    decl->fullPackageName = pkg.fullPackageName;
    decl->ty = funcTy;
    decl->funcBody = std::move(funcBody);
    decl->EnableAttr(Attribute::INTRINSIC);
    decl->EnableAttr(Attribute::GENERIC);

    auto declPtr = decl.get();

    file->decls.push_back(std::move(decl));
    std::rotate(file->decls.rbegin(), file->decls.rbegin() + 1, file->decls.rend());

    return declPtr;
}

Ptr<FuncDecl> GenerateIsSubtypeTypesIntrinsic(Package& pkg, TypeManager& typeManager)
{
    auto file = pkg.files[0].get();
    auto retTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto funcTy = typeManager.GetFunctionTy({}, retTy);
    auto decl = MakeOwned<FuncDecl>();
    auto funcBody = MakeOwned<FuncBody>();
    funcBody->paramLists.emplace_back(CreateFuncParamList(std::vector<OwnedPtr<FuncParam>>{}));
    funcBody->retType = MakeOwned<RefType>();
    funcBody->retType->ty = retTy;
    funcBody->generic = MakeOwned<Generic>();
    funcBody->generic->typeParameters.emplace_back(CreateGenericParamDecl(*decl, typeManager));
    funcBody->generic->typeParameters.emplace_back(CreateGenericParamDecl(*decl, typeManager));
    funcBody->ty = funcTy;

    AddCurFile(*decl, file);
    decl->identifier = IS_SUBTYPE_TYPES_FUNC_NAME;
    decl->fullPackageName = pkg.fullPackageName;
    decl->ty = funcTy;
    decl->funcBody = std::move(funcBody);
    decl->EnableAttr(Attribute::INTRINSIC);
    decl->EnableAttr(Attribute::GENERIC);
    decl->EnableAttr(Attribute::COMPILER_ADD);

    auto declPtr = decl.get();

    file->decls.push_back(std::move(decl));
    std::rotate(file->decls.rbegin(), file->decls.rbegin() + 1, file->decls.rend());

    return declPtr;
}

} // namespace Cangjie::TypeCheckUtil
