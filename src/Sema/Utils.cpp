// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements utility functions for TypeChecker.
 */

#include <algorithm>

#include "TypeCheckUtil.h"

#include "Diags.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"

namespace Cangjie {
using namespace AST;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
std::vector<Ptr<Decl>> GetCycleOnPath(Ptr<Decl> const root, std::deque<Ptr<Decl>>& path)
{
    std::vector<Ptr<Decl>> cycle;
    bool foundCycleStart = false;
    for (auto& i : std::as_const(path)) {
        if (i == root) {
            foundCycleStart = true;
        }
        if (foundCycleStart) {
            cycle.emplace_back(i);
        }
    }
    return cycle;
}

bool IsInitRef(const Node& node)
{
    const auto target = node.GetTarget();
    return target && IsInstanceConstructor(*target);
}

std::vector<Ptr<Node>> CollectInitRefsInVar(const std::vector<OwnedPtr<Decl>>& decls)
{
    std::vector<Ptr<Node>> initRefsInVar;
    for (auto& decl : decls) {
        CJC_NULLPTR_CHECK(decl);
        // Constructors don't depend on static variables.
        if (auto vd = DynamicCast<const VarDeclAbstract*>(decl.get()); vd && !vd->TestAttr(Attribute::STATIC)) {
            Walker(vd->initializer.get(), [&initRefsInVar](auto node) {
                if (IsInitRef(*node)) {
                    (void)initRefsInVar.emplace_back(node);
                }
                return VisitAction::WALK_CHILDREN;
            }).Walk();
        }
    }
    return initRefsInVar;
}

std::vector<Ptr<Node>> CollectInitRefsInVar(const Decl& typeDecl)
{
    if (typeDecl.astKind == ASTKind::STRUCT_DECL) {
        const auto& sd = StaticCast<const StructDecl&>(typeDecl);
        CJC_NULLPTR_CHECK(sd.body);
        return CollectInitRefsInVar(sd.body->decls);
    } else if (typeDecl.astKind == ASTKind::CLASS_DECL) {
        const auto& cd = StaticCast<const ClassDecl&>(typeDecl);
        CJC_NULLPTR_CHECK(cd.body);
        return CollectInitRefsInVar(cd.body->decls);
    } else {
        return {};
    }
}

struct RecursiveCtorCtx {
    std::unordered_set<Ptr<const Node>> finished;
    std::unordered_set<Ptr<const Node>> visited;
    std::unordered_set<Ptr<const Node>> ignored;
    // A map from struct/class declaration to its dependencies.
    std::unordered_map<Ptr<const Decl>, std::vector<Ptr<Node>>> typeDeclToInitRefsInVar;
    std::vector<Ptr<const Node>> path; // A stack to keep track of the cycle.
    bool foundCycle = false;

    const std::vector<Ptr<Node>>& GetDependencies(const Decl& init)
    {
        auto typeDecl = init.outerDecl;
        CJC_NULLPTR_CHECK(typeDecl);
        auto iter = typeDeclToInitRefsInVar.find(typeDecl);
        if (iter == typeDeclToInitRefsInVar.cend()) {
            iter = typeDeclToInitRefsInVar.emplace(typeDecl, CollectInitRefsInVar(*typeDecl)).first;
        }
        return iter->second;
    }
};

VisitAction VisitRecursiveCtorCtx(RecursiveCtorCtx& ctx, const Node& node)
{
    ctx.path.emplace_back(&node);
    if (Utils::In(&node, ctx.finished)) {
        return VisitAction::SKIP_CHILDREN;
    }
    if (Utils::In(&node, ctx.visited)) {
        ctx.foundCycle = true;
        return VisitAction::STOP_NOW;
    }
    (void)ctx.visited.emplace(&node);
    return VisitAction::WALK_CHILDREN;
}

// Use DFS to detect cycle for constructor calls.
// The dependency graph is embedded in the AST so we don't have to build the graph explicitly.
// There are two kinds of vertices in the dependency graph:
// 1. Constructor declarations, judged by `IsInstanceConstructor`.
// 2. Constructor references, judged by `IsInitRef`.
// We use these two predicates to skip irrelevant nodes in the AST.
bool CheckRecursiveCtorFoundCycle(RecursiveCtorCtx& ctx, Node& decl)
{
    auto postVisit = [&ctx](const auto node) {
        if (IsInstanceConstructor(*node) || IsInitRef(*node)) {
            ctx.path.pop_back();
            (void)ctx.finished.emplace(node);
        }
        return VisitAction::SKIP_CHILDREN;
    };
    auto preVisit = [&ctx](const auto node) {
        if (Utils::In<Ptr<const Node>>(node, ctx.ignored)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (auto d = DynamicCast<const Decl*>(node); d && d->annotationsArray) {
            // Annotations are not dependencies.
            (void)ctx.ignored.emplace(d->annotationsArray.get());
            for (auto& anno : d->annotations) {
                (void)ctx.ignored.emplace(anno.get());
            }
        }
        if (IsInstanceConstructor(*node)) {
            const Decl& init = StaticCast<const Decl&>(*node);
            // There must be syntax error if `decl.outerDecl` is null.
            if (init.outerDecl == nullptr) {
                return VisitAction::STOP_NOW;
            }
            auto action = VisitRecursiveCtorCtx(ctx, init);
            if (action != VisitAction::WALK_CHILDREN) {
                return action;
            }
            // Member variables are initialized before constructor call. So for each constructor declaration,
            // there is an arc from the declaration to the initializer in the dependency graph.
            for (Ptr<Node> ref : ctx.GetDependencies(init)) {
                if (CheckRecursiveCtorFoundCycle(ctx, *ref)) {
                    return VisitAction::STOP_NOW;
                }
            }
        } else if (IsInitRef(*node)) {
            auto action = VisitRecursiveCtorCtx(ctx, *node);
            if (action != VisitAction::WALK_CHILDREN) {
                return action;
            }
            // Reference expressions depend on the constructor declarations.
            // So there is an arc from the reference to the target.
            Ptr<Node> target = node->GetTarget();
            CJC_NULLPTR_CHECK(target);
            if (CheckRecursiveCtorFoundCycle(ctx, *target)) {
                return VisitAction::STOP_NOW;
            }
        }
        // There may be more arcs in the children.
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&decl, preVisit, postVisit).Walk();
    return ctx.foundCycle;
}

Range MakeRangeForRecursiveConstructorCall(const Node& node)
{
    if (node.astKind == ASTKind::FUNC_DECL) {
        return MakeRangeForDeclIdentifier(StaticCast<const FuncDecl&>(node));
    } else {
        return MakeRange(node.begin, node.end);
    }
}

void DiagRecursiveConstructorCall(DiagnosticEngine& diag, const std::vector<Ptr<const Node>>& path)
{
    CJC_ASSERT(!path.empty());
    // The head of the cycle is the same as the last one in path.
    const Node& head = *path.back();
    auto builder = diag.DiagnoseRefactor(
        DiagKindRefactor::sema_recursive_constructor_call, head, MakeRangeForRecursiveConstructorCall(head));
    auto iter = std::find(path.cbegin(), path.cend(), path.back());
    // Skip the head of the cycle, which is the same as the error message.
    ++iter;
    for (; iter != path.cend(); ++iter) {
        CJC_NULLPTR_CHECK(*iter);
        const Node& node = **iter;
        // We should skip the compiler added `super`.
        // But keep in mind that `super` in primary constructors also have the `COMPILER_ADD` attribute.
        // We use the position to distinguish these situations.
        if (node.astKind == ASTKind::REF_EXPR && node.TestAttr(Attribute::COMPILER_ADD) &&
            StaticCast<const RefExpr&>(node).isSuper) {
            // Only `init` depends on `super`.
            const FuncDecl& init = StaticCast<const FuncDecl&>(**(iter - 1));
            CJC_NULLPTR_CHECK(init.funcBody);
            if (init.funcBody->begin == node.begin) {
                continue;
            }
        }
        builder.AddNote(node, MakeRangeForRecursiveConstructorCall(node), "depends on");
    }
}
} // namespace

// desugar primary ctor before cjmp customDef merging
void TypeChecker::TypeCheckerImpl::CheckPrimaryCtorBeforeMerge(Node &root)
{
    // Create by cjogen is not need to add default construct.
    if (root.TestAttr(Attribute::TOOL_ADD)) {
        return;
    }
    Walker walkerPackage(&root, [this](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::FILE) {
            auto file = StaticAs<ASTKind::FILE>(node);
            for (auto &decl : file->decls) {
                if (decl->astKind == ASTKind::CLASS_DECL || decl->astKind == ASTKind::STRUCT_DECL) {
                    CheckPrimaryCtorForClassOrStruct(StaticCast<InheritableDecl>(*decl));
                }
            }
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walkerPackage.Walk();
}

void TypeChecker::TypeCheckerImpl::AddDefaultFunction(Node& root)
{
    // Create by cjogen is not need to add default construct.
    if (root.TestAttr(Attribute::TOOL_ADD)) {
        return;
    }
    Walker walkerPackage(&root, [this](Ptr<Node> node) -> VisitAction {
        if (node && node->astKind == ASTKind::FILE) {
            auto file = StaticAs<ASTKind::FILE>(node);
            CheckDefaultParamFuncsEntry(*file);
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walkerPackage.Walk();
}

namespace {
void SetCtorFuncPosition(Decl& decl, FuncDecl& initFunc)
{
    if (initFunc.TestAttr(Attribute::STATIC)) {
        // Do not set ctor position for static constructor here.
        return;
    }
    std::vector<OwnedPtr<Decl>>& decls = (decl.astKind == ASTKind::STRUCT_DECL)
        ? StaticAs<ASTKind::STRUCT_DECL>(&decl)->body->decls
        : StaticAs<ASTKind::CLASS_DECL>(&decl)->body->decls;
    for (auto it = decls.rbegin(); it != decls.rend(); ++it) {
        if ((*it)->astKind != ASTKind::VAR_DECL || (*it)->TestAttr(Attribute::STATIC)) {
            continue;
        }
        // Find last instance vardecl.
        initFunc.begin = (*it)->begin;
        break;
    }
    initFunc.begin.fileID = decl.begin.fileID;
}

bool HasOnlyStructTypeInCycle(std::vector<Ptr<Decl>> const& cycle)
{
    bool res = true;
    for (auto& i : cycle) {
        if (i->astKind != ASTKind::STRUCT_DECL) {
            res = false;
            break;
        }
    }
    return res;
}

bool HasRecompilingMemberVar(const std::vector<OwnedPtr<Decl>>& members, bool isStatic)
{
    std::vector<Ptr<Decl>> variables;
    for (auto& it : std::as_const(members)) {
        if (it->astKind == ASTKind::VAR_DECL && it->TestAttr(Attribute::STATIC) == isStatic) {
            (void)variables.emplace_back(it.get());
        }
    }
    return variables.empty() ||
        std::any_of(variables.cbegin(), variables.cend(), [](auto it) { return it->toBeCompiled; });
}
} // namespace

OwnedPtr<FuncDecl> TypeCheckUtil::CreateDefaultCtor(InheritableDecl& decl, bool isStatic)
{
    OwnedPtr<FuncBody> funcBody = MakeOwnedNode<FuncBody>();
    CopyFileID(funcBody.get(), &decl);
    funcBody->body = MakeOwnedNode<Block>();
    CopyFileID(funcBody->body.get(), &decl);
    auto funcParamList = MakeOwnedNode<FuncParamList>();
    CopyFileID(funcParamList.get(), &decl);
    funcBody->paramLists.push_back(std::move(funcParamList));
    OwnedPtr<FuncDecl> initFunc = MakeOwnedNode<FuncDecl>();
    CopyFileID(initFunc.get(), &decl);
    initFunc->funcBody = std::move(funcBody);
    initFunc->funcBody->funcDecl = initFunc.get();
    initFunc->identifier = "init";
    initFunc->identifier.SetFileID(decl.begin.fileID);
    initFunc->begin = decl.identifier.Begin();                    // Set function postion to declare's line.
    initFunc->end = initFunc->begin + decl.identifier.Length(); // Set function end postion to end of declare name.
    initFunc->funcBody->begin = initFunc->begin;
    initFunc->funcBody->end = initFunc->end;
    initFunc->outerDecl = &decl;
    initFunc->EnableAttr(Attribute::IMPLICIT_ADD);
    initFunc->toBeCompiled = HasRecompilingMemberVar(decl.GetMemberDecls(), isStatic);
    if (isStatic) {
        // Static init is always 'private'.
        initFunc->EnableAttr(Attribute::STATIC, Attribute::PRIVATE);
    } else {
        initFunc->EnableAttr(Attribute::PUBLIC);
    }
    SetCtorFuncPosition(decl, *initFunc); // If member exists, set function position to member's line.
    initFunc->EnableAttr(Attribute::CONSTRUCTOR, Attribute::COMPILER_ADD);
    if (decl.astKind == ASTKind::STRUCT_DECL) {
        auto structDecl = StaticAs<ASTKind::STRUCT_DECL>(&decl);
        initFunc->funcBody->parentStruct = structDecl;
    } else if (decl.astKind == ASTKind::CLASS_DECL) {
        auto classDecl = StaticAs<ASTKind::CLASS_DECL>(&decl);
        initFunc->funcBody->parentClassLike = classDecl;
    }
    return initFunc;
}

Ptr<FuncBody> TypeCheckUtil::GetCurFuncBody(const ASTContext& ctx, const std::string& scopeName)
{
    auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, scopeName);
    Ptr<FuncBody> ret{nullptr};
    if (sym) {
        if (auto fd = AST::As<ASTKind::FUNC_DECL>(sym->node); fd) {
            ret = fd->funcBody.get();
        } else if (auto le = AST::As<ASTKind::LAMBDA_EXPR>(sym->node); le) {
            ret = le->funcBody.get();
        } else if (auto md = AST::As<ASTKind::MACRO_DECL>(sym->node); md) {
            ret = md->desugarDecl->funcBody.get();
        } else if (auto pcd = AST::As<ASTKind::PRIMARY_CTOR_DECL>(sym->node); pcd) {
            ret = pcd->funcBody.get();
        }
    }
    return ret;
}

// if there is no default constructor, insert one.
// NOTICE: it will change AST Node!
void TypeChecker::TypeCheckerImpl::AddDefaultCtor(InheritableDecl& decl) const
{
    if (decl.astKind != ASTKind::STRUCT_DECL && decl.astKind != ASTKind::CLASS_DECL) {
        return;
    }

    // Do not add default constructor to Objective-C mirrors declarations
    // because it requires explicitly added one
    if (decl.TestAnyAttr(Attribute::OBJ_C_MIRROR)) {
        return;
    }

    // Do not add default constructor to common class(struct) because
    // it requires explicitly added one
    // Do not add default constructor to platform class(struct) because it
    // always has one in common class(struct) due to above requirement
    if (decl.astKind == ASTKind::STRUCT_DECL) {
        auto structDecl = StaticAs<ASTKind::STRUCT_DECL>(&decl);
        if (!structDecl->TestAnyAttr(Attribute::COMMON)) {
            structDecl->body->decls.push_back(CreateDefaultCtor(decl));
        }
    } else if (decl.astKind == ASTKind::CLASS_DECL) {
        auto classDecl = StaticAs<ASTKind::CLASS_DECL>(&decl);
        if (!classDecl->TestAnyAttr(Attribute::COMMON, Attribute::JAVA_MIRROR)) {
            classDecl->body->decls.push_back(CreateDefaultCtor(decl));
        }
    }
}

void TypeChecker::TypeCheckerImpl::AddDefaultSuperCall(const FuncBody& funcBody) const
{
    bool hasSuper = false;
    Walker(funcBody.body.get(), [&hasSuper](auto node) {
        if (auto re = DynamicCast<RefExpr*>(node); re && re->isSuper && re->isBaseFunc) {
            hasSuper = true;
            return VisitAction::STOP_NOW;
        } else if (node->astKind == ASTKind::FUNC_BODY) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    if (hasSuper) {
        return; // If the funcbody already contains 'super' expression, do not add super call.
    }
    OwnedPtr<CallExpr> superCall = MakeOwned<CallExpr>();
    CopyBasicInfo(&funcBody, superCall.get());
    auto superExpr = CreateRefExpr("super");
    superExpr->isSuper = true;
    superExpr->isAlone = false;
    superCall->baseFunc = std::move(superExpr);
    CopyBasicInfo(&funcBody, superCall->baseFunc.get());
    superCall->EnableAttr(Attribute::COMPILER_ADD);
    if (funcBody.body) {
        if (funcBody.body->body.empty()) {
            superCall->begin = funcBody.begin;
        }
        funcBody.body->body.insert(funcBody.body->body.begin(), std::move(superCall));
    }
}

void TypeChecker::TypeCheckerImpl::GetTypeArgsOfType(Ptr<Type> type, std::vector<Ptr<Type>>& params)
{
    if (type == nullptr) {
        return;
    }
    params.push_back(type);
    switch (type->astKind) {
        case ASTKind::REF_TYPE: {
            auto ty = StaticAs<ASTKind::REF_TYPE>(type);
            std::for_each(ty->typeArguments.begin(), ty->typeArguments.end(),
                [this, &params](auto& p) { GetTypeArgsOfType(p.get(), params); });
            break;
        }
        case ASTKind::QUALIFIED_TYPE: {
            auto ty = StaticAs<ASTKind::QUALIFIED_TYPE>(type);
            std::for_each(ty->typeArguments.begin(), ty->typeArguments.end(),
                [this, &params](auto& p) { GetTypeArgsOfType(p.get(), params); });
            break;
        }
        case ASTKind::TUPLE_TYPE: {
            auto ty = StaticAs<ASTKind::TUPLE_TYPE>(type);
            std::for_each(ty->fieldTypes.begin(), ty->fieldTypes.end(),
                [this, &params](auto& p) { GetTypeArgsOfType(p.get(), params); });
            break;
        }
        case ASTKind::FUNC_TYPE: {
            auto ty = StaticAs<ASTKind::FUNC_TYPE>(type);
            std::for_each(ty->paramTypes.begin(), ty->paramTypes.end(),
                [this, &params](auto& p) { GetTypeArgsOfType(p.get(), params); });
            GetTypeArgsOfType(ty->retType.get(), params);
            break;
        }
        case ASTKind::OPTION_TYPE: {
            auto ty = StaticAs<ASTKind::OPTION_TYPE>(type);
            GetTypeArgsOfType(ty->desugarType.get(), params);
            break;
        }
        default:
            break;
    }
}

// Check value type recursive.
void TypeChecker::TypeCheckerImpl::CheckValueTypeRecursiveDFS(Ptr<Decl> root, std::deque<Ptr<Decl>> path)
{
    if (!root || root->checkFlag == InheritanceVisitStatus::VISITED) {
        return;
    }
    if (root->checkFlag == InheritanceVisitStatus::VISITING) {
        // Found a cycle; mark every decl in this cycle.
        auto cycle = GetCycleOnPath(root, path);
        if (HasOnlyStructTypeInCycle(cycle)) {
            std::string str;
            for (auto node : cycle) {
                str += node->identifier + "->";
            }
            str += root->identifier;
            diag.Diagnose(*root, DiagKind::sema_value_type_recursive, str.c_str());
        }
        return;
    }

    root->checkFlag = InheritanceVisitStatus::VISITING;
    path.push_back(root);

    CheckValueTypeRecursiveDFSSwitch(root, path);

    path.pop_back();
    root->checkFlag = InheritanceVisitStatus::VISITED;
}

void TypeChecker::TypeCheckerImpl::CheckValueTypeRecursiveDFSSwitch(Ptr<Decl> root, const std::deque<Ptr<Decl>>& path)
{
    if (root == nullptr) {
        return;
    }
    if (root->astKind != ASTKind::STRUCT_DECL && root->astKind != ASTKind::ENUM_DECL) {
        return;
    }
    if (root->ty != nullptr && root->ty->IsEnum() && DynamicCast<RefEnumTy*>(root->ty)) {
        return;
    }
    auto checkField = [this, &path](const Decl& decl) {
        if (decl.ty->IsEnum() && DynamicCast<RefEnumTy*>(decl.ty)) {
            return;
        }
        auto needCheckElemTy = Is<TupleTy*>(decl.ty) || Is<VArrayTy*>(decl.ty);
        if (!needCheckElemTy) {
            return CheckValueTypeRecursiveDFS(Ty::GetDeclOfTy(decl.ty), path);
        }
        for (auto elementTy : decl.ty->typeArgs) {
            CheckValueTypeRecursiveDFS(Ty::GetDeclOfTy(elementTy), path);
        }
    };
    if (root->astKind == ASTKind::STRUCT_DECL) {
        auto rd = StaticAs<ASTKind::STRUCT_DECL>(root);
        for (auto& d : rd->body->decls) {
            CJC_NULLPTR_CHECK(d);
            if (d->astKind != ASTKind::VAR_DECL || d->TestAttr(Attribute::STATIC)) {
                continue;
            }
            checkField(*d);
        }
    } else {
        Ptr<EnumDecl> ed = StaticAs<ASTKind::ENUM_DECL>(root);
        for (auto& ctor : ed->constructors) {
            if (ctor->astKind != ASTKind::FUNC_DECL) {
                continue;
            }
            auto fd = StaticAs<ASTKind::FUNC_DECL>(ctor.get());
            if (fd->funcBody->paramLists.empty()) {
                continue;
            }
            for (auto& val : fd->funcBody->paramLists[0]->params) {
                checkField(*val);
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckRecursiveConstructorCall(const std::vector<OwnedPtr<Decl>>& decls)
{
    if (decls.empty()) {
        return;
    }
    CJC_NULLPTR_CHECK(decls.front()->outerDecl);
    RecursiveCtorCtx ctx;
    for (auto& decl : decls) {
        CJC_NULLPTR_CHECK(decl);
        if ((decl->astKind == ASTKind::VAR_DECL && !decl->TestAttr(Attribute::STATIC)) ||
            IsInstanceConstructor(*decl)) {
            if (CheckRecursiveCtorFoundCycle(ctx, *decl)) {
                DiagRecursiveConstructorCall(diag, ctx.path);
                return;
            }
        }
    }
}

bool TypeChecker::TypeCheckerImpl::HasModifier(const std::set<Modifier>& modifiers, TokenKind kind) const
{
    return std::any_of(modifiers.begin(), modifiers.end(), [kind](const auto& it) { return it.modifier == kind; });
}

bool TypeChecker::TypeCheckerImpl::IsDeprecatedStrict(
    const Ptr<Decl> decl
) const
{
    for (auto& anno: decl->annotations) {
        if (anno->kind == AnnotationKind::DEPRECATED) {
            std::string message = "";
            std::string since = "";
            bool strict = false;

            AST::ExtractArgumentsOfDeprecatedAnno(anno, message, since, strict);

            return strict;
        }
    }

    InternalError("Declaration was not marked as deprecated.");
    return false;
}

using std::get;

template<> void PData::Commit(Constraint& data)
{
    for (auto& [tyvar, bounds] : data) {
        bounds.lbs.commit();
        bounds.ubs.commit();
        bounds.sum.commit();
        bounds.eq.commit();
    }
}

template<> void PData::Reset(Constraint& data)
{
    for (auto& [tyvar, bounds] : data) {
        bounds.lbs.reset();
        bounds.ubs.reset();
        bounds.sum.reset();
        bounds.eq.reset();
    }
}

template<> CstVersionID PData::Stash(Constraint& data)
{
    CstVersionID ver;
    for (auto& [tyvar, bounds] : data) {
        auto id1 = bounds.lbs.stash();
        auto id2 = bounds.ubs.stash();
        auto id3 = bounds.sum.stash();
        auto id4 = bounds.eq.stash();
        ver[tyvar] = {id1, id2, id3, id4};
    }
    return ver;
}

template<> void PData::Apply(Constraint& data, CstVersionID& version)
{
    for (auto& [tyvar, ver] : version) {
        data[tyvar].lbs.apply(get<0>(ver));
        data[tyvar].ubs.apply(get<1>(ver));
        data[tyvar].sum.apply(get<2>(ver));
        data[tyvar].eq.apply(get<3>(ver));
    }
}

template<> void PData::ResetSoft(Constraint& data)
{
    for (auto& [tyvar, bounds] : data) {
        bounds.lbs.resetSoft();
        bounds.ubs.resetSoft();
        bounds.sum.resetSoft();
        bounds.eq.resetSoft();
    }
}

std::string TyVarBounds::ToString() const
{
    std::string s = "Lower bounds: ";
    s += "{";
    for (auto& ty : std::as_const(lbs)) {
        s += Ty::ToString(ty) + ", ";
    }
    s += "}\n";
    s += "Upper bounds: ";
    s += "{";
    for (auto& ty : std::as_const(ubs)) {
        s += Ty::ToString(ty) + ", ";
    }
    s += "}\n";
    s += "Sum: ";
    s += "{";
    for (auto& ty : std::as_const(sum)) {
        s += Ty::ToString(ty) + ", ";
    }
    s += "}\n";
    s += "Equals: ";
    s += "{";
    for (auto& ty : std::as_const(eq)) {
        s += Ty::ToString(ty) + ", ";
    }
    s += "}\n";
    return s;
}

std::string ToStringC(const Constraint& c)
{
    std::string s;
    for (auto& [tv, bounds] : c) {
        s += Ty::ToString(tv) + " :\n" + bounds.ToString() + "\n";
    }
    return s;
}

std::string ToStringS(const TypeSubst& m)
{
    std::string s = "[";
    for (auto& [tv, ty] : m) {
        s += Ty::ToString(tv) + " |-> " + Ty::ToString(ty) + ", ";
    }
    s += "]\n";
    return s;
}

std::string ToStringMS(const MultiTypeSubst& m)
{
    std::string s = "[";
    for (auto& [tv, tys] : m) {
        s += Ty::ToString(tv) + " |-> {";
        for (auto ty : tys) {
            s += Ty::ToString(ty) + ", ";
        }
        s += "},\n";
    }
    s += "]\n";
    return s;
}

std::string ToStringP(const SubstPack& m)
{
    return ToStringS(m.u2i) + ToStringMS(m.inst);
}
} // namespace Cangjie
