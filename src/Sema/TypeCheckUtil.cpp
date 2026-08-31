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
#include "cangjie/Modules/ModulesUtils.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie::TypeCheckUtil {
using namespace AST;
namespace {
const std::set<std::string> BUILTIN_OPERATORS = {"@", ".", "[]", "()", "++", "--", "?", "!", "-", "**", "*", "/", "%",
    "+", "<<", ">>", "<", "<=", ">", ">=", "is", "as", "==", "!=", "&", "^", "|", "..", "..=", "&&", "||", "??", "~>",
    "=", "**=", "*=", "/=", "%/", "+=", "-=", "<<=", ">>=", "&=", "^=", "|="};

bool operator<(const std::optional<ModalInfo>& lhs, const std::optional<ModalInfo>& rhs)
{
    if (!rhs) {
        return false;
    }
    if (!lhs && rhs) {
        return true;
    }
    return ToIndex(*lhs) < ToIndex(*rhs);
}
} // namespace

bool FuncSigCmp::operator()(const FuncSig& lhs, const FuncSig& rhs) const
{
    if (lhs.identifier != rhs.identifier) {
        return lhs.identifier < rhs.identifier;
    }
    if (lhs.thisMode != rhs.thisMode) {
        return lhs.thisMode < rhs.thisMode;
    }
    if (lhs.paramTys.size() != rhs.paramTys.size()) {
        return lhs.paramTys.size() < rhs.paramTys.size();
    }
    // If 'lhs' and 'rhs' have same size of func parameter types,
    // compare each type's kind, type name, type definition position and ptr value in order.
    for (size_t i = 0; i < lhs.paramTys.size(); ++i) {
        if (CompTyByNamesModal(lhs.paramTys[i], rhs.paramTys[i])) {
            return true;
        }
    }
    return false;
}

bool IsCStringConstructor(const FuncDecl& fd)
{
    if (!IsInstanceConstructor(fd) || !fd.funcBody || fd.funcBody->paramLists.empty()) {
        return false;
    }
    if (!fd.outerDecl || !fd.outerDecl->IsBuiltIn()) {
        return false;
    }
    return StaticCast<BuiltInDecl*>(fd.outerDecl)->type == BuiltInType::CSTRING;
}

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
        (void)expr.instTys.emplace_back(typeArg->DataTy());
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
        types.begin(), types.end(), [](auto& type) { return type->GetTy() && type->GetTy()->HasIntersectionTy(); });
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
    if (target == nullptr && (!node->GetTy() || node->GetTy()->IsNothing())) {
        node->SetTy({TypeManager::GetInvalidTy()});
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
    return !node.GetTy().IsCorrect();
}

bool IsFieldOperator(const std::string& field)
{
    return Utils::In(field, BUILTIN_OPERATORS);
}

std::vector<ModalTy> GetParamTys(const FuncDecl& fd)
{
    if (fd.TestAttr(Attribute::IMPORTED) && fd.GetTy().IsCorrect() && fd.GetTy()->IsFunc()) {
        return DynamicCast<FuncTy*>(fd.DataTy())->paramTys;
    }
    CJC_NULLPTR_CHECK(fd.funcBody);
    return GetFuncBodyParamTys(*fd.funcBody);
}

std::vector<ModalTy> GetFuncBodyParamTys(const FuncBody& fb)
{
    if (fb.paramLists.empty()) {
        return {};
    }
    std::vector<ModalTy> ret;
    for (auto& param : fb.paramLists[0].get()->params) {
        if (param->type) {
            param->SetTy(param->type->GetTy());
        }
        ret.emplace_back(param->GetTy() ? param->GetTy() : ModalTy{TypeManager::GetInvalidTy()});
    }
    return ret;
}

// Generate type mapping for src is an override or implement of target.
MultiTypeSubst GenerateTypeMappingBetweenFuncs(TypeManager& typeManager, const FuncDecl& src, const FuncDecl& target)
{
    MultiTypeSubst typeMapping;
    if (src.outerDecl && src.outerDecl->GetTy().IsCorrect()) {
        typeMapping = typeManager.GenerateStructDeclTypeMapping(*src.outerDecl);
    }
    if (target.TestAttr(Attribute::GENERIC) && src.TestAttr(Attribute::GENERIC)) {
        // Solve generic function (eg:`func foo<T>(arr: A<T>): Unit`) 's type identical check.
        MergeTypeSubstToMultiTypeSubst(typeMapping, typeManager.GenerateGenericMappingFromGeneric(target, src));
    }
    return typeMapping;
}

// Check if src is an override or implement of target. DO NOT call 'Synthesize'.
bool IsOverrideOrShadow(TypeManager& typeManager, const FuncDecl& src, const FuncDecl& target, DataTy baseTy,
    ModalInfo baseMode, DataTy expectInstParent, ModalInfo parentMode)
{
    if (src.TestAttr(Attribute::IS_BROKEN) || target.TestAttr(Attribute::IS_BROKEN)) {
        return false;
    }
    if (auto ret = typeManager.GetOverrideCache(&src, &target, baseTy, baseMode, expectInstParent, parentMode);
        ret.has_value()) {
        return ret.value();
    }
    if (src.TestAttr(Attribute::STATIC) != target.TestAttr(Attribute::STATIC)) {
        // Static and non-static functions cannot override each other. Because this is an early, deterministic check,
        // we do not record a negative result in `overrideOrShadowCache` (to avoid unnecessary cache growth) and
        // return false directly.
        return false;
    }
    // override this param must use the same this mode.
    if (TypeManager::HasThisParam(src) && TypeManager::GetThisParamMode(src) != TypeManager::GetThisParamMode(target)) {
        return false;
    }
    auto srcFt = DynamicCast<FuncTy*>(src.DataTy());
    auto targetFt = DynamicCast<FuncTy*>(target.DataTy());
    MultiTypeSubst mts;
    if (expectInstParent) {
        CJC_ASSERT(src.outerDecl && Is<InheritableDecl>(src.outerDecl));
        MergeTypeSubstToMultiTypeSubst(mts, GenerateTypeMappingByTy(target.outerDecl->DataTy(), expectInstParent));
        auto substituteToParent = Promotion(typeManager).Promote(src.outerDecl->DataTy(), target.outerDecl->DataTy());
        for (const auto& p : substituteToParent) {
            MergeTypeSubstToMultiTypeSubst(mts, GenerateTypeMappingByTy(p, expectInstParent));
        }
        if (target.TestAttr(Attribute::GENERIC) && src.TestAttr(Attribute::GENERIC)) {
            MergeTypeSubstToMultiTypeSubst(mts, typeManager.GenerateGenericMappingFromGeneric(target, src));
        }
    } else {
        mts = GenerateTypeMappingBetweenFuncs(typeManager, src, target);
    }
    std::set<TypeSubst> typeMappings = ExpandMultiTypeSubst(typeManager, mts, {srcFt, targetFt});
    for (auto typeMapping : typeMappings) {
        auto srcParamTys = srcFt ? srcFt->paramTys : GetParamTys(src);
        auto targetParamTys = targetFt ? targetFt->paramTys : GetParamTys(target);
        if (srcParamTys.size() != targetParamTys.size()) {
            typeManager.AddOverrideCache(src, target, baseTy, baseMode, expectInstParent, parentMode, false);
            return false;
        }
        // Only generate typeMapping by base type, if functions' outerDecls are irrelevant.
        // eg: interface I1 { func foo(): Int64 }, interface I2 { func foo(): Int64 }
        // class/interface Type3 <: I1 & I2 { func foo() : Int64 {0}}
        if (Ty::IsTyCorrect(baseTy) && Is<InterfaceDecl>(src.outerDecl) && Is<InterfaceDecl>(target.outerDecl)) {
            auto parentStructTy = typeManager.GetInstantiatedTy(target.outerDecl->GetTy(), typeMapping);
            auto childStructTy = typeManager.GetInstantiatedTy(src.outerDecl->GetTy(), typeMapping);
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
        if (typeManager.IsFuncParameterTypesIdentical(srcParamTys, targetParamTys) &&
            (!TypeManager::HasThisParam(src) ||
                typeManager.GetThisParamMode(src) == typeManager.GetThisParamMode(target))) {
            bool isCrossPlatform =
                (src.TestAttr(AST::Attribute::COMMON) && target.TestAttr(AST::Attribute::SPECIFIC)) ||
                (src.TestAttr(AST::Attribute::SPECIFIC) && target.TestAttr(AST::Attribute::COMMON));
            if (isCrossPlatform) {
                continue;
            }
            typeManager.AddOverrideCache(src, target, baseTy, baseMode, expectInstParent, parentMode, true);
            return true;
        }
    }
    typeManager.AddOverrideCache(src, target, baseTy, baseMode, expectInstParent, parentMode, false);
    return false;
}

// Check if src is an override or implement of target. DO NOT call 'Synthesize'.
bool IsOverrideOrShadow(TypeManager& typeManager, const PropDecl& src, const PropDecl& target, DataTy baseTy)
{
    CJC_ASSERT(src.outerDecl);
    MultiTypeSubst mts = typeManager.GenerateStructDeclTypeMapping(*src.outerDecl);
    auto typeMapping = MultiTypeSubstToTypeSubst(mts);
    // Only generate typeMapping by base type, if functions' outerDecls are irrelevant.
    // eg: interface I1 { func foo() : Int64 }, interface I2 { func foo() : Int64}
    // class/interface Type3 <: I1&I2 { func foo() : Int64 {0}}
    if (Ty::IsTyCorrect(baseTy) && Is<InterfaceDecl>(src.outerDecl) && Is<InterfaceDecl>(target.outerDecl)) {
        auto parentStructTy = typeManager.GetInstantiatedTy(target.outerDecl->GetTy(), typeMapping);
        auto childStructTy = typeManager.GetInstantiatedTy(src.outerDecl->GetTy(), typeMapping);
        if (!typeManager.IsSubtype(childStructTy, parentStructTy)) {
            typeManager.GenerateGenericMapping(mts, *baseTy);
            typeMapping.merge(MultiTypeSubstToTypeSubst(mts));
        }
    }
    auto srcTy = src.type ? src.type->GetTy() : src.GetTy();
    auto targetTy = target.type ? target.type->GetTy() : target.GetTy();
    bool ret = srcTy == typeManager.GetInstantiatedTy(targetTy, typeMapping);
    // If property `src` overrides `target` within the same instantiated type (i.e. `src.outerDecl` equals the
    // declaration of `baseTy`) and both have the same `static` attribute, update the cached overridden function
    // declaration for the property.
    if (ret && src.outerDecl == Ty::GetDeclPtrOfTy(baseTy) &&
        src.TestAttr(Attribute::STATIC) == target.TestAttr(Attribute::STATIC)) {
        typeManager.UpdateTopOverriddenFuncDeclMap(&src, &target);
    }
    return ret;
}

// Check where expr is memberAccess calling interface's member.
bool IsGenericUpperBoundCall(const Expr& expr, Decl& target)
{
    auto ma = DynamicCast<const MemberAccess*>(&expr);
    bool isNotGenericCall =
        !ma || !ma->baseExpr || !ma->baseExpr->GetTy() || !ma->baseExpr->GetTy()->IsGeneric() || !target.outerDecl;
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

static Ptr<FuncDecl> GetUsedPropAccessor(const PropDecl& decl, bool isGetter)
{
    if (auto pd = DynamicCast<PropDecl>(&decl)) {
        auto& funcs = isGetter ? pd->getters : pd->setters;
        if (!funcs.empty()) {
            return funcs[0];
        }
    }
    return nullptr;
}

static Ptr<FuncDecl> GetValidPropAccessor(ClassDecl& cd, bool isGetter, ModalInfo targetModal, const std::string& name)
{
    auto curClass = &cd;
    while (curClass != nullptr) {
        for (auto& it : curClass->GetMemberDecls()) {
            if (it->identifier != name || !Is<PropDecl>(it)) {
                continue;
            }
            auto& prop = StaticCast<PropDecl>(*it);
            if (prop.TyMode() != targetModal) {
                continue;
            }
            if (auto found = GetUsedPropAccessor(prop, isGetter)) {
                return found;
            }
        }
        curClass = curClass->GetSuperClassDecl();
    }
    return nullptr;
}

/**
 * Since spec support 'var' propDecl to inherit parent's getter/setter separately that
 * child can only override one of getter/setter.
 * We need to find getter/setter from current class or parent class.
 * return (getter, setter)
 */
std::pair<Ptr<FuncDecl>, Ptr<FuncDecl>> GetUsableGetterSetterForProperty(PropDecl& pd)
{
    return std::make_pair(GetUsableAccessorForProperty(pd, true), GetUsableAccessorForProperty(pd, false));
}

Ptr<FuncDecl> GetUsableAccessorForProperty(PropDecl& pd, bool isGetter)
{
    Ptr<FuncDecl> fun = nullptr;
    auto cd = DynamicCast<ClassDecl*>(pd.outerDecl);
    auto& col = isGetter ? pd.getters : pd.setters;
    if (col.empty()) {
        if (cd) {
            fun = GetValidPropAccessor(*cd, isGetter, pd.TyMode(), pd.identifier);
        }
    } else {
        fun = col[0];
    }
    return fun;
}

Ptr<AST::Decl> GetUsedMemberDecl(AST::Decl& decl, bool isGetter)
{
    if (auto pd = DynamicCast<PropDecl*>(&decl); pd) {
        auto& funcs = isGetter ? pd->getters : pd->setters;
        if (!funcs.empty()) {
            return funcs[0];
        }
        if (pd->isVar) {
            if (auto found = GetUsableAccessorForProperty(*pd, isGetter)) {
                return found;
            }
        }
        return nullptr;
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

size_t CountOptionNestedLevel(DataTy ty)
{
    size_t level = 0;
    DataTy currentTy = ty;
    while (currentTy->IsCoreOptionType()) {
        auto args = currentTy->TyArgs();
        CJC_ASSERT(args.size() == 1);
        CJC_NULLPTR_CHECK(args.front());
        currentTy = args.front();
        level++;
    }
    return level;
}

ModalTy UnboxOptionType(ModalTy ty)
{
    ModalTy optionUnboxTy = ty;
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
        fullType += interface->GetTy().String();
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
                if (auto fd = DynamicCast<FuncDecl>(it)) {
                    funcTargets.push_back(fd);
                }
            }
            return funcTargets;
        }
        case ASTKind::MEMBER_ACCESS: {
            std::vector<Ptr<FuncDecl>> funcTargets;
            for (auto target : StaticCast<const MemberAccess&>(node).targets) {
                if (auto fd = DynamicCast<FuncDecl>(target)) {
                    funcTargets.push_back(fd);
                }
            }
            return funcTargets;
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

Ptr<Annotation> FindFirstAnnotation(const Decl& decl, AnnotationKind kind)
{
    auto found = std::find_if(decl.annotations.begin(), decl.annotations.end(),
        [&kind](const auto& anno) { return anno.get()->kind == kind; });
    if (found != decl.annotations.end()) {
        return (*found).get();
    }
    return nullptr;
}

ModalInfo GetThisParamModal(const FuncDecl& fd)
{
    // Accessor's this-modal comes from the owning property's type
    if (fd.propDecl) {
        return fd.propDecl->TyMode();
    }
    if (fd.funcBody->paramLists[0]->thisParam) {
        return fd.funcBody->paramLists[0]->thisParam->modal;
    }
    return {};
}
ModalInfo GetThisParamModal(const Decl& decl)
{
    if (auto fd = DynamicCast<FuncDecl>(&decl)) {
        return GetThisParamModal(*fd);
    }
    if (auto pd = DynamicCast<PropDecl>(&decl)) {
        return pd->TyMode();
    }
    return {};
}

ModalInfo GetCurThisModal(const ASTContext& ctx, const std::string& scopeName)
{
    auto pred = [](Symbol& sym) -> bool {
        auto funcDecl = DynamicCast<FuncDecl*>(sym.node);
        if (!funcDecl) {
            return false;
        }
        if (!Is<InheritableDecl>(funcDecl->outerDecl) || funcDecl->TestAttr(Attribute::STATIC)) {
            return false;
        }
        return true;
    };
    auto funcNode = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(ctx, scopeName, pred);
    if (!funcNode) {
        return {};
    }
    auto func = StaticCast<FuncDecl>(funcNode->node);
    if (!TypeManager::HasThisParam(*func)) {
        return {};
    }
    return GetThisParamModal(*func);
}

bool HasDefaultImpl(const Decl& decl)
{
    if (auto pd = DynamicCast<PropDecl>(&decl)) {
        if (pd->getters.size() > 0) {
            return pd->getters[0]->TestAttr(Attribute::DEFAULT);
        }
        if (pd->setters.size() > 0) {
            return pd->setters[0]->TestAttr(Attribute::DEFAULT);
        }
    }
    return decl.TestAttr(Attribute::DEFAULT);
}

void AddArrayLitConstructor(ArrayLit& al)
{
    auto decl = Ty::GetDeclPtrOfTy(al.GetTy());
    if (!decl) {
        return;
    }
    for (auto it : decl->GetMemberDeclPtrs()) {
        if (auto fd = DynamicCast<FuncDecl*>(it); fd && IsInstanceConstructor(*fd) && fd->funcBody) {
            // Constructor used for 'ArrayLit' has 3 params.
            if (fd->funcBody->paramLists.empty() || fd->funcBody->paramLists[0]->params.size() != 3) {
                continue;
            }
            if (GetThisParamModal(*fd) != al.TyMode()) {
                continue;
            }
            auto firstParamTy = fd->funcBody->paramLists[0]->params[0]->GetTy();
            if (Ty::IsTyCorrect(firstParamTy) && firstParamTy->IsArray()) {
                al.initFunc = fd;
                return;
            }
        }
    }
}

std::optional<std::pair<ModalTy, size_t>> GetParamTyAccordingToArgName(const FuncDecl& fd, const std::string argName)
{
    CJC_ASSERT(!argName.empty());
    // Null test is done in the caller.
    auto& paramList = fd.funcBody->paramLists[0];
    // Traverse the parameters, find the parameter has same identifier with the named argument.
    for (size_t j = 0; j < paramList->params.size(); ++j) {
        if (paramList->params[j] && paramList->params[j]->identifier == argName) {
            auto ty = paramList->params[j]->type ? paramList->params[j]->type->GetTy() : paramList->params[j]->GetTy();
            return {std::make_pair(ty, j)};
        }
    }
    // Legacy CString(am: ptr) calls use arbitrary labels; accept any named arg for the single-param ctor.
    if (IsCStringConstructor(fd) && paramList->params.size() == 1 && paramList->params[0]) {
        auto ty = paramList->params[0]->type ? paramList->params[0]->type->GetTy() : paramList->params[0]->GetTy();
        return {std::make_pair(ty, 0U)};
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
            res.emplace(StaticCast<TyVar*>(tyParam->DataTy()));
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
        res.emplace(StaticCast<TyVar*>(tyParam->DataTy()));
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
std::vector<ModalTy> GetParamTysInArgsOrder(TypeManager& tyMgr, const CallExpr& ce, const FuncDecl& fd)
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
    std::vector<ModalTy> tyInArgOrder;
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
                tyInArgOrder.emplace_back(paramList->params[i]->GetTy());
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
bool IsNeedRuntimeCheck(TypeManager& typeManager, DataTy srcTy, DataTy targetTy)
{
    auto isFinalType = [](DataTy ty) {
        if (!ty) {
            return false;
        }
        if (ty->IsStruct() || ty->IsEnum() || ty->IsPointer() || ty->IsCString() || ty->IsPrimitive() ||
            Is<VArrayTy>(ty.get()) || ty->IsArray()) {
            return true;
        }
        if (ty->IsClass()) {
            auto decl = Ty::GetDeclPtrOfTy(ty);
            return decl && !decl->TestAnyAttr(Attribute::ABSTRACT, Attribute::OPEN);
        }
        return false;
    };
    // Nothing is a subtype of any other type; casting any Non-Nothing type to Nothing will always be false.
    if (targetTy->IsNothing()) {
        return false;
    }
    if (isFinalType(srcTy) && isFinalType(targetTy)) {
        auto srcDecl = Ty::GetDeclPtrOfTy(srcTy);
        auto targetDecl = Ty::GetDeclPtrOfTy(targetTy);
        if (srcDecl || targetDecl) {
            return srcDecl == targetDecl;
        }
    }
    return (srcTy && srcTy->IsClassLike() && targetTy && targetTy->IsClassLike()) || (srcTy && srcTy->IsGeneric()) ||
        (targetTy && targetTy->IsGeneric()) || (srcTy && srcTy->HasGeneric()) || (targetTy && targetTy->HasGeneric()) ||
        typeManager.IsSubtype(srcTy, targetTy, true, false) || typeManager.IsSubtype(targetTy, srcTy, true, false);
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
bool LessThanAll(DataTy ty, const std::set<DataTy>& tys, const std::function<bool(DataTy, DataTy)>& lessThan)
{
    return std::all_of(tys.cbegin(), tys.cend(), [ty, &lessThan](DataTy element) { return lessThan(ty, element); });
}

DataTy FindSmallestTy(const std::set<DataTy>& tys, const std::function<bool(DataTy, DataTy)>& lessThan)
{
    if (tys.empty()) {
        return {TypeManager::GetNothingTy()};
    }
    DataTy bubble{};
    // bubble over one or two tys that are not min by each iteration
    for (DataTy ty : tys) {
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

namespace {
bool MemberMatchesSig(const Decl& member, const MemSig& sig)
{
    if (member.identifier != sig.id) {
        return false;
    }
    if (sig.isVarOrProp) {
        return member.astKind == ASTKind::VAR_DECL || member.astKind == ASTKind::PROP_DECL;
    }
    auto fd = DynamicCast<const FuncDecl*>(&member);
    if (!fd || !fd->funcBody || fd->funcBody->paramLists.empty()) {
        return false;
    }
    auto generic = fd->GetGeneric();
    auto genArity = generic ? generic->typeParameters.size() : 0;
    // Generic members are also registered with genArity 0 for inferred type arguments.
    return fd->funcBody->paramLists[0]->params.size() == sig.arity &&
        (sig.genArity == 0 || genArity == sig.genArity);
}

std::vector<Ptr<GenericsTy>> GetDeclGenericParamVec(const Decl& d)
{
    std::vector<Ptr<GenericsTy>> res;
    if (auto generic = d.GetGeneric()) {
        for (auto& tp : generic->typeParameters) {
            if (auto gTy = DynamicCast<GenericsTy*>(tp->DataTy())) {
                res.push_back(gTy);
            }
        }
    }
    return res;
}

// Collect the generic params of `member`'s owner appearing in `member`'s parameter types,
// i.e. those determined once the member call's arguments are known.
void CollectMemberParamCoverage(
    const Decl& member, const std::set<Ptr<GenericsTy>>& ownerGps, std::set<Ptr<GenericsTy>>& covered)
{
    auto fd = DynamicCast<const FuncDecl*>(&member);
    if (!fd || !fd->funcBody || fd->funcBody->paramLists.empty()) {
        return; // vars & props have no parameters, thus determine nothing
    }
    for (auto& param : fd->funcBody->paramLists[0]->params) {
        if (param->GetTy() && param->GetTy()->HasGeneric()) {
            covered.merge(param->GetTy()->GetGenericTyArgs(ownerGps));
        }
    }
}

void CollectMemberResultCoverage(
    const Decl& member, const std::set<Ptr<GenericsTy>>& ownerGps, std::set<Ptr<GenericsTy>>& covered)
{
    ModalTy resultTy;
    if (auto fd = DynamicCast<const FuncDecl*>(&member); fd && fd->funcBody && fd->funcBody->retType) {
        resultTy = fd->funcBody->retType->GetTy();
    }
    if ((!resultTy || !Ty::IsTyCorrect(resultTy.Ty())) && member.GetTy() && member.GetTy()->IsFunc()) {
        resultTy = RawStaticCast<FuncTy*>(member.DataTy())->retTy;
    }
    if (!resultTy) {
        resultTy = member.GetTy();
    }
    if (resultTy && resultTy->HasGeneric()) {
        covered.merge(resultTy->GetGenericTyArgs(ownerGps));
    }
}

// Collect the generic params of `owner` that appear in the parameter types of `owner`'s own
// members matching any of `memSigs`, i.e. those determined once such a member call's arguments
// are known. `anyMatch` is set when at least one member matches.
std::set<Ptr<GenericsTy>> DirectMemberCoverage(const Decl& owner, const std::vector<MemSig>& memSigs,
    const MemSigSet& resultConstrainedMemSigs, const std::set<Ptr<GenericsTy>>& ownerGps, bool& anyMatch)
{
    std::set<Ptr<GenericsTy>> covered;
    for (auto& member : owner.GetMemberDecls()) {
        CJC_NULLPTR_CHECK(member);
        for (const auto& sig : memSigs) {
            if (!MemberMatchesSig(*member, sig)) {
                continue;
            }
            anyMatch = true;
            CollectMemberParamCoverage(*member, ownerGps, covered);
            if (resultConstrainedMemSigs.count(sig) > 0) {
                CollectMemberResultCoverage(*member, ownerGps, covered);
            }
        }
    }
    return covered;
}

struct CoverageInput {
    const std::vector<MemSig>* memSigs;
    const MemSigSet* resultConstrainedMemSigs;
    TypeManager* tyMgr;
};

std::set<Ptr<GenericsTy>> CoveredGenericsOf(
    Decl& d, CoverageInput& input, bool& anyMatch, std::set<Ptr<Decl>>& visited);

struct CoverageContext {
    CoverageInput* input;
    TypeManager* tyMgr;
    bool* anyMatch;
    std::set<Ptr<GenericsTy>>* covered;
};

// Collect into `covered` the generic params of the extended decl that are determined by members
// declared in extends of it. The extended decl's i-th generic param unifies with the i-th type
// arg of the extended type; it is determined once all extend generics inside that type arg are
// determined by the matching extend members' parameter types.
void CollectExtendCoverage(const InheritableDecl& id, const std::vector<Ptr<GenericsTy>>& gpsVec,
    CoverageContext& ctx)
{
    for (auto extend : ctx.tyMgr->GetDeclExtends(id)) {
        CJC_NULLPTR_CHECK(extend);
        auto extGpsVec = GetDeclGenericParamVec(*extend);
        std::set<Ptr<GenericsTy>> extGps(extGpsVec.begin(), extGpsVec.end());
        bool extMatch = false;
        auto extCovered =
            DirectMemberCoverage(*extend, *ctx.input->memSigs, *ctx.input->resultConstrainedMemSigs, extGps, extMatch);
        if (!extMatch) {
            continue;
        }
        *ctx.anyMatch = true;
        auto extTy = extend->extendedType ? extend->extendedType->GetTy() : ModalTy{};
        if (!extTy) {
            continue;
        }
        for (size_t i = 0; i < gpsVec.size() && i < extTy->typeArgs.size(); ++i) {
            auto arg = extTy->typeArgs[i];
            if (!arg || !arg->HasGeneric()) {
                continue;
            }
            auto need = arg->GetGenericTyArgs(extGps);
            if (!need.empty() && std::includes(extCovered.begin(), extCovered.end(), need.begin(), need.end())) {
                ctx.covered->insert(gpsVec[i]);
            }
        }
    }
}

// Collect into `covered` the generic params of `id` determined by inherited members. A covered
// super generic param is unified with the corresponding type arg of the inherited type, which
// structurally determines `id`'s generics inside that type arg.
void CollectSuperCoverage(
    const InheritableDecl& id, const std::set<Ptr<GenericsTy>>& gps, std::set<Ptr<Decl>>& visited,
    CoverageContext& ctx)
{
    for (auto& inheritedType : id.inheritedTypes) {
        auto superTy = inheritedType ? inheritedType->GetTy() : ModalTy{};
        auto superDecl = Ty::GetDeclOfTy<InheritableDecl>(superTy);
        if (!superDecl || !visited.insert(superDecl).second) {
            continue;
        }
        bool superMatch = false;
        auto superCovered = CoveredGenericsOf(*superDecl, *ctx.input, superMatch, visited);
        if (!superMatch) {
            continue;
        }
        *ctx.anyMatch = true;
        auto superGpsVec = GetDeclGenericParamVec(*superDecl);
        for (size_t j = 0; j < superGpsVec.size() && j < superTy->typeArgs.size(); ++j) {
            auto arg = superTy->typeArgs[j];
            if (superCovered.count(superGpsVec[j]) > 0 && arg && arg->HasGeneric()) {
                ctx.covered->merge(arg->GetGenericTyArgs(gps));
            }
        }
    }
}

// Collect the generic params of `d` determined by member usages matching `memSigs`, considering
// members declared in `d` itself, in extends of `d`, and inherited ones; coverage of extend/super
// generic params is mapped back to `d`'s own params through the extended/inherited type's args.
std::set<Ptr<GenericsTy>> CoveredGenericsOf(
    Decl& d, CoverageInput& input, bool& anyMatch, std::set<Ptr<Decl>>& visited)
{
    auto gpsVec = GetDeclGenericParamVec(d);
    std::set<Ptr<GenericsTy>> gps(gpsVec.begin(), gpsVec.end());
    auto covered = DirectMemberCoverage(d, *input.memSigs, *input.resultConstrainedMemSigs, gps, anyMatch);
    auto id = DynamicCast<InheritableDecl*>(&d);
    if (!id) {
        return covered;
    }
    CoverageContext ctx{&input, input.tyMgr, &anyMatch, &covered};
    CollectExtendCoverage(*id, gpsVec, ctx);
    CollectSuperCoverage(*id, gps, visited, ctx);
    return covered;
}

// A generic candidate is only useful in a sum constraint if the member usages that produced it
// can eventually determine all of its generic params. A candidate like HashMap<K, V> matched via
// `operator [](key: K): V` leaves V as a placeholder no later constraint can solve; it cannot be
// disambiguated anyway, while the free placeholder keeps all dependent constraints unsolvable and
// causes combinatorial re-checks of the enclosing expressions.
bool CanDetermineCandidate(
    Decl& candidate, const std::vector<MemSig>& memSigs, const MemSigSet& resultConstrainedMemSigs, TypeManager& tyMgr)
{
    if (memSigs.empty()) {
        return true;
    }
    auto gpsVec = GetDeclGenericParamVec(candidate);
    if (gpsVec.empty()) {
        // non-generic candidates never introduce free placeholders
        return true;
    }
    bool anyMatch = false;
    std::set<Ptr<Decl>> visited{&candidate};
    CoverageInput input{&memSigs, &resultConstrainedMemSigs, &tyMgr};
    auto covered = CoveredGenericsOf(candidate, input, anyMatch, visited);
    if (!anyMatch) {
        // no declared or inherited member matched; nothing can be concluded, keep conservatively
        return true;
    }
    return std::all_of(gpsVec.begin(), gpsVec.end(), [&covered](Ptr<GenericsTy> gp) { return covered.count(gp) > 0; });
}
} // namespace

void TryEnforceCandidate(
    TyVar& tv, const std::set<Ptr<Decl>>& candidates, TypeManager& tyMgr, const std::vector<MemSig>& memSigs,
    const MemSigSet& resultConstrainedMemSigs)
{
    if (candidates.empty()) {
        return;
    }
    std::set<DataTy> declTys;
    for (auto d : candidates) {
        declTys.emplace(d->DataTy());
    }
    auto pro = Promotion(tyMgr);
    auto isSuperDecl = [&pro](DataTy sup, DataTy sub) { return sub && sup && !pro.Promote(sub, sup).empty(); };
    // try to find the most general type
    auto uniq = FindSmallestTy(declTys, isSuperDecl);
    if (resultConstrainedMemSigs.empty() && Ty::IsTyCorrect(uniq)) {
        tyMgr.ConstrainByCtor(tv, *uniq);
    } else {
        std::set<Ptr<Ty>> sumTys;
        for (auto d : candidates) {
            if (CanDetermineCandidate(*d, memSigs, resultConstrainedMemSigs, tyMgr)) {
                sumTys.emplace(d->DataTy());
            }
        }
        if (sumTys.empty() && !resultConstrainedMemSigs.empty()) {
            // The coverage check only proves generics determined by member parameter types. If every candidate is
            // filtered, later constraints from the member result may still be the only available discriminator.
            sumTys = declTys;
        }
        if (sumTys.empty()) {
            // constraining tv by any of these candidates is hopeless, leave it free
            return;
        }
        // fill type arguments of found type with placeholder tyvars, in case it's generic
        // the tyvars may be solved later when inferring the lambda body
        std::vector<Ptr<GenericsTy>> tyArgs;
        tyMgr.constraints[&tv].sum.softClear();
        for (auto ty : sumTys) {
            tyMgr.AddSumByCtor(tv, *ty, tyArgs);
        }
    }
}

std::set<DataTy> TypeMapToTys(const std::map<TypeKind, TypeKind>& m, bool fromKey)
{
    std::set<DataTy> result;
    for (auto& [operandKind, retKind] : m) {
        result.insert(TypeManager::GetPrimitiveTy(fromKey ? operandKind : retKind));
    }
    return result;
}

std::set<DataTy> GetGenericParamsForDecl(const AST::Decl& decl)
{
    std::set<DataTy> ret;
    if (decl.generic) {
        for (auto& gp : decl.generic->typeParameters) {
            ret.insert(gp->DataTy());
        }
    }
    if (auto ed = DynamicCast<ExtendDecl*>(&decl)) {
        auto target = ed->extendedType->GetTarget();
        if (target) {
            ret.merge(GetGenericParamsForDecl(*GetRealTarget(target)));
        }
    } else if (auto fd = DynamicCast<FuncDecl*>(&decl); fd && fd->funcBody && fd->funcBody->generic) {
        for (auto& gp : fd->funcBody->generic->typeParameters) {
            ret.insert(gp->DataTy());
        }
    }
    if (auto outer = decl.outerDecl) {
        ret.merge(GetGenericParamsForDecl(*outer));
    }
    return ret;
}

std::set<AST::DataTy> GetGenericParamsForTy(AST::ModalTy ty)
{
    if (auto id = Ty::GetDeclPtrOfTy<InheritableDecl>(ty)) {
        return GetGenericParamsForDecl(*id);
    }
    return {};
}

std::set<AST::DataTy> GetGenericParamsForCall(const AST::CallExpr& ce, const AST::FuncDecl& fd)
{
    auto ret = GetGenericParamsForDecl(fd);
    auto base = ce.baseFunc.get();
    while (base) {
        if (base->GetTy()) {
            ret.merge(GetGenericParamsForTy(base->GetTy()));
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

std::optional<std::pair<Ptr<FuncDecl>, DataTy>> FindInitDecl(const InheritableDecl& decl, TypeManager& typeManager,
    std::vector<OwnedPtr<Expr>>& valueArgs, const std::vector<DataTy> instTys)
{
    std::vector<ModalTy> valueParamTys;
    std::transform(
        valueArgs.begin(), valueArgs.end(), std::back_inserter(valueParamTys), [](auto& arg) { return arg->GetTy(); });

    return FindInitDecl(decl, typeManager, valueParamTys, instTys);
}

std::optional<std::pair<Ptr<FuncDecl>, DataTy>> FindInitDecl(const InheritableDecl& decl, TypeManager& typeManager,
    const std::vector<ModalTy> valueParamTys, const std::vector<DataTy> instTys)
{
    auto initFuncDecl = GetMemberDecl<FuncDecl>(decl, "init", valueParamTys, typeManager);

    if (!initFuncDecl) {
        return std::nullopt;
    }

    return std::make_pair(
        initFuncDecl, typeManager.GetInstantiatedTy(initFuncDecl->DataTy(), GenerateTypeMapping(decl, instTys)));
}

OwnedPtr<CallExpr> CreateInitCall(const std::pair<Ptr<FuncDecl>, DataTy> initDeclInfo,
    std::vector<OwnedPtr<Expr>>& valueArgs, File& curFile, const std::vector<DataTy> instTys)
{
    std::vector<OwnedPtr<FuncArg>> valueFuncArgs;
    std::transform(valueArgs.begin(), valueArgs.end(), std::back_inserter(valueFuncArgs),
        [](auto& arg) { return CreateFuncArg(std::move(arg)); });

    auto call = MakeOwned<CallExpr>();
    auto initDecl = initDeclInfo.first;
    auto ty = initDeclInfo.second;
    auto refExpr = CreateRefExpr(*initDecl);
    refExpr->SetTy({ty});
    refExpr->curFile = &curFile;
    refExpr->instTys = instTys;
    call->baseFunc = std::move(refExpr);
    call->curFile = &curFile;
    call->resolvedFunction = initDecl;
    CJC_ASSERT(ty && ty->IsFunc());
    call->SetTy({StaticCast<FuncTy>(ty)->retTy});
    call->args = std::move(valueFuncArgs);
    return call;
}

OwnedPtr<ThrowExpr> CreateThrowException(
    const ClassDecl& exceptionDecl, std::vector<OwnedPtr<Expr>> args, File& curFile, TypeManager& typeManager)
{
    auto throwExpr = MakeOwned<ThrowExpr>();
    throwExpr->expr = CreateInitCall(FindInitDecl(exceptionDecl, typeManager, args).value(), args, curFile);
    throwExpr->SetTy({TypeManager::GetNothingTy()});
    throwExpr->curFile = &curFile;
    return throwExpr;
}

OwnedPtr<GenericParamDecl> CreateGenericParamDecl(Decl& decl, const std::string& name, TypeManager& typeManager)
{
    auto typeParam = MakeOwned<GenericParamDecl>();
    typeParam->identifier = name;
    typeParam->SetTy({typeManager.GetGenericsTy(*typeParam)});
    typeParam->outerDecl = &decl;
    typeParam->fullPackageName = decl.fullPackageName;
    return typeParam;
}

OwnedPtr<GenericParamDecl> CreateGenericParamDecl(Decl& decl, TypeManager& typeManager)
{
    return CreateGenericParamDecl(decl, "T", typeManager);
}

Ptr<FuncDecl> GenerateGetTypeForTypeParamIntrinsic(Package& pkg, TypeManager& typeManager)
{
    auto file = pkg.files[0].get();
    auto retTy = typeManager.GetCStringTy();
    auto funcTy = typeManager.GetFunctionTy({}, {retTy});
    auto decl = MakeOwned<FuncDecl>();
    auto funcBody = MakeOwned<FuncBody>();
    funcBody->paramLists.emplace_back(CreateFuncParamList(std::vector<OwnedPtr<FuncParam>>{}));
    funcBody->retType = MakeOwned<RefType>();
    funcBody->retType->SetTy({retTy});
    funcBody->retType->EnableAttr(Attribute::COMPILER_ADD);
    funcBody->generic = MakeOwned<Generic>();
    funcBody->generic->typeParameters.emplace_back(CreateGenericParamDecl(*decl, typeManager));
    funcBody->SetTy({funcTy});

    decl->curFile = file;
    decl->begin = file->begin;
    decl->end = file->begin;
    decl->identifier = GET_TYPE_FOR_TYPE_PARAMETER_FUNC_NAME;
    decl->fullPackageName = pkg.fullPackageName;
    decl->SetTy({funcTy});
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
    auto funcTy = typeManager.GetFunctionTy({}, {retTy});
    auto decl = MakeOwned<FuncDecl>();
    auto funcBody = MakeOwned<FuncBody>();
    funcBody->paramLists.emplace_back(CreateFuncParamList(std::vector<OwnedPtr<FuncParam>>{}));
    funcBody->retType = MakeOwned<RefType>();
    funcBody->retType->SetTy({retTy});
    funcBody->generic = MakeOwned<Generic>();
    funcBody->generic->typeParameters.emplace_back(CreateGenericParamDecl(*decl, "T1", typeManager));
    funcBody->generic->typeParameters.emplace_back(CreateGenericParamDecl(*decl, "T2", typeManager));
    funcBody->SetTy({funcTy});

    AddCurFile(*decl, file);
    decl->begin = file->begin;
    decl->end = file->begin;
    decl->identifier = IS_SUBTYPE_TYPES_FUNC_NAME;
    decl->fullPackageName = pkg.fullPackageName;
    decl->SetTy({funcTy});
    decl->funcBody = std::move(funcBody);
    decl->EnableAttr(Attribute::INTRINSIC);
    decl->EnableAttr(Attribute::GENERIC);
    decl->EnableAttr(Attribute::COMPILER_ADD);

    auto declPtr = decl.get();

    file->decls.push_back(std::move(decl));
    std::rotate(file->decls.rbegin(), file->decls.rbegin() + 1, file->decls.rend());

    return declPtr;
}

bool SearchTargetDeclForProtectMember(const Node& curComposite, const Decl& outerDeclOfTarget, TypeManager& typeManager)
{
    auto typeDecl = &curComposite;
    // For protected members, need check the extended type.
    if (curComposite.astKind == ASTKind::EXTEND_DECL) {
        auto outerED = RawStaticCast<const ExtendDecl*>(&curComposite);
        auto decl = Ty::GetDeclPtrOfTy(outerED->extendedType->GetTy());
        if (decl == &outerDeclOfTarget) {
            return true;
        }
        if (decl) {
            typeDecl = decl; // Let the BFS search start from the extended declaration.
        }
    }
    // For protected members, need check superclass.
    if (typeDecl->IsClassLikeDecl()) {
        auto outerCLD = RawStaticCast<const ClassLikeDecl*>(typeDecl);
        std::queue<Ptr<const InheritableDecl>> res;
        res.push(outerCLD);
        while (!res.empty()) {
            auto iter = res.front();
            if (iter == &outerDeclOfTarget) {
                return true;
            }
            for (const auto& extend : typeManager.GetDeclExtends(*iter)) {
                res.push(extend);
            }
            if (auto cd = DynamicCast<ClassDecl*>(iter); cd && cd->GetSuperClassDecl()) {
                auto superClass = cd->GetSuperClassDecl();
                res.push(superClass);
            }
            res.pop();
        }
    }
    return false;
}

inline bool IsNormalCtorRef(const AST::Node& node)
{
    auto re = DynamicCast<RefExpr>(&node);
    return !re || (!re->isThis && !re->isSuper);
}

bool IsLegalAccess(Symbol* curComposite, const Decl& d, const AST::Node& node, ImportManager& importManager,
    TypeManager& typeManager)
{
    auto vd = DynamicCast<const VarDecl*>(&d);
    auto fd = DynamicCast<const FuncDecl*>(&d);
    auto cld = DynamicCast<const ClassLikeDecl*>(&d);
    auto bid = DynamicCast<BuiltInDecl>(&d);
    if (!vd && !fd && !cld && !bid) {
        // There are four kinds of members in class, VarDecl, funcDecl, classDecl and interfaceDecl. If decl is not one
        // of these, then there is no need to check visibility.
        return true;
    }
    // Public & external decls are always accessible. But public in extend may not export.
    // The node with 'IN_CORE' or 'IN_MACRO' attribute is created by compiler,
    // allowing access any kind of decl for special.
    if (node.TestAnyAttr(Attribute::IN_CORE, Attribute::IN_MACRO)) {
        return true;
    }
    if (!node.IsSamePackage(d) && node.curFile && node.astKind == ASTKind::MEMBER_ACCESS) {
        auto ma = StaticCast<MemberAccess>(&node);
        if (ma->baseExpr && ma->baseExpr->GetTy() &&
            !importManager.IsExtendMemberAccessible(*node.curFile, d, *ma->baseExpr->GetTy())) {
            return false;
        }
    }
    if (d.TestAttr(Attribute::PUBLIC)) {
        return true;
    }
    CJC_ASSERT(node.curFile && node.curFile->curPackage);
    auto relation = Modules::GetPackageRelation(node.curFile->curPackage->fullPackageName, d.GetFullPackageName());
    // `flag` indicates whether the type name is used to construct an object outside a type declaration
    // or in a type without inheritance relationship.
    bool flag = IsClassOrEnumConstructor(d) && !d.TestAttr(Attribute::PRIVATE) && IsNormalCtorRef(node) &&
        (!curComposite || !SearchTargetDeclForProtectMember(*curComposite->node, *d.outerDecl, typeManager));
    if (d.TestAttr(Attribute::GLOBAL) || flag) {
        // When decl is private it can only be accessed in same file,
        if (d.TestAttr(Attribute::PRIVATE)) {
            // In the LSP, the 'node' may be a new ast node, 'curFile' pointer consistency cannot be ensured.
            return node.curFile && d.curFile && *node.curFile == *d.curFile;
        }
        return Modules::IsVisible(d, relation);
    }
    Ptr<Decl> outerDeclOfTarget = d.outerDecl;
    if (outerDeclOfTarget == nullptr || !outerDeclOfTarget->IsNominalDecl()) {
        return true; // Access local decls, must be valid (outerDecl is empty or non-nominal decl).
    }
    CJC_ASSERT(!curComposite || curComposite->node);
    // 1. When decl is internal, checking the package relation.
    if (d.TestAttr(Attribute::INTERNAL)) {
        return Modules::IsVisible(d, relation);
    } else if (d.TestAttr(Attribute::PROTECTED)) {
        // 2. when decl is protected, checking the package relation, if relation is none, checking composite relation.
        // 3. Access decl in same composite decl is always allowed.
        return Modules::IsVisible(d, relation) ||
            (curComposite &&
                (curComposite->node == outerDeclOfTarget ||
                    SearchTargetDeclForProtectMember(*curComposite->node, *outerDeclOfTarget, typeManager)));
    }
    // 4. otherwise accessing private decl must inside same decl.
    auto expectedOuter = outerDeclOfTarget;
    if (expectedOuter->specificImplementation) {
        expectedOuter = expectedOuter->specificImplementation;
    }
    return curComposite && curComposite->node == expectedOuter;
}

Ptr<Decl> FindCorrespondingCommonDecl(const Decl& specificDecl)
{
    if (specificDecl.curFile == nullptr || specificDecl.curFile->curPackage == nullptr) {
        return nullptr;
    }

    for (const auto& file : specificDecl.curFile->curPackage->files) {
        for (const auto& decl : file->decls) {
            if (decl->specificImplementation == &specificDecl) {
                return decl;
            }
        }
    }

    return nullptr;
}
} // namespace Cangjie::TypeCheckUtil
