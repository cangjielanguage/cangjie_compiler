// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the elimination of recursive types.
 */

#include "TypeCheckerImpl.h"

using namespace Cangjie;
using namespace AST;

namespace {
struct CmpNodeByPackagePos {
    bool operator()(Ptr<const AST::Node> n1, Ptr<const AST::Node> n2) const
    {
        if (n1 && n1->curFile && n1->curFile->curPackage && n2 && n2->curFile && n2->curFile->curPackage) {
            if (int cp1 = n1->curFile->curPackage->fullPackageName.compare(n2->curFile->curPackage->fullPackageName);
                cp1 != 0) {
                return cp1 < 0;
            }
        }
        return CompNodeByPos(n1, n2);
    }
};

// A dependency graph among value types.
// There are two kinds of vertices in the graph: `StructDecl` and `EnumDecl`.
// There is an arc from `u` to `v`, if `v` is a member variable of a `struct` `u`,
// or `v` is a parameter of a constructor of an `enum` `u`,
class Graph {
public:
    explicit Graph(const std::vector<Ptr<PackageDecl>>& pkgs)
    {
        for (auto pkg : pkgs) {
            CJC_NULLPTR_CHECK(pkg);
            AddArcs(*pkg->srcPackage);
        }
    }

    const std::set<Ptr<const Decl>, CmpNodeByPackagePos>& InEdges(const EnumDecl& ed) const
    {
        return enumInEdges.at(&ed);
    }

    std::vector<Graph> StronglyConnectedComponents() const
    {
        TarjanContext ctx;
        for (auto u : vertices) {
            CJC_NULLPTR_CHECK(u);
            if (!Utils::InKeys(u, ctx.indices)) {
                TarjanSCC(ctx, *u);
            }
        }
        return std::move(ctx.sccs);
    }

    // Peek the `enum` with the maximum in-degree to box.
    // Will peek the foremost one if there are more than one maximums.
    // Returns `nullptr` if there are no `enum`s in the graph or the SCC is trivial.
    Ptr<const EnumDecl> PeekEnumToBox() const
    {
        Ptr<const EnumDecl> candidate = nullptr;
        size_t maxInDegree = 0;
        for (auto& [ed, inEdges] : enumInEdges) {
            size_t inDegree = inEdges.size();
            if (inDegree > maxInDegree) {
                candidate = ed;
                maxInDegree = inDegree;
            }
        }
        return candidate;
    }

    void RemoveVertex(const EnumDecl& ed)
    {
        vertices.erase(&ed);
        for (auto u : enumInEdges.at(&ed)) {
            outEdges.at(u).erase(&ed);
        }
        outEdges.erase(&ed);
        enumInEdges.erase(&ed);
    }

private:
    Graph()
    {
    }

    void AddVertex(const Decl& v)
    {
        if (!v.GetTy().IsCorrect() || v.GetTy()->HasGeneric()) {
            return;
        }
        vertices.emplace(&v);
        if (!Utils::InKeys(Ptr(&v), outEdges)) {
            outEdges[&v] = {};
        }
        if (v.astKind == ASTKind::ENUM_DECL) {
            auto& ed = StaticCast<const EnumDecl&>(v);
            if (!Utils::InKeys(Ptr(&ed), enumInEdges)) {
                enumInEdges[&ed] = {};
            }
        }
    }

    void AddArc(const Decl& u, const Decl& v)
    {
        if (!u.GetTy().IsCorrect() || u.GetTy()->HasGeneric() || !v.GetTy().IsCorrect() || v.GetTy()->HasGeneric()) {
            return;
        }
        AddVertex(u);
        AddVertex(v);
        outEdges[&u].emplace(&v);
        if (v.astKind == ASTKind::ENUM_DECL) {
            auto& ed = StaticCast<const EnumDecl&>(v);
            enumInEdges[&ed].emplace(&u);
        }
    }

    void AddArcs(const Decl& decl, const Ty& fieldTy)
    {
        if (!Ty::IsTyCorrect(&fieldTy)) {
            return;
        } else if ((fieldTy.IsStruct() || fieldTy.IsEnum()) && Ty::GetDeclOfTy(&fieldTy)) {
            AddArc(decl, *Ty::GetDeclOfTy(&fieldTy));
        } else if (fieldTy.IsTuple()) {
            for (auto typeArg : fieldTy.typeArgs) {
                if (Ty::IsTyCorrect(typeArg)) {
                    AddArcs(decl, *typeArg);
                }
            }
        }
    }

    void AddArcs(const StructDecl& sd)
    {
        CJC_NULLPTR_CHECK(sd.body);
        for (auto& d : sd.body->decls) {
            CJC_NULLPTR_CHECK(d);
            if (d->astKind != ASTKind::VAR_DECL || d->TestAttr(Attribute::STATIC) || !d->GetTy().IsCorrect()) {
                continue;
            }
            AddArcs(sd, *d->GetTy());
        }
    }

    void AddArcs(const EnumDecl& ed)
    {
        for (auto& ctor : ed.constructors) {
            CJC_NULLPTR_CHECK(ctor);
            if (auto fd = DynamicCast<const FuncDecl*>(ctor.get())) {
                CJC_ASSERT(fd->funcBody && fd->funcBody->paramLists.size() == 1 && fd->funcBody->paramLists.front());
                for (auto& param : fd->funcBody->paramLists.front()->params) {
                    CJC_NULLPTR_CHECK(param);
                    if (param->GetTy().IsCorrect()) {
                        AddArcs(ed, *param->GetTy());
                    }
                }
            }
        }
    }

    void AddArcs(const Decl& decl)
    {
        if (decl.astKind == ASTKind::STRUCT_DECL) {
            AddArcs(StaticCast<const StructDecl&>(decl));
        } else if (decl.astKind == ASTKind::ENUM_DECL) {
            AddArcs(StaticCast<const EnumDecl&>(decl));
        }
    }

    void AddArcs(const Package& pkg)
    {
        IterateToplevelDecls(pkg, [this](auto& decl) {
            CJC_NULLPTR_CHECK(decl);
            AddArcs(*decl);
        });
        for (auto& decl : pkg.genericInstantiatedDecls) {
            CJC_NULLPTR_CHECK(decl);
            AddArcs(*decl);
        }
    }

    // Get the vertex-induced sub-graph.
    Graph SubGraph(const std::unordered_set<Ptr<const Decl>>& subVertices) const
    {
        Graph subGraph;
        for (auto& [u, uOutEdges] : outEdges) {
            CJC_NULLPTR_CHECK(u);
            if (!Utils::In(u, subVertices)) {
                continue;
            }
            subGraph.AddVertex(*u);
            for (auto v : uOutEdges) {
                if (Utils::In(v, subVertices)) {
                    CJC_ASSERT(u && v);
                    subGraph.AddArc(*u, *v);
                }
            }
        }
        return subGraph;
    }

    struct TarjanContext {
        size_t index = 0;
        std::vector<Ptr<const Decl>> stack;
        std::unordered_map<Ptr<const Decl>, size_t> indices;  /**< The discovered order of vertices in a DFS. */
        std::unordered_map<Ptr<const Decl>, size_t> lowlinks; /**< The smallest index reachable from the vertex. */
        std::unordered_map<Ptr<const Decl>, bool> onStack;    /**< Indicate whether the vertex is on stack. */
        std::vector<Graph> sccs;                              /**< Strongly connected components. */
    };

    void TarjanSCC(TarjanContext& ctx, const Decl& u) const
    {
        ctx.indices[&u] = ctx.index;
        ctx.lowlinks[&u] = ctx.index;
        ++ctx.index;
        ctx.stack.emplace_back(&u);
        ctx.onStack[&u] = true;
        CJC_ASSERT(Utils::InKeys(Ptr(&u), outEdges));
        for (auto v : outEdges.at(&u)) {
            CJC_NULLPTR_CHECK(v);
            if (!Utils::InKeys(v, ctx.indices)) {
                TarjanSCC(ctx, *v);
                ctx.lowlinks[&u] = std::min(ctx.lowlinks[&u], ctx.lowlinks[v]);
            } else if (ctx.onStack[v]) {
                ctx.lowlinks[&u] = std::min(ctx.lowlinks[&u], ctx.indices[v]);
            }
        }
        if (ctx.lowlinks[&u] == ctx.indices[&u]) {
            std::unordered_set<Ptr<const Decl>> subVertices;
            Ptr<const Decl> w = nullptr;
            do {
                w = ctx.stack.back();
                ctx.stack.pop_back();
                ctx.onStack[w] = false;
                subVertices.emplace(w);
            } while (w != &u);
            ctx.sccs.emplace_back(SubGraph(subVertices));
        }
    }

    std::set<Ptr<const Decl>, CmpNodeByPackagePos> vertices;
    std::map<Ptr<const Decl>, std::set<Ptr<const Decl>, CmpNodeByPackagePos>, CmpNodeByPackagePos> outEdges;
    // We don't care about the in-edges of `struct`s
    std::map<Ptr<const EnumDecl>, std::set<Ptr<const Decl>, CmpNodeByPackagePos>, CmpNodeByPackagePos> enumInEdges;
};

void CheckAndUpdateDeclTyWithNewTy(Decl& decl, const EnumTy& specifiedTy, TypeManager& typeManager)
{
    const Ty* specifiedPtr = static_cast<const Ty*>(&specifiedTy);
    if (decl.GetTy()->IsTuple()) {
        auto tupleTy = RawStaticCast<TupleTy*>(decl.DataTy());
        bool hasSpecifiedTy = std::find_if(tupleTy->typeArgs.begin(), tupleTy->typeArgs.end(),
                                  [specifiedPtr](const ModalTy& typeArg) { return typeArg.get() == specifiedPtr; }) !=
            tupleTy->typeArgs.end();
        if (!hasSpecifiedTy) {
            return;
        }
        std::vector<DataTy> vec;
        for (auto elemTy : tupleTy->TyArgs()) {
            if (elemTy.get() == specifiedPtr) {
                auto enumTy = RawStaticCast<EnumTy*>(elemTy);
                auto newTy = typeManager.GetRefEnumTy(*enumTy->declPtr, enumTy->TyArgs());
                enumTy->hasCorrespondRefEnumTy = true;
                newTy->decl = enumTy->decl;
                vec.emplace_back(newTy);
            } else {
                vec.emplace_back(elemTy);
            }
        }
        decl.SetTy({typeManager.GetTupleTy(vec)});
    } else if (decl.DataTy() == specifiedPtr) {
        auto enumTy = RawStaticCast<EnumTy*>(decl.DataTy());
        auto newTy = typeManager.GetRefEnumTy(*enumTy->declPtr, enumTy->TyArgs());
        enumTy->hasCorrespondRefEnumTy = true;
        newTy->decl = enumTy->decl;
        decl.SetTy({newTy});
    }
}
} // namespace

void TypeChecker::TypeCheckerImpl::UpdateMemberVariableTy(const Decl& decl, const EnumTy& eTy)
{
    switch (decl.astKind) {
        case ASTKind::STRUCT_DECL: {
            auto rd = StaticCast<StructDecl*>(&decl);
            for (auto& d : rd->body->decls) {
                if (!d || d->astKind != ASTKind::VAR_DECL || d->TestAttr(Attribute::STATIC)) {
                    continue;
                }
                CheckAndUpdateDeclTyWithNewTy(*d, eTy, typeManager);
            }
            break;
        }
        case ASTKind::ENUM_DECL: {
            auto ed = StaticCast<EnumDecl*>(&decl);
            for (auto& ctor : ed->constructors) {
                if (ctor->astKind != ASTKind::FUNC_DECL) {
                    continue;
                }
                auto fd = StaticCast<FuncDecl*>(ctor.get());
                for (auto& d : fd->funcBody->paramLists[0]->params) {
                    CheckAndUpdateDeclTyWithNewTy(*d, eTy, typeManager);
                }
            }
            break;
        }
        default:
            CJC_ASSERT(false && "wrong branch");
            break;
    }
}

void TypeChecker::TypeCheckerImpl::PerformRecursiveTypesElimination()
{
    // Since `RefEnumTy` and `EnumTy` cannot be distinguished in cjo, we have to check all the imported packages.
    // NOTE: This api will also contains current source package.
    Graph graph(importManager.GetAllImportedPackages());
    std::vector<Graph> sccs = graph.StronglyConnectedComponents();
    while (!sccs.empty()) {
        Graph scc = std::move(sccs.back());
        sccs.pop_back();
        auto ed = scc.PeekEnumToBox();
        if (ed == nullptr) {
            continue;
        }
        auto& inEdges = scc.InEdges(*ed);
        for (auto u : inEdges) {
            CJC_NULLPTR_CHECK(u);
            UpdateMemberVariableTy(*u, *StaticCast<EnumTy*>(ed->DataTy()));
        }
        scc.RemoveVertex(*ed);
        auto subSCCs = scc.StronglyConnectedComponents();
        std::copy(std::move_iterator{subSCCs.cbegin()}, std::move_iterator{subSCCs.cend()}, std::back_inserter(sccs));
    }
}
