// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the TypeChecker related classes.
 */

#include "Diags.h"
#include "JoinAndMeet.h"
#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace TypeCheckUtil;
using namespace AST;
using namespace Sema;

namespace {
std::string ConstructDiagInfoForCycle(const Decl& root, const std::deque<Ptr<Decl>>& path)
{
    bool cycle{false};
    std::string str;
    for (auto& i : path) {
        CJC_NULLPTR_CHECK(i);
        if (i == &root) {
            cycle = true;
        }
        if (!cycle) {
            continue;
        }
        i->EnableAttr(Attribute::IN_REFERENCE_CYCLE);
        str += i->identifier + "->";
    }
    str += root.identifier;
    return str;
}

void MarkInvalidInheritanceForNonClassLike(InheritableDecl& id)
{
    CJC_ASSERT(!id.IsClassLikeDecl());
    for (auto& type : id.inheritedTypes) {
        if (auto decl = Ty::GetDeclPtrOfTy(type->ty); decl && !decl->IsClassLikeDecl()) {
            // Non-classlike decl cannot inherit non-classlike decl, add mark avoid invalid type substitution.
            id.EnableAttr(Attribute::IN_REFERENCE_CYCLE);
        }
    }
}

TypeSubst GetSubstituteMap(const TypeManager& tyMgr, const FuncDecl& decl1, const FuncDecl& decl2)
{
    // Generate typeMapping from the 'decl1' to the 'decl2'.
    TypeSubst substituteMap = tyMgr.GenerateGenericMappingFromGeneric(decl1, decl2);
    // Notice: these two functions must be in the same composite type or its extend, should make sure by call side.
    if (decl1.outerDecl && decl2.outerDecl) {
        // Generate typeMapping from the outerDecl of 'decl1' to the outerDecl of 'decl2'.
        auto parentMap = tyMgr.GenerateGenericMappingFromGeneric(*decl1.outerDecl, *decl2.outerDecl);
        substituteMap.merge(parentMap);
    }
    return substituteMap;
}

/**
 * Find whether the declMap in current context @p ctx has declaration of @p names and astKind @p target.
 * Note that names is a pair of type <string, string>, whose first element is the declaration name and the second
 * element is the name of the scope it locates, which is the key of declMap.
 */
bool FindASTKindInDeclMap(const ASTContext& ctx, const Names& names, const ASTKind target)
{
    auto decls = ctx.GetDeclsByName(names);
    for (auto decl : decls) {
        if (decl->astKind == target) {
            return true;
        }
    }
    return false;
}

/**
 * Check recursively in upper bounds when the typeArg is another generic parameter.
 * NOTE: Only generic param decls which defined for same declaration may be used recursively.
 * For example: error should be report when T < Option<U>, U <: T since Option is not class/interface.
 */
bool IsGenericParamExistInUpperBounds(GenericsTy& gTy, Ty& upper)
{
    // If exist any invalid ty, we can skip checking.
    if (!Ty::IsTyCorrect(&upper)) {
        return false;
    }
    CJC_NULLPTR_CHECK(gTy.decl);
    std::unordered_set<Ptr<Ty>> visited{&gTy};
    std::queue<Ptr<Ty>> q;
    q.push(&upper);
    while (!q.empty()) {
        auto curTy = q.front();
        q.pop();
        if (curTy == &gTy) {
            return true;
        }
        if (auto [_, success] = visited.emplace(curTy); !success) {
            continue;
        }
        for (auto& arg : curTy->typeArgs) {
            CJC_ASSERT(arg);
            if (!arg->IsGeneric()) {
                q.push(arg);
                continue;
            }
            auto genericTy = RawStaticCast<GenericsTy*>(arg);
            if (genericTy == &gTy) {
                return true;
            }
            // If genericTys are not belong to same declaration, the self recursion must not exists.
            CJC_NULLPTR_CHECK(genericTy->decl);
            if (genericTy->decl->outerDecl != gTy.decl->outerDecl) {
                continue;
            }
            for (auto& it : genericTy->upperBounds) {
                CJC_NULLPTR_CHECK(it);
                q.push(it);
            }
        }
    }
    return false;
}

bool AreUpperBoundsDirectlyRecursive(std::set<Ptr<GenericsTy>> visitedGenerics, Ty& upperBound)
{
    if (!upperBound.IsGeneric()) {
        return false;
    }
    auto genericTy = RawStaticCast<GenericsTy*>(&upperBound);
    if (visitedGenerics.find(genericTy) != visitedGenerics.end()) {
        return true;
    }
    visitedGenerics.insert(genericTy);
    for (auto& it : genericTy->upperBounds) {
        CJC_NULLPTR_CHECK(it);
        if (AreUpperBoundsDirectlyRecursive(visitedGenerics, *it)) {
            return true;
        }
    }
    return false;
}

void CreateGenericConstraints(Generic& generic)
{
    for (auto& param : generic.typeParameters) {
        auto genericTy = DynamicCast<GenericsTy*>(param->ty);
        if (genericTy == nullptr) {
            continue;
        }
        generic.assumptionCollection.emplace(genericTy, genericTy->upperBounds);
        auto gc = MakeOwnedNode<GenericConstraint>();
        CopyBasicInfo(&generic, gc.get());
        // Sort upper bound tys. To ensure the compiler added upperBounds are in stable order.
        std::set<Ptr<Ty>, CmpTyByName> sortedUpperTys;
        sortedUpperTys.insert(genericTy->upperBounds.begin(), genericTy->upperBounds.end());
        for (auto& it : sortedUpperTys) {
            auto sub = MakeOwnedNode<RefType>();
            sub->ty = genericTy;
            CopyBasicInfo(&generic, sub.get());
            auto upper = MakeOwnedNode<RefType>();
            upper->ty = it;
            CopyBasicInfo(&generic, upper.get());
            gc->type = std::move(sub);
            gc->upperBounds.push_back(std::move(upper));
        }
        if (!gc->upperBounds.empty()) {
            generic.genericConstraints.push_back(std::move(gc));
        }
    }
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckRedefinition(ASTContext& ctx)
{
    std::vector<Symbol*> syms;
    auto enableMacroInLSP = ci->invocation.globalOptions.enableMacroInLSP;
    std::function<VisitAction(Ptr<Node>)> collector = [&syms, &collector, &enableMacroInLSP](auto node) {
        // Collect all decls with symbol, except decls that do not have name.
        static std::vector<ASTKind> ignoredKinds = {
            ASTKind::PRIMARY_CTOR_DECL, ASTKind::EXTEND_DECL, ASTKind::VAR_WITH_PATTERN_DECL};
        if (auto decl = DynamicCast<Decl*>(node); decl && !decl->TestAttr(Attribute::IS_BROKEN) && decl->symbol &&
            !Utils::In(decl->astKind, ignoredKinds) && decl->identifier != WILDCARD_CHAR) {
            syms.emplace_back(decl->symbol);
        }
        if (enableMacroInLSP && node->astKind == ASTKind::FILE) {
            auto file = StaticAs<ASTKind::FILE>(node);
            // Walk decls in macrocall to find references, for lsp.
            for (auto& it : file->originalMacroCallNodes) {
                Walker(it.get(), collector).Walk();
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(ctx.curPackage, collector).Walk();

    CheckRedefinitionInDeclHelper(ctx, syms);
    CheckConflictDeclWithSubPackage(*ctx.curPackage);
}

void TypeChecker::TypeCheckerImpl::CollectDeclMapAndCheckRedefinitionForOneSymbol(
    ASTContext& ctx, const Symbol& sym, const Names& names)
{
    CJC_NULLPTR_CHECK(sym.node);
    // 1. Duplicate decl with wildcard pattern is allowed.
    bool isWildCard = false;
    if (auto vwp = AST::As<ASTKind::VAR_WITH_PATTERN_DECL>(sym.node); vwp && vwp->irrefutablePattern != nullptr) {
        isWildCard = vwp->irrefutablePattern->astKind == ASTKind::WILDCARD_PATTERN;
    }
    // 2. Function redefinition will not be checked in this phase.
    bool funcOverloading = sym.astKind == ASTKind::FUNC_DECL && (FindASTKindInDeclMap(ctx, names, ASTKind::FUNC_DECL));
    // 3. Macro function redefinition will not be checked in this phase
    bool macroOverloading = sym.node->IsMacroCallNode() ||
        (sym.astKind == ASTKind::MACRO_DECL && (FindASTKindInDeclMap(ctx, names, ASTKind::MACRO_DECL)));
    auto currDecl = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, sym.scopeName);
    // 4. VarDecl and FuncDecl as constructor in enumDecl can overload, other's not.
    bool isEnumConstructor = currDecl && currDecl->astKind == ASTKind::ENUM_DECL &&
        sym.node->TestAttr(Attribute::ENUM_CONSTRUCTOR) && !FindASTKindInDeclMap(ctx, names, sym.astKind);
    // 5. Constructor and main entry will not conflict with none function decls.
    //    Function confliction will be checked later.
    bool ignoredHere = isWildCard || funcOverloading || macroOverloading || isEnumConstructor ||
        sym.node->TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::MAIN_ENTRY);
    if (ignoredHere) {
        ctx.AddDeclName(names, StaticCast<Decl>(*sym.node));
        return;
    }
    auto found = ctx.GetDeclsByName(names);
    if (!found.empty()) {
        bool privateGlobalInDifferentFile = sym.node->TestAttr(Attribute::GLOBAL, Attribute::PRIVATE) &&
            found.front()->TestAttr(Attribute::GLOBAL, Attribute::PRIVATE) &&
            sym.node->curFile != found.front()->curFile;
        bool multiPlat =
            sym.node->TestAttr(Cangjie::AST::Attribute::COMMON) && found.front()->TestAttr(Attribute::PLATFORM);
        multiPlat =
            multiPlat || (sym.node->TestAttr(Attribute::PLATFORM) && found.front()->TestAttr(Attribute::COMMON));
        if (!privateGlobalInDifferentFile && !multiPlat) {
            DiagRedefinitionWithFoundNode(diag, StaticCast<Decl>(*sym.node), *found.front());
        }
    }
    ctx.AddDeclName(names, StaticCast<Decl>(*sym.node));
}

void TypeChecker::TypeCheckerImpl::CheckRedefinitionInDeclHelper(ASTContext& ctx, std::vector<Symbol*>& syms)
{
    auto ignoredMacroDecl = [](const Node& node) {
        return node.astKind == ASTKind::MACRO_EXPAND_DECL || node.TestAttr(Attribute::MACRO_INVOKE_FUNC);
    };
    for (auto sym : syms) {
        // macro expanded decls are not added into declMap.
        // macro invoke func can NOT be seen by developer so should not be added.
        if (ignoredMacroDecl(*sym->node)) {
            continue;
        }
        std::string scopeName = ScopeManagerApi::GetScopeNameWithoutTail(sym->scopeName);
        auto names = std::make_pair(sym->name, scopeName);
        if (ctx.curPackage->TestAttr(Attribute::TOOL_ADD)) {
            ctx.AddDeclName(names, StaticCast<Decl>(*sym->node));
            continue; // Ignore redefinition checking for cjogen package.
        }
        CollectDeclMapAndCheckRedefinitionForOneSymbol(ctx, *sym, names);
    }
}

void TypeChecker::TypeCheckerImpl::CheckConflictDeclWithSubPackage(const Package& pkg)
{
    if (pkg.files.empty()) {
        return;
    }
    std::unordered_set<std::string> subPkgNames;
    auto subDirs = FileUtil::GetDirectories(FileUtil::GetDirPath(pkg.files[0]->filePath));
    for (auto& it : subDirs) {
        subPkgNames.emplace(it.name);
    }
    std::vector<Ptr<Decl>> toplevelDecls;
    IterateToplevelDecls(pkg, [&toplevelDecls](auto& decl) {
        if (decl->TestAttr(Attribute::IS_BROKEN)) {
            return;
        }
        if (auto vpd = DynamicCast<VarWithPatternDecl*>(decl.get())) {
            for (auto it : FlattenVarWithPatternDecl(*vpd)) {
                if (auto vp = DynamicCast<VarPattern*>(it)) {
                    toplevelDecls.emplace_back(vp->varDecl.get());
                }
            }
        } else {
            toplevelDecls.emplace_back(decl.get());
        }
    });
    for (auto decl : toplevelDecls) {
        if (subPkgNames.count(decl->identifier) != 0) {
            auto fullSubName = pkg.fullPackageName + "." + decl->identifier;
            diag.DiagnoseRefactor(DiagKindRefactor::sema_conflict_with_sub_package, *decl,
                MakeRange(decl->identifier), decl->identifier.Val(), fullSubName);
        }
    }
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromASTType(ASTContext& ctx, Ptr<Node> type)
{
    if (type == nullptr) {
        return TypeManager::GetInvalidTy();
    }
    // If ty is not nullptr, it means this type's ty is already set, no matter whether this type is legal or not.
    if (!Ty::IsInitialTy(type->ty)) {
        return type->ty;
    }
    switch (type->astKind) {
        case ASTKind::PRIMITIVE_TYPE: {
            auto primitiveType = StaticAs<ASTKind::PRIMITIVE_TYPE>(type);
            primitiveType->ty = TypeManager::GetPrimitiveTy(primitiveType->kind);
            return primitiveType->ty;
        }
        case ASTKind::REF_TYPE: {
            auto refType = StaticAs<ASTKind::REF_TYPE>(type);
            refType->ty = GetTyFromASTType(ctx, *refType);
            return refType->ty;
        }
        case ASTKind::QUALIFIED_TYPE: {
            auto qualifiedType = StaticAs<ASTKind::QUALIFIED_TYPE>(type);
            qualifiedType->ty = GetTyFromASTType(ctx, *qualifiedType);
            return qualifiedType->ty;
        }
        case ASTKind::VARRAY_TYPE: {
            auto varrayType = StaticAs<ASTKind::VARRAY_TYPE>(type);
            varrayType->ty = GetTyFromASTType(ctx, *varrayType);
            return varrayType->ty;
        }
        case ASTKind::TUPLE_TYPE: {
            auto tupleType = StaticAs<ASTKind::TUPLE_TYPE>(type);
            tupleType->ty = GetTyFromASTType(ctx, *tupleType);
            return tupleType->ty;
        }
        case ASTKind::PAREN_TYPE: {
            auto parenType = StaticAs<ASTKind::PAREN_TYPE>(type);
            parenType->ty = GetTyFromASTType(ctx, parenType->type.get());
            return parenType->ty;
        }
        case ASTKind::FUNC_TYPE: {
            auto funcType = StaticAs<ASTKind::FUNC_TYPE>(type);
            funcType->ty = GetTyFromASTType(ctx, *funcType);
            return funcType->ty;
        }
        case ASTKind::OPTION_TYPE: {
            auto optionType = StaticAs<ASTKind::OPTION_TYPE>(type);
            optionType->ty = GetTyFromASTType(ctx, *optionType);
            return optionType->ty;
        }
        case ASTKind::INVALID_TYPE: {
            auto invalidType = StaticAs<ASTKind::INVALID_TYPE>(type);
            invalidType->ty = RawStaticCast<Ty*>(TypeManager::GetInvalidTy());
            return invalidType->ty;
        }
        case ASTKind::THIS_TYPE:
            CJC_ABORT(); // ThisType should not enter current func.
            // Fall-through
        default: {
            return TypeManager::GetInvalidTy();
        }
    }
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromASTType(ASTContext& ctx, RefType& rt)
{
    // If rt.ty is nullptr but rt.ref.target is packageDecl, rt is the base of another qualified type and is already
    // resolved.
    if ((rt.ref.target && rt.ref.target->astKind == ASTKind::PACKAGE_DECL) || !Ty::IsInitialTy(rt.ty)) {
        return rt.ty;
    }
    // Get scope target.
    auto targets = LookupTopLevel(ctx, rt.ref.identifier, rt.scopeName, rt);
    // A copy of scopeTargets for better error messages.
    // If non types are defined, error message will be 'xxx' is not a type
    // If cannot find any declarations, error message will be undeclared type name 'xxx'
    auto allTargets = std::vector<Ptr<Decl>>(targets);

    // Remove all non-type decls.
    Utils::EraseIf(targets, [](Ptr<const Decl> d) { return !d || !d->IsTypeDecl(); });

    std::sort(targets.begin(), targets.end(),
        [](Ptr<const Decl> d1, Ptr<const Decl> d2) {
            if (d1->scopeLevel > d2->scopeLevel) {
                return true;
            } else if (d1->scopeLevel == d2->scopeLevel) {
                // Ranking overloads also by platform > common
                if (d1->TestAttr(Attribute::PLATFORM)) {
                    return true;
                }
            }
            return false;
        });

    Ptr<Decl> target{nullptr};
    if (targets.empty()) {
        if (!ctx.packageDecls.empty()) {
            // a.b.c, ref a not found, but packagedecl of [a a.b] may exist.
            return TypeManager::GetInvalidTy();
        }
        if (allTargets.empty()) {
            diag.Diagnose(rt, DiagKind::sema_undeclared_type_name, rt.ref.identifier.Val());
        } else {
            diag.Diagnose(rt, DiagKind::sema_not_a_type, rt.ref.identifier.Val());
        }
        return TypeManager::GetInvalidTy();
    }

    if (targets.size() == 1 ||
        // The other case must be scopeTargets.size() > 1, so no need to check
        // this condition. scopeTargets is sorted, so only need to check whether
        // scope levels are equal or not. If they are equal then rt is ambiguous.
        (targets[0]->scopeLevel > targets[1]->scopeLevel) ||
        // With equal scope levels platform declaration are prefered over the common ones
        (targets[0]->TestAttr(Attribute::PLATFORM))) {
        target = targets[0];
        if (auto builtin = DynamicCast<BuiltInDecl>(target); builtin && builtin->type == BuiltInType::CFUNC) {
            auto ty = GetTyFromASTCFuncType(ctx, rt);
            rt.ref.target = target;
            return ty;
        }
        ReplaceTarget(&rt, target);
        // Get semaTy only by name, no need to check inside.
        auto typeArgs = GetTyFromASTType(ctx, rt.typeArguments);
        rt.ty = GetTyFromASTType(*target, typeArgs);
        return rt.ty;
    }
    // If there are more than one targets and the scope are the same then report ambiguous error.
    DiagAmbiguousUse(diag, rt, rt.ref.identifier, targets, importManager);
    return TypeManager::GetInvalidTy();
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromASTCFuncType(ASTContext& ctx, RefType& rt)
{
    if (rt.typeArguments.size() != 1) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_generic_argument_no_match, rt, MakeRange(rt.GetBegin(), rt.GetEnd()));
        return TypeManager::GetInvalidTy();
    }
    auto funcType = DynamicCast<FuncType>(&*rt.typeArguments[0]);
    if (!funcType) {
        diag.Diagnose(rt.typeArguments[0]->GetBegin(), rt.typeArguments[0]->GetEnd(), DiagKind::sema_cfunc_type);
        return TypeManager::GetInvalidTy();
    }
    std::vector<Ptr<Ty>> paramTys;
    for (size_t i{0}; i < funcType->paramTypes.size(); ++i) {
        auto& param = funcType->paramTypes[i];
        param->ty = GetTyFromASTType(ctx, &*param);
        paramTys.push_back(param->ty);
    }
    Ptr<Ty> retTy = funcType->retType->ty = GetTyFromASTType(ctx, funcType->retType.get());
    funcType->ty = GetTyFromASTType(ctx, funcType);
    return typeManager.GetFunctionTy(std::move(paramTys), retTy, {.isC = true});
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromASTType(ASTContext& ctx, QualifiedType& qt)
{
    // If qt.ty is nullptr but qt.target is packageDecl, qt is the base of another qualified type and is already
    // resolved.
    bool earlyQuit = (qt.target && qt.target->astKind == ASTKind::PACKAGE_DECL) || !Ty::IsInitialTy(qt.ty);
    if (earlyQuit) {
        return qt.ty;
    }
    Ptr<Type> baseType = qt.baseType.get();
    CJC_ASSERT(baseType != nullptr);
    if (baseType->astKind == ASTKind::INVALID_TYPE) {
        return TypeManager::GetInvalidTy();
    }
    std::string packageName = ASTContext::GetPackageName(baseType);
    auto [packageDecl, isConflicted] = importManager.GetImportedPackageDecl(&qt, packageName);
    if (isConflicted) {
        diag.Diagnose(qt, DiagKind::sema_package_name_conflict, packageName);
        return TypeManager::GetInvalidTy();
    } else if (packageDecl) {
        ReplaceTarget(baseType, packageDecl);
        // Base node of current package qualifier node is useless.
        // So set all parent nodes as type of invalid to avoid checking their types.
        auto curType = &qt;
        do {
            CJC_NULLPTR_CHECK(curType->baseType);
            curType->baseType->ty = TypeManager::GetInvalidTy();
            curType = DynamicCast<QualifiedType*>(curType->baseType.get());
        } while (curType != nullptr);
    } else {
        baseType->ty = GetTyFromASTType(ctx, baseType);
    }
    Ptr<Decl> targetOfBaseType = baseType->GetTarget();
    // Check targetOfBaseType.
    std::vector<Ptr<Decl>> targetsOfField;
    if (targetOfBaseType != nullptr) {
        targetsOfField = FieldLookup(ctx, targetOfBaseType, qt.field, {.lookupInherit = false, .lookupExtend = false});
    }
    if (targetsOfField.empty()) {
        diag.Diagnose(qt, qt.field.Begin(), DiagKind::sema_undeclared_type_name, qt.field.Val());
        return TypeManager::GetInvalidTy();
    }
    auto target = targetsOfField.front();
    CJC_NULLPTR_CHECK(target);
    if (!target->IsTypeDecl()) {
        diag.Diagnose(qt, qt.field.Begin(), DiagKind::sema_not_a_type, qt.field.Val());
        return TypeManager::GetInvalidTy();
    }
    // Bind target-user relationship.
    ReplaceTarget(&qt, target);
    // Get semaTy only by name, no need to check inside.
    auto typeArgs = GetTyFromASTType(ctx, qt.typeArguments);
    qt.ty = GetTyFromASTType(*target, typeArgs);
    return qt.ty;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromASTType(ASTContext& ctx, VArrayType& varrayType)
{
    varrayType.typeArgument->ty = GetTyFromASTType(ctx, varrayType.typeArgument.get());
    auto expr = StaticAs<ASTKind::CONSTANT_TYPE>(varrayType.constantType.get())->constantExpr.get();
    CJC_ASSERT(expr && expr->astKind == ASTKind::LIT_CONST_EXPR);
    auto le = StaticAs<ASTKind::LIT_CONST_EXPR>(expr);
#if CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto lengthLimitKind = TypeKind::TYPE_INT64;
#endif
    le->constNumValue.asInt.InitIntLiteral(le->stringValue, lengthLimitKind);
    le->constNumValue.asInt.SetOutOfRange(Cangjie::TypeManager::GetPrimitiveTy(lengthLimitKind));
    if (le->constNumValue.asInt.IsOutOfRange()) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_exceed_num_value_range, *le, le->stringValue,
#if CANGJIE_CODEGEN_CJNATIVE_BACKEND
            "Int64");
#endif
        return TypeManager::GetInvalidTy();
    }
    varrayType.ty = typeManager.GetVArrayTy(*varrayType.typeArgument->ty, le->constNumValue.asInt.Int64());
    return varrayType.ty;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromASTType(ASTContext& ctx, TupleType& tupleType)
{
    if (!Ty::IsInitialTy(tupleType.ty)) {
        return tupleType.ty;
    }
    std::vector<Ptr<Ty>> elemTy;
    for (auto& fieldType : tupleType.fieldTypes) {
        if (!fieldType) {
            return TypeManager::GetInvalidTy();
        }
        fieldType->ty = GetTyFromASTType(ctx, fieldType.get());
        if (Ty::IsInitialTy(fieldType->ty)) {
            return TypeManager::GetInvalidTy();
        }
        elemTy.push_back(fieldType->ty);
    }
    tupleType.ty = typeManager.GetTupleTy(elemTy);
    return tupleType.ty;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromASTType(ASTContext& ctx, FuncType& funcType)
{
    if (!Ty::IsInitialTy(funcType.ty)) {
        return funcType.ty;
    }
    std::vector<Ptr<Ty>> paramTys;
    for (auto& paramType : funcType.paramTypes) {
        if (!paramType) {
            return TypeManager::GetInvalidTy();
        }
        paramType->ty = GetTyFromASTType(ctx, paramType.get());
        if (Ty::IsInitialTy(paramType->ty)) {
            return TypeManager::GetInvalidTy();
        }
        paramTys.push_back(paramType->ty);
    }
    if (!funcType.retType) {
        return TypeManager::GetInvalidTy();
    }
    funcType.retType->ty = GetTyFromASTType(ctx, funcType.retType.get());
    if (!funcType.retType->ty) {
        return TypeManager::GetInvalidTy();
    }
    funcType.ty = typeManager.GetFunctionTy(paramTys, funcType.retType->ty, {funcType.isC});
    return funcType.ty;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromASTType(ASTContext& ctx, OptionType& optionType)
{
    if (optionType.componentType) {
        optionType.componentType->ty = GetTyFromASTType(ctx, optionType.componentType.get());
    }
    if (optionType.desugarType) {
        optionType.ty = GetTyFromASTType(ctx, *optionType.desugarType);
    } else {
        optionType.ty = TypeManager::GetInvalidTy();
    }
    return optionType.ty;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromASTType(Decl& decl, const std::vector<Ptr<Ty>>& typeArgs)
{
    switch (decl.astKind) {
        case ASTKind::CLASS_DECL: {
            auto cd = StaticAs<ASTKind::CLASS_DECL>(&decl);
            return typeManager.GetClassTy(*cd, typeArgs);
        }
        case ASTKind::INTERFACE_DECL: {
            auto id = StaticAs<ASTKind::INTERFACE_DECL>(&decl);
            return typeManager.GetInterfaceTy(*id, typeArgs);
        }
        case ASTKind::STRUCT_DECL: {
            auto sd = StaticAs<ASTKind::STRUCT_DECL>(&decl);
            return typeManager.GetStructTy(*sd, typeArgs);
        }
        case ASTKind::ENUM_DECL: {
            auto ed = StaticAs<ASTKind::ENUM_DECL>(&decl);
            return typeManager.GetEnumTy(*ed, typeArgs);
        }
        case ASTKind::TYPE_ALIAS_DECL: {
            auto tad = StaticAs<ASTKind::TYPE_ALIAS_DECL>(&decl);
            return typeManager.GetTypeAliasTy(*tad, typeArgs);
        }
        case ASTKind::GENERIC_PARAM_DECL: {
            auto gpd = StaticAs<ASTKind::GENERIC_PARAM_DECL>(&decl);
            return typeManager.GetGenericsTy(*gpd);
        }
        case ASTKind::BUILTIN_DECL: {
            auto bid = StaticAs<ASTKind::BUILTIN_DECL>(&decl);
            return GetTyFromBuiltinDecl(*bid, typeArgs);
        }
        default:
            return decl.ty;
    }
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetTyFromBuiltinDecl(const BuiltInDecl& bid, const std::vector<Ptr<Ty>>& typeArgs)
{
    switch (bid.type) {
        case BuiltInType::ARRAY: {
            return GetBuiltInArrayType(typeArgs);
        }
        case BuiltInType::POINTER: {
            return GetBuiltInPointerType(typeArgs);
        }
        case BuiltInType::CSTRING: {
            return TypeManager::GetCStringTy();
        }
        case BuiltInType::VARRAY: {
            return GetBuiltInVArrayType(typeArgs);
        }
        case BuiltInType::CFUNC: {
            return GetBuiltinCFuncType(typeArgs);
        }
        default:
            return TypeManager::GetInvalidTy();
    }
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetBuiltInPointerType(const std::vector<Ptr<Ty>>& typeArgs)
{
    auto elemTy = typeArgs.empty() ? TypeManager::GetInvalidTy() : typeArgs[0];
    return typeManager.GetPointerTy(elemTy);
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetBuiltInArrayType(const std::vector<Ptr<Ty>>& typeArgs)
{
    if (typeArgs.size() > 1) {
        return TypeManager::GetInvalidTy();
    }
    auto elemTy = typeArgs.empty() ? TypeManager::GetInvalidTy() : typeArgs[0];
    return typeManager.GetArrayTy(elemTy, 1);
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::GetBuiltInVArrayType(const std::vector<Ptr<Ty>>& typeArgs)
{
    CJC_ASSERT(typeArgs.size() == 1);
    auto elemTy = typeArgs.empty() ? TypeManager::GetInvalidTy() : typeArgs[0];
    return typeManager.GetVArrayTy(*elemTy, 0);
}

Ptr<AST::Ty> TypeChecker::TypeCheckerImpl::GetBuiltinCFuncType(const std::vector<Ptr<AST::Ty>>& typeArgs)
{
    // the return type is CFunc<T>
    CJC_ASSERT(typeArgs.size() == 1 && Ty::IsTyCorrect(typeArgs[0]));
    return typeManager.GetFunctionTy(
        typeArgs, typeArgs[0], {.isC = true, .isClosureTy = false, .hasVariableLenArg = false, .noCast = false});
}

std::vector<Ptr<Ty>> TypeChecker::TypeCheckerImpl::GetTyFromASTType(
    const std::vector<OwnedPtr<GenericParamDecl>>& typeParameters)
{
    std::vector<Ptr<Ty>> typeArgs;
    for (auto& gpd : typeParameters) {
        if (Ty::IsInitialTy(gpd->ty)) {
            gpd->ty = GetTyFromASTType(*gpd, {});
        }
        typeArgs.push_back(gpd->ty);
    }
    return typeArgs;
}

std::vector<Ptr<Ty>> TypeChecker::TypeCheckerImpl::GetTyFromASTType(
    ASTContext& ctx, std::vector<OwnedPtr<Type>>& typeArguments)
{
    std::vector<Ptr<Ty>> typeArgs;
    for (auto& arg : typeArguments) {
        if (Ty::IsInitialTy(arg->ty)) {
            arg->ty = GetTyFromASTType(ctx, arg.get());
        }
        typeArgs.push_back(arg->ty);
    }
    return typeArgs;
}

void TypeChecker::TypeCheckerImpl::SetDeclTy(Decl& decl)
{
    std::vector<Ptr<Ty>> typeArgs;
    auto generic = decl.GetGeneric();
    if (generic) {
        typeArgs = GetTyFromASTType(generic->typeParameters);
    }
    decl.ty = GetTyFromASTType(decl, typeArgs);
}

void TypeChecker::TypeCheckerImpl::SetTypeAliasDeclTy(ASTContext& ctx, TypeAliasDecl& tad)
{
    SetDeclTy(tad);
    if (tad.type == nullptr) {
        return;
    }
    tad.type->ty = GetTyFromASTType(ctx, tad.type.get());
    if (!Ty::IsTyCorrect(tad.type->ty)) {
        std::string name;
        if (auto rt = DynamicCast<RefType*>(tad.type.get()); rt) {
            name = rt->ref.identifier;
        } else if (auto qt = DynamicCast<QualifiedType*>(tad.type.get()); qt) {
            name = qt->field;
        } else {
            name = "Unknown type";
        }
        diag.Diagnose(*tad.type, DiagKind::sema_not_a_type, name);
    }
}

void TypeChecker::TypeCheckerImpl::ResolveOneDecl(ASTContext& ctx, Decl& decl)
{
    switch (decl.astKind) {
        case ASTKind::CLASS_DECL:
            if (decl.HasAnno(AnnotationKind::ANNOTATION)) {
                decl.EnableAttr(Attribute::IS_ANNOTATION);
            }
            [[fallthrough]];
        case ASTKind::STRUCT_DECL:
        case ASTKind::INTERFACE_DECL:
        case ASTKind::ENUM_DECL: {
            auto id = RawStaticCast<InheritableDecl*>(&decl);
            SetDeclTy(*id);
            if (decl.TestAttr(Attribute::C) && !id->inheritedTypes.empty()) {
                diag.Diagnose(decl, DiagKind::sema_c_type_cannot_implement_interface, decl.identifier.Val());
            }
            for (auto& superType : id->inheritedTypes) {
                superType->ty = GetTyFromASTType(ctx, superType.get());
                if (auto clt = DynamicCast<ClassLikeTy*>(superType->ty); clt && Ty::IsTyCorrect(id->ty)) {
                    (void)clt->directSubtypes.emplace(id->ty);
                }
            }
            break;
        }
        case ASTKind::FUNC_DECL:
        case ASTKind::GENERIC_PARAM_DECL:
        case ASTKind::BUILTIN_DECL:
        case ASTKind::EXTEND_DECL:
            SetDeclTy(decl);
            break;
        case ASTKind::VAR_WITH_PATTERN_DECL: {
            auto& vpd = StaticCast<VarWithPatternDecl&>(decl);
            Walker(vpd.irrefutablePattern.get(), [&ctx, &vpd](auto node) {
                if (auto vd = DynamicCast<VarDecl*>(node)) {
                    // Collect mapping from VarDecl to the outer VarWithPatternDecl.
                    ctx.StoreOuterVarWithPatternDecl(*vd, vpd);
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
            break;
        }
        default:
            break;
    }
}

void TypeChecker::TypeCheckerImpl::ResolveDecls(ASTContext& ctx)
{
    std::vector<Symbol*> syms = GetAllDecls(ctx);
    for (auto& sym : syms) {
        if (sym->node->astKind == ASTKind::VAR_DECL || sym->node->astKind == ASTKind::FUNC_DECL) {
            SetOuterFunctionDecl(*StaticCast<Decl*>(sym->node));
        }
    }
    for (auto& sym : syms) {
        if (auto decl = AST::As<ASTKind::DECL>(sym->node); decl) {
            ResolveOneDecl(ctx, *decl);
        }
    }
}

void TypeChecker::TypeCheckerImpl::ResolveTypeAlias(const std::vector<Ptr<ASTContext>>& contexts)
{
    auto setTypAliasFunc = [this](ASTContext& ctx, TypeAliasDecl& tad) { return SetTypeAliasDeclTy(ctx, tad); };
    auto substituteFunc = [this](ASTContext&, TypeAliasDecl& tad) { return SubstituteTypeAliasForAlias(tad); };
    // Resolve all targets for typeAlias decls.
    for (auto ctx : contexts) {
        IterateToplevelDecls(*ctx->curPackage, [&ctx, &setTypAliasFunc](const OwnedPtr<Decl>& decl) {
            if (decl->astKind == ASTKind::TYPE_ALIAS_DECL) {
                setTypAliasFunc(*ctx, *StaticAs<ASTKind::TYPE_ALIAS_DECL>(decl.get()));
            }
        });
    }
    for (auto ctx : contexts) {
        // TypeAlias circle check before substitution.
        TypeAliasCircleCheck(*ctx);
        // Substitute typealiased decl types.
        IterateToplevelDecls(*ctx->curPackage, [&ctx, &substituteFunc](const OwnedPtr<Decl>& decl) {
            if (decl->astKind == ASTKind::TYPE_ALIAS_DECL) {
                substituteFunc(*ctx, *StaticAs<ASTKind::TYPE_ALIAS_DECL>(decl.get()));
            }
        });
    }
}

void TypeChecker::TypeCheckerImpl::SetTypeTy(ASTContext& ctx, Type& type)
{
    type.ty = GetTyFromASTType(ctx, &type);
    type.ty = SubstituteTypeAliasInTy(*type.ty);
}

void TypeChecker::TypeCheckerImpl::SubstituteTypeAliasForAlias(TypeAliasDecl& tad)
{
    auto resolveTypes = [this](Ptr<Node> node) -> VisitAction {
        switch (node->astKind) {
            case ASTKind::PRIMARY_CTOR_DECL:
                return VisitAction::SKIP_CHILDREN;
            case ASTKind::PRIMITIVE_TYPE:
            case ASTKind::REF_TYPE:
            case ASTKind::QUALIFIED_TYPE:
            case ASTKind::TUPLE_TYPE:
            case ASTKind::PAREN_TYPE:
            case ASTKind::FUNC_TYPE:
            case ASTKind::OPTION_TYPE: {
                auto type = StaticAs<ASTKind::TYPE>(node);
                CJC_ASSERT(type->ty != nullptr);
                type->ty = SubstituteTypeAliasInTy(*type->ty);
                return VisitAction::WALK_CHILDREN;
            }
            default:
                break;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&tad, resolveTypes).Walk();
}

void TypeChecker::TypeCheckerImpl::ResolveNames(ASTContext& ctx)
{
    auto id = Walker::GetNextWalkerID();
    // NOTE: ThisType should be resolved with function and it's parent.
    auto resolveSingleType = [&ctx, id, this](Ptr<Node> node) -> VisitAction {
        switch (node->astKind) {
            case ASTKind::REF_TYPE:
            case ASTKind::PRIMITIVE_TYPE:
            case ASTKind::QUALIFIED_TYPE:
            case ASTKind::VARRAY_TYPE:
            case ASTKind::TUPLE_TYPE:
            case ASTKind::PAREN_TYPE:
            case ASTKind::FUNC_TYPE:
            case ASTKind::OPTION_TYPE: {
                auto type = StaticAs<ASTKind::TYPE>(node);
                SetTypeTy(ctx, *type);
                if (auto qt = DynamicCast<QualifiedType*>(node); qt && qt->TestAttr(Attribute::IS_CHECK_VISITED)) {
                    // Update id to ignore baseType for unchanged type in incremental compilation.
                    CJC_NULLPTR_CHECK(qt->baseType);
                    qt->baseType->visitedByWalkerID = id;
                }
                return VisitAction::WALK_CHILDREN;
            }
            default:
                break;
        }
        return VisitAction::WALK_CHILDREN;
    };
    std::vector<Symbol*> syms = GetToplevelDecls(ctx);
    for (auto& sym : syms) {
        CJC_NULLPTR_CHECK(sym);
        Walker(sym->node, id, resolveSingleType).Walk();
    }
    if (ci->invocation.globalOptions.enableMacroInLSP) {
        for (auto& file : ctx.curPackage->files) {
            for (auto& it : file->originalMacroCallNodes) {
                Walker(it.get(), id, resolveSingleType).Walk();
            }
        }
    }
}

bool TypeChecker::TypeCheckerImpl::CheckAndReduceUpperBounds(
    GenericsTy& genericTy, const std::set<Ptr<Ty>>& upperBounds)
{
    if (!Ty::AreTysCorrect(upperBounds)) {
        return false;
    }
    if (upperBounds.size() <= 1) {
        return true;
    }
    auto joinAndMeet = JoinAndMeet(typeManager, upperBounds);
    auto meetRes = joinAndMeet.MeetAsVisibleTy();
    if (std::get_if<Ptr<Ty>>(&meetRes)) {
        auto maxCommonChildTy = std::get<Ptr<Ty>>(meetRes);
        for (auto& it : upperBounds) {
            if (it != maxCommonChildTy) {
                genericTy.upperBounds.erase(it);
            }
        }
        return true;
    } else {
        genericTy.isUpperBoundLegal = false;
        genericTy.upperBounds.clear();
        return false;
    }
}

bool TypeChecker::TypeCheckerImpl::ValidRecursiveConstraintCheck(const Generic& generic)
{
    for (auto& it : generic.typeParameters) {
        CJC_NULLPTR_CHECK(it);
        auto genericTy = DynamicCast<GenericsTy*>(it->ty);
        if (!genericTy) {
            continue;
        }
        for (auto& upper : genericTy->upperBounds) {
            CJC_NULLPTR_CHECK(upper);
            if (upper->IsGeneric() && AreUpperBoundsDirectlyRecursive(std::set<Ptr<GenericsTy>>(), *upper)) {
                auto upperGenericTy = DynamicCast<GenericsTy*>(upper);
                CJC_NULLPTR_CHECK(upperGenericTy);
                upperGenericTy->isUpperBoundLegal = false;
                CJC_NULLPTR_CHECK(genericTy->decl);
                diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_param_directly_recursive, *genericTy->decl,
                    genericTy->decl->identifier, upperGenericTy->String());
                return false;
            }
            if (!upper->IsClassLike() && IsGenericParamExistInUpperBounds(*genericTy, *upper)) {
                genericTy->isUpperBoundLegal = false;
                CJC_NULLPTR_CHECK(genericTy->decl);
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_generic_param_exist_in_class_irrelevant_upperbound_recursively,
                    *genericTy->decl, genericTy->decl->identifier, upper->String());
                return false;
            }
        }
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::CheckUpperBoundsLegality(const Generic& generic)
{
    for (auto& gp : generic.typeParameters) {
        CJC_NULLPTR_CHECK(gp);
        auto genericsTy = DynamicCast<GenericsTy*>(gp->ty);
        if (!genericsTy) {
            return false;
        }
        for (auto& upper : genericsTy->upperBounds) {
            CJC_NULLPTR_CHECK(upper);
            if (!CheckUpperBoundsLegalityRecursively(*upper)) {
                return false;
            }
        }
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::CheckUpperBoundsLegalityRecursively(const Ty& upper)
{
    auto decl = Ty::GetDeclPtrOfTy(&upper);
    if (!decl) {
        return true; // If upper is not user defined type decl, just treat current checking as passed.
    }
    auto typeArgs = upper.typeArgs;
    if (!typeManager.CheckGenericDeclInstantiation(decl, typeArgs)) {
        return false;
    }
    for (auto& it : typeArgs) {
        if (!CheckUpperBoundsLegalityRecursively(*it)) {
            return false;
        }
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::SanityCheckForClassUpperBounds(
    GenericsTy& genericTy, const std::set<Ptr<Ty>>& classUpperBounds)
{
    if (!CheckAndReduceUpperBounds(genericTy, classUpperBounds)) {
        auto diagInfo = Ty::GetTypesToStableStr(classUpperBounds, ", ");
        CJC_NULLPTR_CHECK(genericTy.decl);
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_multiple_class_upperbounds, *genericTy.decl, genericTy.decl->identifier, diagInfo);
        // NOTE: Use 'IN_REFERENCE_CYCLE' attribute to mark invalid upperbounds for genericParamDecl.
        genericTy.decl->EnableAttr(Attribute::IN_REFERENCE_CYCLE);
    }
}

void TypeChecker::TypeCheckerImpl::SanityCheckForOneGenericTy(GenericsTy& genericTy)
{
    std::set<Ptr<Ty>> classUpperBounds;
    bool invalidUpperbound = false;
    std::set<Ptr<Ty>, CmpTyByName> sortedUpperTys;
    sortedUpperTys.insert(genericTy.upperBounds.begin(), genericTy.upperBounds.end());
    for (auto& upper : sortedUpperTys) {
        CJC_NULLPTR_CHECK(upper);
        if (upper->IsAny() || upper->IsCType()) {
            continue; // Ignore for top type and ctype constraint.
        }
        if (upper->kind == TypeKind::TYPE_CLASS) {
            classUpperBounds.emplace(upper);
        } else if (upper->kind != TypeKind::TYPE_INTERFACE) {
            // Rule 1: There can only be interface or class bounds.
            diag.DiagnoseRefactor(DiagKindRefactor::sema_upper_bound_must_be_class_or_interface, *genericTy.decl,
                upper->String(), genericTy.String());
            invalidUpperbound = true;
            continue;
        }
    }
    if (invalidUpperbound) {
        return;
    }
    // Rule 2: If there are multiple classes, they must be in one inheritance chain.
    SanityCheckForClassUpperBounds(genericTy, classUpperBounds);
}

void TypeChecker::TypeCheckerImpl::AssumptionSanityCheck(const Generic& generic)
{
    // Rule 1: There can be no recursive constraint if the upper bound of a type argument is a class irrelevant type.
    if (!ValidRecursiveConstraintCheck(generic)) {
        return;
    }
    // Do sanity check recursively according to rule 2-5.
    for (auto& it : generic.typeParameters) {
        auto genericTy = DynamicCast<GenericsTy*>(it->ty);
        if (!genericTy) {
            continue;
        }
        SanityCheckForOneGenericTy(*genericTy);
    }
}

void TypeChecker::TypeCheckerImpl::CollectAssumption(ASTContext& ctx, const Decl& decl)
{
    switch (decl.astKind) {
        case ASTKind::FUNC_DECL:
        case ASTKind::BUILTIN_DECL:
        case ASTKind::CLASS_DECL:
        case ASTKind::INTERFACE_DECL:
        case ASTKind::STRUCT_DECL:
        case ASTKind::ENUM_DECL:
        case ASTKind::EXTEND_DECL: {
            auto generic = decl.GetGeneric();
            if (generic != nullptr) {
                Assumption(generic->assumptionCollection, ctx.gcBlames, decl);
            }
            break;
        }
        default:
            break;
    }
}

void TypeChecker::TypeCheckerImpl::ExposeGenericUpperBounds(ASTContext& ctx, const Generic& generic) const
{
    for (auto& it : generic.typeParameters) {
        CJC_NULLPTR_CHECK(it);
        auto genericTy = DynamicCast<GenericsTy*>(it->ty);
        if (!genericTy) {
            continue;
        }
        std::set<Ptr<Ty>> exposedUpperBounds(genericTy->upperBounds.begin(), genericTy->upperBounds.end());
        std::queue<Ptr<GenericsTy>> q;
        q.push(genericTy);
        std::unordered_set<Ptr<GenericsTy>> visited = {};
        while (!q.empty()) {
            auto gTy = q.front();
            q.pop();
            if (auto [_, success] = visited.emplace(gTy); !success) {
                continue;
            }
            for (auto upper : gTy->upperBounds) {
                if (upper->IsGeneric()) {
                    q.push(RawStaticCast<GenericsTy*>(upper));
                } else {
                    (void)exposedUpperBounds.emplace(upper);
                    auto& srcBlames = ctx.gcBlames[gTy][upper];
                    ctx.gcBlames[genericTy][upper].insert(srcBlames.begin(), srcBlames.end());
                }
            }
        }
        (void)exposedUpperBounds.erase(genericTy); // Erase itself to avoid invalid looping.
        genericTy->upperBounds = std::move(exposedUpperBounds);
    }
}

void TypeChecker::TypeCheckerImpl::CheckAssumption(ASTContext& ctx, const Decl& decl)
{
    if (decl.TestAttr(Attribute::COMMON) && decl.TestAttr(Attribute::FROM_COMMON_PART)) {
        return;
    }

    auto generic = decl.GetGeneric();
    if (generic == nullptr) {
        return;
    }
    if (decl.TestAttr(Attribute::TOOL_ADD)) {
        ExposeGenericUpperBounds(ctx, *generic);
        return;
    }
    CheckGenericConstraints(ctx, *generic);
    if (!CheckUpperBoundsLegality(*generic)) {
        diag.Diagnose(decl, DiagKind::sema_generic_type_argument_not_match_constraint, decl.ty->String());
    }
    ExposeGenericUpperBounds(ctx, *generic);
    AssumptionSanityCheck(*generic);
}

void TypeChecker::TypeCheckerImpl::AddSuperClassObjectForClassDecl(ASTContext& ctx)
{
    std::vector<Symbol*> syms = GetSymsByASTKind(ctx, ASTKind::CLASS_DECL, Sort::posAsc);
    for (auto& sym : syms) {
        CJC_ASSERT(sym && sym->node && sym->node->astKind == ASTKind::CLASS_DECL);
        auto cd = StaticAs<ASTKind::CLASS_DECL>(sym->node);
        if (HasSuperClass(*cd)) {
            continue;
        }
        if (cd->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
            if (!AddJObjectSuperClassJavaInterop(ctx, *cd)) {
                AddObjectSuperClass(ctx, *cd);
            }
        } else {
            AddObjectSuperClass(ctx, *cd);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CollectAndCheckAssumption(ASTContext& ctx)
{
    std::vector<Symbol*> syms = GetGenericCandidates(ctx);
    for (auto& sym : syms) {
        if (auto decl = AST::As<ASTKind::DECL>(sym->node); decl) {
            CollectAssumption(ctx, *decl);
        }
    }
    IgnoreAssumptionForTypeAliasDecls(ctx);
    AddAssumptionForExtendDecls(ctx);
    for (auto& sym : syms) {
        if (auto decl = AST::As<ASTKind::DECL>(sym->node); decl) {
            CheckAssumption(ctx, *decl);
        }
    }
}

void TypeChecker::TypeCheckerImpl::TypeAliasCircleCheck(const ASTContext& ctx)
{
    if (ctx.curPackage->TestAttr(Attribute::IMPORTED)) {
        return; // Alias dependency check can be ignored for imported package.
    }
    std::vector<Symbol*> syms = GetSymsByASTKind(ctx, ASTKind::TYPE_ALIAS_DECL, Sort::posAsc);
    for (auto& sym : syms) {
        if (auto tad = AST::As<ASTKind::TYPE_ALIAS_DECL>(sym->node); tad) {
            std::deque<Ptr<Decl>> path;
            CheckTypeAliasCycleForOneDecl(*tad, path);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckTypeAliasCycleForOneType(Type& type, std::deque<Ptr<Decl>>& path)
{
    switch (type.astKind) {
        case ASTKind::REF_TYPE: {
            auto rt = StaticAs<ASTKind::REF_TYPE>(&type);
            if (rt->ref.target && rt->ref.target->astKind == ASTKind::TYPE_ALIAS_DECL) {
                CheckTypeAliasCycleForOneDecl(*StaticAs<ASTKind::TYPE_ALIAS_DECL>(rt->ref.target), path);
            }
            break;
        }
        case ASTKind::QUALIFIED_TYPE: {
            auto qt = StaticAs<ASTKind::QUALIFIED_TYPE>(&type);
            if (qt->target && qt->target->astKind == ASTKind::TYPE_ALIAS_DECL) {
                CheckTypeAliasCycleForOneDecl(*StaticAs<ASTKind::TYPE_ALIAS_DECL>(qt->target), path);
            }
            break;
        }
        default:
            break;
    }
}

void TypeChecker::TypeCheckerImpl::CheckTypeAliasCycleForTypeArgsRecursively(
    std::vector<Ptr<Type>>& typeArgs, std::deque<Ptr<Decl>>& path)
{
    for (auto type : typeArgs) {
        if (type == nullptr) {
            continue;
        }
        CheckTypeAliasCycleForOneType(*type, path);
    }
}

void TypeChecker::TypeCheckerImpl::CheckTypeAliasCycleForOneDecl(TypeAliasDecl& tad, std::deque<Ptr<Decl>>& path)
{
    if (tad.checkFlag == InheritanceVisitStatus::VISITING) {
        // Found a cycle; mark every decl in this cycle.
        auto diagInfo = ConstructDiagInfoForCycle(tad, path);
        diag.Diagnose(tad, DiagKind::sema_typealias_cycle, diagInfo.c_str());
        return;
    }
    tad.checkFlag = InheritanceVisitStatus::VISITING;
    path.push_back(&tad);
    std::vector<Ptr<Type>> typeArgs;
    GetTypeArgsOfType(tad.type.get(), typeArgs);
    CheckTypeAliasCycleForTypeArgsRecursively(typeArgs, path);
    path.pop_back();
    tad.checkFlag = InheritanceVisitStatus::VISITED;
}

void TypeChecker::TypeCheckerImpl::StructDeclCircleOrDupCheckForOneSymbol(ASTContext& ctx, const Symbol& sym)
{
    CJC_NULLPTR_CHECK(sym.node);
    switch (sym.node->astKind) {
        case ASTKind::CLASS_DECL: {
            auto cd = StaticAs<ASTKind::CLASS_DECL>(sym.node);
            // Do duplicate interfaces implementation check.
            CheckDupInterfaceInStructDecl(*cd);
            // Check if there is inheritance cycle.
            std::deque<Ptr<Decl>> path{};
            CheckInheritanceCycleDFS(ctx, *cd, path);
            break;
        }
        case ASTKind::INTERFACE_DECL: {
            auto id = StaticAs<ASTKind::INTERFACE_DECL>(sym.node);
            // Do duplicate interfaces implementation check.
            CheckDupInterfaceInStructDecl(*id);
            // Check if there is inheritance cycle.
            std::deque<Ptr<Decl>> path{};
            CheckInheritanceCycleDFS(ctx, *id, path);
            break;
        }
        case ASTKind::STRUCT_DECL:
        case ASTKind::ENUM_DECL: {
            auto d = RawStaticCast<InheritableDecl*>(sym.node);
            CheckDupInterfaceInStructDecl(*d);
            MarkInvalidInheritanceForNonClassLike(*d);
            break;
        }
        default:
            break;
    }
}

void TypeChecker::TypeCheckerImpl::StructDeclCircleOrDupCheck(ASTContext& ctx)
{
    std::vector<Symbol*> syms = GetAllStructDecls(ctx);
    for (auto& sym : syms) {
        StructDeclCircleOrDupCheckForOneSymbol(ctx, *sym);
    }
}

void TypeChecker::TypeCheckerImpl::CheckDupInterfaceInStructDecl(InheritableDecl& decl)
{
    if (!Ty::IsTyCorrect(decl.ty)) {
        return;
    }
    // Do not check for decl's related extend decls in precheck step.
    CheckInstDupSuperInterfaces(decl, decl, {}, false);
}

template <typename T>
void TypeChecker::TypeCheckerImpl::CheckInheritanceCycleHelper(
    ASTContext& ctx, const Decl& decl, const Type& te, std::deque<Ptr<Decl>>& path, Ptr<ExtendDecl> extendDecl)
{
    if (te.astKind == ASTKind::REF_TYPE || te.astKind == ASTKind::QUALIFIED_TYPE) {
        auto target = te.GetTarget();
        // If type's target is typeAliasDecl, recursively finding the real used type.
        if (auto tad = DynamicCast<TypeAliasDecl*>(target); tad && tad->type) {
            CheckInheritanceCycleHelper<T>(ctx, decl, *tad->type, path, extendDecl);
        } else if (target && target->checkFlag != InheritanceVisitStatus::VISITED) {
            CheckInheritanceCycleDFS(ctx, *target, path, extendDecl);
        }
    } else {
        diag.Diagnose(decl, DiagKind::sema_inheritance_non_ref_type, decl.identifier.Val());
    }
}

void TypeChecker::TypeCheckerImpl::CheckInheritanceCycleWithExtend(
    ASTContext& ctx, const InheritableDecl& decl, std::deque<Ptr<Decl>>& path)
{
    std::set<Ptr<ExtendDecl>> extends = typeManager.GetDeclExtends(decl);
    for (auto& extend : extends) {
        for (auto& it : extend->inheritedTypes) {
            it->ty = GetTyFromASTType(ctx, it.get());
            auto target = Ty::GetDeclPtrOfTy(it->ty);
            if (auto id = DynamicCast<InterfaceDecl*>(target); id && id->checkFlag != InheritanceVisitStatus::VISITED) {
                CheckInheritanceCycleDFS(ctx, *id, path, extend);
            } else {
                CheckInheritanceCycleHelper<Ptr<InterfaceDecl>>(ctx, decl, *it, path, extend);
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckInheritanceCycleDFS(
    ASTContext& ctx, Decl& decl, std::deque<Ptr<Decl>>& path, Ptr<ExtendDecl> extendDecl)
{
    if (decl.checkFlag == InheritanceVisitStatus::VISITING) {
        CheckInheritanceCycleDFSHandleVisiting(decl, path, extendDecl);
        return;
    }

    decl.checkFlag = InheritanceVisitStatus::VISITING;
    path.push_back(&decl);

    if (auto cld = DynamicCast<ClassLikeDecl*>(&decl); cld) {
        for (auto& it : cld->inheritedTypes) {
            auto target = Ty::GetDeclPtrOfTy(it->ty);
            if (target && target->IsNominalDecl() && target->checkFlag != InheritanceVisitStatus::VISITED) {
                CheckInheritanceCycleDFS(ctx, *target, path, extendDecl);
            } else {
                CheckInheritanceCycleHelper<Ptr<ClassLikeDecl>>(ctx, decl, *it, path, extendDecl);
            }
        }
        CheckInheritanceCycleWithExtend(ctx, *cld, path);
    }

    path.pop_back();
    decl.checkFlag = InheritanceVisitStatus::VISITED;
}

void TypeChecker::TypeCheckerImpl::CheckInheritanceCycleDFSHandleVisiting(
    const Decl& decl, const std::deque<Ptr<Decl>>& path, Ptr<const ExtendDecl> extendDecl)
{
    // Found a cycle; mark every decl in this cycle.
    auto str = ConstructDiagInfoForCycle(decl, path);
    diag.Diagnose((extendDecl == nullptr) ? decl : *extendDecl, DiagKind::sema_inheritance_cycle, str);
}

void TypeChecker::TypeCheckerImpl::IgnoreAssumptionForTypeAliasDecls(const ASTContext& ctx) const
{
    std::vector<Symbol*> syms = GetSymsByASTKind(ctx, ASTKind::TYPE_ALIAS_DECL, Sort::posAsc);
    for (auto& sym : syms) {
        if (auto tad = AST::As<ASTKind::TYPE_ALIAS_DECL>(sym->node); tad && tad->type && tad->generic) {
            for (auto& tp : tad->generic->typeParameters) {
                if (auto genTy = DynamicCast<GenericsTy*>(tp->ty)) {
                    genTy->isAliasParam = true;
                }
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::AddAssumptionForExtendDecls(ASTContext& ctx)
{
    std::vector<Symbol*> syms = GetSymsByASTKind(ctx, ASTKind::EXTEND_DECL, Sort::posAsc);
    for (auto& sym : syms) {
        if (auto ed = AST::As<ASTKind::EXTEND_DECL>(sym->node); ed) {
            if (ed->extendedType && ed->generic) {
                AddAssumptionForType(ctx, *ed->extendedType, *ed->generic);
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::AddUpperBoundOnTypeParameters(
    ASTContext& ctx, const Generic& generic, const Decl& typeTarget, MultiTypeSubst& revTypeMapping,
    TyVarUB& allAssumptionMap, const TypeSubst& typeArgAppliedMap)
{
    for (auto& typeParameter : generic.typeParameters) {
        auto genericTy = DynamicCast<GenericsTy*>(typeParameter->ty);
        if (genericTy == nullptr) {
            continue;
        }
        auto aliasedTypeGeneric = typeTarget.GetGeneric();
        if (aliasedTypeGeneric == nullptr) {
            continue;
        }
        auto assumedUpperBounds = aliasedTypeGeneric->assumptionCollection;
        GetAllAssumptions(assumedUpperBounds, allAssumptionMap);
        auto constraintTys = revTypeMapping[StaticCast<GenericsTy*>(typeParameter->ty)];
        UpperBounds constraints;
        for (auto it : constraintTys) {
            auto uppers = allAssumptionMap[StaticCast<GenericsTy*>(it)];
            constraints.merge(uppers);
        }
        std::set<Ptr<Ty>> upperBoundsForParam;
        std::for_each(constraints.begin(), constraints.end(),
            [this, &ctx, &constraintTys, &genericTy, &typeArgAppliedMap, &upperBoundsForParam](auto ty) {
                Ptr<Ty> instTy = typeManager.GetInstantiatedTy(ty, typeArgAppliedMap);
                if (Ty::IsTyCorrect(instTy)) {
                    upperBoundsForParam.insert(instTy);
                    for (auto it : constraintTys) {
                        auto srcBlames = ctx.gcBlames[it][ty];
                        ctx.gcBlames[genericTy][instTy].merge(srcBlames);
                    }
                }
            });
        genericTy->upperBounds.insert(upperBoundsForParam.begin(), upperBoundsForParam.end());
    }
}

void TypeChecker::TypeCheckerImpl::AddAssumptionForType(ASTContext& ctx, const Type& type, Generic& generic)
{
    if (type.astKind != ASTKind::REF_TYPE && type.astKind != ASTKind::QUALIFIED_TYPE) {
        return;
    }
    auto aliasedTypeTarget = type.GetTarget();
    if (!aliasedTypeTarget || !type.ty || type.ty->typeArgs.empty()) {
        return;
    }
    MultiTypeSubst revTypeMapping;
    TyVarUB allAssumptionMap;
    GetRevTypeMapping(aliasedTypeTarget->ty->typeArgs, type.ty->typeArgs, revTypeMapping);
    auto typeArgAppliedMap = GetGenericTysToInstTysMapping(*aliasedTypeTarget->ty, *type.ty);
    AddUpperBoundOnTypeParameters(
        ctx, generic, *aliasedTypeTarget, revTypeMapping, allAssumptionMap, typeArgAppliedMap);
    CreateGenericConstraints(generic);
}

void TypeChecker::TypeCheckerImpl::PreCheckMacroRedefinition(const std::vector<Ptr<FuncDecl>>& funcs) const
{
    std::vector<Ptr<FuncDecl>> macroVec;
    std::for_each(funcs.begin(), funcs.end(), [&macroVec](auto func) {
        if (func->TestAttr(Attribute::MACRO_FUNC) && !func->TestAttr(Attribute::COMPILER_ADD)) {
            macroVec.emplace_back(func);
        }
    });
    if (macroVec.size() <= 1) {
        return;
    }
    bool invalid = std::any_of(
        macroVec.begin(), macroVec.end(), [](auto fd) { return !fd->funcBody || fd->funcBody->paramLists.empty(); });
    if (invalid) {
        return;
    }
    size_t cnt0 = macroVec[0]->funcBody->paramLists.front().get()->params.size();
    size_t cnt1 = macroVec[1]->funcBody->paramLists.front().get()->params.size();
    // Allow one attribute macro and one non-attribute macro.
    // 2 means a threshold
    if ((macroVec.size() == 2 && cnt0 == cnt1) || macroVec.size() > 2) {
        for (auto md : macroVec) {
            (void)ci->diag.Diagnose(*md, DiagKind::sema_expand_macro_redefinition, md->identifier.Val().c_str());
        }
    }
}

void TypeChecker::TypeCheckerImpl::PreCheckFuncRedefinitionForEnum(const std::vector<Ptr<FuncDecl>>& funcs)
{
    CJC_ASSERT(!funcs.empty()); // Caller guarantees.
    if (funcs[0]->outerDecl == nullptr || funcs[0]->outerDecl->astKind != ASTKind::ENUM_DECL) {
        return;
    }
    auto ed = StaticAs<ASTKind::ENUM_DECL>(funcs[0]->outerDecl);
    std::unordered_set<size_t> paramNum;
    std::unordered_map<std::string, Ptr<const Decl>> ctorNames;
    std::for_each(ed->constructors.begin(), ed->constructors.end(),
        [&ctorNames](auto& decl) { ctorNames.emplace(std::make_pair(decl->identifier, decl.get())); });
    for (auto func : funcs) {
        if (!func->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            // Member functions in enum cannot have same name with enum constructor.
            auto iter = ctorNames.find(func->identifier);
            if (iter != ctorNames.cend()) {
                DiagRedefinitionWithFoundNode(diag, *func, *iter->second);
            }
            continue;
        }
        // Function redefinition check is moved to precheck when the ty of function is not set, because the
        // return type need to be inferred in type check stage.
        auto funcTy = DynamicCast<FuncTy*>(func->ty);
        auto paramTys = funcTy ? funcTy->paramTys : GetParamTys(*func);
        if (paramNum.find(paramTys.size()) != paramNum.end()) {
            diag.Diagnose(*func, DiagKind::sema_duplicated_item_in_enum,
                func->identifier.Val().c_str(), ed->identifier.Val().c_str());
        } else {
            paramNum.insert(paramTys.size());
        }
    }
}

void TypeChecker::TypeCheckerImpl::PreCheckFuncRedefinitionForFFI(const std::vector<Ptr<FuncDecl>>& funcs)
{
    std::vector<Ptr<FuncDecl>> nativeDuplicates;
    std::copy_if(funcs.begin(), funcs.end(), std::back_inserter(nativeDuplicates),
        [](Ptr<const FuncDecl> fd) { return fd->TestAnyAttr(Attribute::FOREIGN, Attribute::C); });
    if (nativeDuplicates.size() <= 1) {
        return;
    }
    for (size_t i = 1; i < nativeDuplicates.size(); ++i) {
        auto fd = nativeDuplicates[i];
        if (fd->ShouldDiagnose() && !fd->TestAttr(Attribute::CONSTRUCTOR)) {
            DiagRedefinitionWithFoundNode(diag, *fd, *nativeDuplicates[0]);
        }
    }
}

void TypeChecker::TypeCheckerImpl::PreCheckFuncStaticConflict(const std::vector<Ptr<FuncDecl>>& funcs)
{
    std::vector<Ptr<FuncDecl>> staticFuncs;
    std::copy_if(funcs.begin(), funcs.end(), std::back_inserter(staticFuncs), [](Ptr<const FuncDecl> fd) {
        return fd->TestAttr(Attribute::STATIC) && !fd->TestAttr(Attribute::CONSTRUCTOR);
    });
    Ptr<FuncDecl> firstNonStatic = nullptr;
    for (const auto& func : funcs) {
        if (!func->TestAttr(Attribute::STATIC) && !func->TestAttr(Attribute::CONSTRUCTOR)) {
            firstNonStatic = func;
            break;
        }
    }
    if (!staticFuncs.empty() && firstNonStatic) {
        for (const auto& func : staticFuncs) {
            DiagStaticAndNonStaticOverload(diag, *func, *firstNonStatic);
        }
    }
}

bool TypeChecker::TypeCheckerImpl::PreCheckFuncRedefinitionWithSameSignature(
    std::vector<Ptr<FuncDecl>> funcs, bool needReportErr)
{
    bool ret = false;
    while (funcs.size() > 1) {
        auto it = funcs.begin();
        auto it1 = std::partition(it, funcs.end(), [&it, this](Ptr<FuncDecl> fd) {
            // Function redefinition check is moved to PreCheck when the ty of function is not set, because the
            // return type need to be inferred in type check stage.
            auto funcTy1 = DynamicCast<FuncTy*>((*it)->ty);
            auto funcTy2 = DynamicCast<FuncTy*>(fd->ty);
            auto paramTys1 = funcTy1 ? funcTy1->paramTys : GetParamTys(**it);
            auto paramTys2 = funcTy2 ? funcTy2->paramTys : GetParamTys(*fd);
            // Get substituteMap S = [X11 -> X21, ..., X1n -> X2m]. from '*it' to 'fd' if exists.
            TypeSubst substituteMap = GetSubstituteMap(typeManager, **it, *fd);
            return typeManager.IsFuncParameterTypesIdentical(paramTys1, paramTys2, substituteMap) &&
                fd->TestAttr(Attribute::STATIC) == (*it)->TestAttr(Attribute::STATIC) &&
                fd->TestAttr(Attribute::CONSTRUCTOR) == (*it)->TestAttr(Attribute::CONSTRUCTOR) &&
                fd->TestAttr(Attribute::MAIN_ENTRY) == (*it)->TestAttr(Attribute::MAIN_ENTRY);
        });
        std::vector<Ptr<FuncDecl>> sameSigFuncs;
        std::copy(funcs.begin(), it1, std::back_inserter(sameSigFuncs));
        if (sameSigFuncs.size() > 1) {
            if (needReportErr) {
                DiagOverloadConflict(diag, sameSigFuncs);
            }
            ret = true;
        }
        // All processed.
        if (it1 == funcs.end()) {
            break;
        }
        // No ty case, force push.
        if (sameSigFuncs.empty()) {
            sameSigFuncs.emplace_back(*it1++);
        }
        std::copy(it1, funcs.end(), funcs.begin());
        funcs.resize(funcs.size() - sameSigFuncs.size());
    }
    return ret;
}

void TypeChecker::TypeCheckerImpl::PreCheckFuncRedefinition(const ASTContext& ctx)
{
    // Redefinition check can be ignored for imported package or package generate by cjogen.
    if (ctx.curPackage->TestAnyAttr(Attribute::IMPORTED, Attribute::TOOL_ADD)) {
        return; // Redefinition check can be ignored for imported package.
    }
    auto syms = GetSymsByASTKind(ctx, ASTKind::FUNC_DECL);
    // Redefinition candidates.
    std::map<Names, std::vector<Ptr<FuncDecl>>> candidates;
    for (auto sym : syms) {
        if (!sym->node || sym->node->TestAttr(Attribute::MACRO_INVOKE_FUNC)) {
            continue; // Do not collect invalid/macro expanded node.
        }
        std::string scopeName = ScopeManagerApi::GetScopeNameWithoutTail(sym->scopeName);
        auto names = std::make_pair(sym->name, scopeName);
        auto fd = StaticAs<ASTKind::FUNC_DECL>(sym->node);
        if (fd->propDecl) {
            continue; // Do not check for property's getter/setter.
        }
        if (candidates.find(names) != candidates.end()) {
            candidates[names].emplace_back(fd);
        } else {
            candidates[names] = {fd};
        }
    }
    MPTypeCheckerImpl::FilterOutCommonCandidatesIfPlatformExist(candidates);
    auto candidatesForImport = candidates;
    for (const auto& [names, funcs] : std::as_const(candidates)) {
        PreCheckMacroRedefinition(funcs);
        // Check Functions in the Enum.
        PreCheckFuncRedefinitionForEnum(funcs);
        // Check FFI function duplications.
        PreCheckFuncRedefinitionForFFI(funcs);
        // Check static and non-static duplications.
        PreCheckFuncStaticConflict(funcs);
        // Check same signature for both non-generic and generic functions.
        PreCheckFuncRedefinitionWithSameSignature(funcs);
    }
}

namespace {
Ptr<ReturnExpr> GetDanglingReturn(const FuncParam& fp)
{
    Ptr<ReturnExpr> ret = nullptr;
    Walker(fp.assignment.get(), [&ret](Ptr<Node> node) {
        if (node->astKind == ASTKind::FUNC_BODY) {
            // We donot check recursively here, since the caller guarantees all the parameters are checked.
            return VisitAction::SKIP_CHILDREN;
        } else if (node->astKind == ASTKind::RETURN_EXPR) {
            ret = RawStaticCast<ReturnExpr*>(node);
            return VisitAction::STOP_NOW;
        } else {
            return VisitAction::WALK_CHILDREN;
        }
    }).Walk();
    return ret;
}
}; // namespace

void TypeChecker::TypeCheckerImpl::CheckReturnAndJump(const ASTContext& ctx)
{
    auto syms = GetSymsByASTKind(ctx, ASTKind::RETURN_EXPR);
    for (auto sym : syms) {
        CJC_ASSERT(sym && sym->node);
        auto re = StaticCast<ReturnExpr*>(sym->node);
        auto refFuncBody = GetCurFuncBody(ctx, sym->scopeName);
        if (refFuncBody) {
            if (refFuncBody->funcDecl && IsStaticInitializer(*refFuncBody->funcDecl)) {
                re->ty = TypeManager::GetInvalidTy();
                (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_return_in_static_init, *sym->node);
            } else {
                re->refFuncBody = refFuncBody;
            }
        } else {
            re->ty = TypeManager::GetInvalidTy();
            diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_return, *sym->node);
        }
    }
    auto paramSyms = GetSymsByASTKind(ctx, ASTKind::FUNC_PARAM);
    for (auto sym : paramSyms) {
        CJC_ASSERT(sym && sym->node);
        FuncParam& param = *StaticCast<FuncParam*>(sym->node);
        Ptr<ReturnExpr> re = GetDanglingReturn(param);
        if (re != nullptr) {
            re->refFuncBody = nullptr;
            re->ty = TypeManager::GetInvalidTy();
            diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_return, *re);
        }
    }

    syms = GetSymsByASTKind(ctx, ASTKind::JUMP_EXPR);
    for (auto sym : syms) {
        CJC_ASSERT(sym && sym->node);
        if (!ScopeManager::GetRefLoopSymbol(ctx, *sym->node)) {
            diag.Diagnose(*sym->node, DiagKind::sema_invalid_loop_control);
            sym->node->ty = TypeManager::GetInvalidTy();
        }
    }
}

void TypeChecker::TypeCheckerImpl::ReplaceThisTypeInFunc(const AST::FuncDecl& funcDecl)
{
    CJC_ASSERT(funcDecl.funcBody);
    // Parser guarantees 'This' only exist on the return type of function decl inside classBody.
    auto cd = DynamicCast<ClassDecl*>(funcDecl.outerDecl);
    if (cd == nullptr || !Ty::IsTyCorrect(cd->ty)) {
        return;
    }
    // Replace ThisType to RefType.
    if (auto thisType = DynamicCast<ThisType*>(funcDecl.funcBody->retType.get())) {
        auto rt = MakeOwned<RefType>();
        rt->ref.target = cd;
        rt->ref.identifier = "This";
        rt->ref.identifier.SetPos(thisType->begin, thisType->begin + std::string_view{"This"}.size());
        CopyBasicInfo(thisType, rt.get());
        rt->ty = typeManager.GetClassThisTy(*cd, cd->ty->typeArgs);
        funcDecl.funcBody->retType = std::move(rt);
    }
}

void TypeChecker::TypeCheckerImpl::PreSetDeclType(const ASTContext& ctx)
{
    // 1. Set user defined enum constructor's type.
    auto syms = GetSymsByASTKind(ctx, ASTKind::ENUM_DECL);
    for (auto sym : syms) {
        CJC_ASSERT(sym && sym->node);
        auto ed = StaticCast<EnumDecl*>(sym->node);
        for (auto& constructor : ed->constructors) {
            SetEnumEleTy(*constructor);
        }
    }
    // 2. Set user defined varDecl and propDecl's type.
    syms = GetSymsByASTKind(ctx, ASTKind::VAR_DECL);
    auto propSyms = GetSymsByASTKind(ctx, ASTKind::PROP_DECL);
    syms.insert(syms.end(), propSyms.begin(), propSyms.end());
    for (auto sym : syms) {
        CJC_ASSERT(sym && sym->node);
        auto vd = StaticCast<VarDecl*>(sym->node);
        if (vd->type && Ty::IsTyCorrect(vd->type->ty)) {
            vd->ty = vd->type->ty;
        }
    }
    // 3. Set user defined function parameter's types (must exist), and function return type if written.
    syms = GetSymsByASTKind(ctx, ASTKind::FUNC_DECL);
    for (auto sym : syms) {
        CJC_ASSERT(sym && sym->node);
        auto fd = StaticCast<FuncDecl*>(sym->node);
        CJC_NULLPTR_CHECK(fd->funcBody);
        ReplaceThisTypeInFunc(*fd); // Always need to update node of ThisType.
        if (Ty::IsTyCorrect(fd->ty)) {
            continue; // Do not replace valid ty.
        }
        auto paramTys = GetFuncBodyParamTys(*fd->funcBody);
        Ptr<Ty> retTy = TypeManager::GetQuestTy();
        if (fd->TestAttr(Attribute::CONSTRUCTOR)) {
            // Static init has return type of unit. Instance init has return type of current typeDecl.
            retTy = fd->TestAttr(Attribute::STATIC)
                ? TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)
                : (fd->outerDecl && fd->outerDecl->IsNominalDecl() ? fd->outerDecl->ty : TypeManager::GetInvalidTy());
            fd->ty = typeManager.GetFunctionTy(paramTys, retTy);
            continue;
        }
        if (fd->funcBody->retType) {
            retTy = fd->funcBody->retType->ty;
        } else if (fd->funcBody->body && fd->funcBody->body->body.empty()) {
            retTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
        }
        bool isCFunc =
            fd->funcBody->TestAttr(Attribute::C) || (fd->TestAttr(Attribute::FOREIGN) && IsUnsafeBackend(backendType));
        bool hasVariableLenArg = fd->hasVariableLenArg ||
            (!fd->funcBody->paramLists.empty() && fd->funcBody->paramLists[0]->hasVariableLenArg);
        fd->ty = typeManager.GetFunctionTy(paramTys, retTy, {isCFunc, false, hasVariableLenArg});
        if (fd->TestAttr(Attribute::IS_CHECK_VISITED)) {
            fd->funcBody->ty = fd->ty;
        }
    }
}

void TypeChecker::TypeCheckerImpl::PreCheckUsage(ASTContext& ctx, const Package& pkg)
{
    // NOTE: processing order is important.
    // Build enum constructors map.
    BuildImportedEnumConstructorMap(ctx);
    BuildEnumConstructorMap(ctx);
    // Resolve all type names.
    ResolveNames(ctx);
    // Build and check extend decls for each type.
    BuildExtendMap(ctx);

    mpImpl->UpdatePlatformMemberGenericTy(
        ctx, [this](ASTContext& ctx, ASTKind kind) { return this->GetSymsByASTKind(ctx, kind); });
    // Check duplicate interface inheritance for nominal decls. NOTE: Should resolve typeAlias first.
    StructDeclCircleOrDupCheck(ctx);
    // Check and set member's basic attributes.
    CheckAllDeclAttributes(ctx);
    // Add Object as super class for class declaration.
    AddSuperClassObjectForClassDecl(ctx);
    // Collector assumption collection of generic declaration.
    CollectAndCheckAssumption(ctx);
    // Check extend generic param & reset inheritance checking flag.
    CheckExtendRules(ctx);
    // Check Function Redefinition.
    PreCheckFuncRedefinition(ctx);
    // Check return and jump expressions.
    CheckReturnAndJump(ctx);
    // Preset user defined types of decls, must before sema check to reduce circular synthesis.
    PreSetDeclType(ctx);
    // Check invalid inherit of class, struct, enum, interface.
    PreCheckInvalidInherit(ctx, pkg);

    // Check for CJMP decls
    mpImpl->PreCheck4CJMP(pkg);
}

void TypeChecker::TypeCheckerImpl::PreCheckInvalidInherit(const ASTContext& ctx, const AST::Package& pkg)
{
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }

    auto inheritableDecls = GetAllStructDecls(ctx);
    for (auto sym : inheritableDecls) {
        CJC_NULLPTR_CHECK(sym);
        CJC_NULLPTR_CHECK(sym->node);
        auto id = DynamicCast<InheritableDecl*>(sym->node);
        if (id == nullptr) {
            continue;
        }
        // For ExtendDecl, we check it in `CheckExtendInterfaces`, so we dont't check it here.
        if (id->astKind == ASTKind::EXTEND_DECL) {
            continue;
        }
        DiagKind kind = id->astKind == ASTKind::INTERFACE_DECL ? DiagKind::sema_interface_is_not_inheritable
                                                               : DiagKind::sema_interface_is_not_implementable;
        for (auto& it : id->inheritedTypes) {
            if (it->ty->IsCType()) {
                diag.Diagnose(*it, kind, CTYPE_NAME);
            }
        }
    }
}

namespace {
Ptr<Decl> GetTypeDecl(TypeManager& tyMgr, Ptr<Decl> d)
{
    if (auto ed = DynamicCast<ExtendDecl*>(d)) {
        auto target = ed->extendedType->GetTarget();
        if (!target) {
            return tyMgr.GetDummyBuiltInDecl(ed->extendedType->ty);
        }
        return GetRealTarget(target);
    } else {
        return d;
    }
}

Ptr<Decl> GetTypeDeclOfMember(TypeManager& tyMgr, const Decl& d)
{
    return GetTypeDecl(tyMgr, d.outerDecl);
}

MemSig MemDecl2Sig(Decl& d)
{
    if (auto fd = DynamicCast<FuncDecl*>(&d)) {
        return MemSig{fd->identifier,
            false,
            fd->funcBody->paramLists[0]->params.size(),
            fd->generic ? fd->generic->typeParameters.size() : 0};
    } else {
        return MemSig{d.identifier, true};
    }
}
} // namespace

void TypeChecker::TypeCheckerImpl::CollectDeclsWithMember(Ptr<Package> pkg, ASTContext& ctx)
{
    const std::set<ASTKind> TYPE_DECLS{ASTKind::CLASS_DECL, ASTKind::STRUCT_DECL, ASTKind::ENUM_DECL,
        ASTKind::INTERFACE_DECL, ASTKind::EXTEND_DECL, ASTKind::BUILTIN_DECL};
    const std::set<ASTKind> MEMBER_DECLS{ASTKind::FUNC_DECL, ASTKind::PROP_DECL, ASTKind::VAR_DECL};
    std::map<Ptr<Decl>, std::unordered_set<MemSig, MemSigHash>> decl2Mems;
    std::set<Ptr<Decl>> allTops; // includes extendDecl needed for finding super types
    auto mapDecl = [this, &ctx, &decl2Mems, &MEMBER_DECLS](Ptr<Decl> d) {
        if (MEMBER_DECLS.count(d->astKind) == 0) {
            return;
        }
        auto sig = MemDecl2Sig(*d);
        auto top = GetTypeDeclOfMember(typeManager, *d);
        ctx.mem2Decls[sig].insert(top);
        decl2Mems[top].insert(sig);
        if (sig.genArity > 0) {
            sig.genArity = 0;
            ctx.mem2Decls[sig].insert(top);
            decl2Mems[top].insert(sig);
        }
    };
    auto collectDecl = [&allTops, &TYPE_DECLS, &mapDecl](Ptr<Decl> top) {
        if (TYPE_DECLS.count(top->astKind) > 0) {
            allTops.emplace(top);
        }
        for (auto& d : top->GetMemberDecls()) {
            mapDecl(d);
        }
    };
    // collect this package
    std::vector<Symbol*> syms = GetAllDecls(ctx);
    for (auto& sym : syms) {
        collectDecl(StaticCast<Decl*>(sym->node));
    }
    // collect imported
    for (auto& file : pkg->files) {
        auto imported = importManager.GetImportedDecls(*file);
        for (auto& [_, tops] : imported) {
            for (auto& top : tops) {
                collectDecl(top);
            }
        }
    }
    // collect builtin extends, which somehow is not included in imported
    for (auto& [_, tops] : typeManager.builtinTyToExtendMap) {
        for (auto& top : tops) {
            collectDecl(top);
        }
    }
    // Members are not propagated to inherited types here, since it may take
    // too much time & memory; only remember accesible subtypes here.
    // Inherited members are computed on-demand.
    for (auto& top : allTops) {
        auto realTop = GetTypeDecl(typeManager, top);
        if (auto inheritable = DynamicCast<InheritableDecl*>(top)) {
            for (auto sup : inheritable->GetAllSuperDecls()) {
                ctx.subtypeDeclsMap[sup].insert(realTop);
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::PreCheck(const std::vector<Ptr<ASTContext>>& contexts)
{
    // NOTE: PreCheck should not contains any 'Synthesize' call.
    for (auto& ctx : contexts) {
        // Stage 1. Check redefinition except functions and build all pkgs declMap to accelerate look up.
        CheckRedefinition(*ctx);
    }

    for (auto& ctx : contexts) {
        // Stage 2: Set decl sema type without checking inside the decl body.
        ResolveDecls(*ctx);
    }
    // Resolve type alias separately after all user-defined type resolved.
    ResolveTypeAlias(contexts);

    // Clear extend related data before iteration of all packages.
    typeManager.ClearMapCache();
    // First part does not need sema check.
    // Collect all imported non-source extends before checking each package.
    BuildImportedExtendMap();
    for (auto& ctx : contexts) {
        // Phase: check annotation for FFI.
        PreCheckAnnoForFFI(*ctx->curPackage);
        PreCheckUsage(*ctx, *ctx->curPackage);
    }
    PreCheckAllExtendInterface();
    // CHIR's closure conversion cannot be used without 'Object' class.
    // If '-no-prelude' is given, should check dependencies of class.
    if (ci->invocation.globalOptions.chirCC && !ci->invocation.globalOptions.implicitPrelude) {
        CheckCHIRClassDependencies();
    }
    // Hotfix. Update 'anyTy' in typeManger to real interface 'Any' type.
    UpdateAnyTy();
    UpdateCTypeTy();
}
