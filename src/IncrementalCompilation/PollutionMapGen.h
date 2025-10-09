// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CACHE_DATA_POLLUTION_MAP_GEN_H
#define CANGJIE_CACHE_DATA_POLLUTION_MAP_GEN_H

#include "PollutionAnalyzer.h"

namespace Cangjie::IncrementalCompilation {
// Get all the usages of a decl from a map that records all usages in a decl by mainly reversing it.
// Also for RawMangledName's that cannot be found in cache, its user is directly stored in a recompilation list.
class PollutionMapGen {
public:
    static std::tuple<ChangePollutedMap, TypeMap> Get(const AST::PackageDecl& p,
        const RawMangled2DeclMap& mangledName2AST,
        const std::unordered_map<Ptr<const AST::Decl>, std::set<Ptr<const AST::Decl>>>& sourceImportedInfo,
        const SemanticInfo& usage, const OptEffectStrMap& chirOpt, const ImportManager& man)
    {
        PollutionMapGen i{p, mangledName2AST, sourceImportedInfo, usage};
        i.Collect();
        i.CollectCHIROpt(chirOpt);
        i.CollectAlias(man, *p.srcPackage);
        return {std::move(i.resp), std::move(i.rest)};
    }

private:
    PollutionMapGen(const AST::PackageDecl& p, const RawMangled2DeclMap& mangledName2AST,
        const std::unordered_map<Ptr<const AST::Decl>, std::set<Ptr<const AST::Decl>>>& sourcePopulations,
        const SemanticInfo& graph)
        : mangled2Decl(mangledName2AST), sourcePopulations(sourcePopulations), graph(graph), pdecl(&p)
    {
    }
    const RawMangled2DeclMap& mangled2Decl;
    const std::unordered_map<Ptr<const AST::Decl>, std::set<Ptr<const AST::Decl>>>& sourcePopulations;
    const SemanticInfo& graph; // Will be used for dumping new cache, should not use move.
    Ptr<const AST::PackageDecl> pdecl;
    ChangePollutedMap resp{};
    TypeMap rest{};

    void Collect()
    {
        for (auto& p : graph.usages) {
            CollectPopulation(*p.first, p.second);
        }
        for (auto& p : sourcePopulations) {
            CollectSourceImportedPopulation(*p.first, p.second);
        }
        for (auto& p : graph.relations) {
            CollectRelation(p.first, p.second);
        }
        for (auto& p : graph.builtInTypeRelations) {
            CollectBuiltinRelation(p.first, p.second);
        }
    }

    void CollectCHIROpt(const OptEffectStrMap& chirOpt)
    {
        for (auto& r : chirOpt) {
            auto& l = resp.directUses[ChangePollutedMap::Idx::BODY][r.first];
            for (auto& use : r.second) {
                if (auto it = mangled2Decl.find(use); it != mangled2Decl.cend()) {
                    (void)l.insert(it->second);
                }
            }
        }
    }

    void CollectRelation(const RawMangledName& mangle, const SemaRelation& rel)
    {
        for (auto& ext : rel.extends) {
            rest.AddExtend(mangle, ext);
        }
        for (auto& interf : rel.extendedInterfaces) {
            rest.interfaceExtendTypes[interf].insert(mangle);
        }
        auto declIt = mangled2Decl.find(mangle);
        if (declIt == mangled2Decl.cend()) {
            return;
        }
        auto& decl = *declIt->second;
        for (auto& type : rel.inherits) {
            if (auto it = mangled2Decl.find(type); it != mangled2Decl.cend()) {
                rest.AddParent(*it->second, decl);
            }
        }
    }

    void CollectBuiltinRelation(const std::string& name, const SemaRelation& rel)
    {
        for (auto& ext : rel.extends) {
            rest.AddExtend(name, ext);
        }
        for (auto& interf : rel.extendedInterfaces) {
            (void)rest.interfaceExtendTypes[interf].insert(name);
        }
    }

    Ptr<const AST::Decl> GetUnqualifiedUsageScope(const AST::Decl& decl) const
    {
        if (!decl.outerDecl) {
            return pdecl;
        }
        Ptr<const AST::Decl> outer{decl.outerDecl};
        while (outer->outerDecl) {
            outer = outer->outerDecl;
        }
        return outer;
    }

    void CollectUsedNameInfo(
        const AST::Decl& decl, const std::string& usedName, const NameUsage& use, ChangePollutedMap::Idx index)
    {
        for (auto& u : use.packageQualifiers) {
            (void)resp.pqUses[index][usedName][u].emplace(&decl);
        }
        for (auto& u : use.parentDecls) {
            (void)resp.qUses[index][{u, usedName}].emplace(&decl);
        }
        if (use.hasUnqualifiedUsageOfImported) {
            (void)resp.unqUsesOfImported[index][usedName][GetUnqualifiedUsageScope(decl)].emplace(&decl);
        }
        if (use.hasUnqualifiedUsage || use.hasUnqualifiedUsageOfImported) {
            (void)resp.unqUses[index][usedName][GetUnqualifiedUsageScope(decl)].emplace(&decl);
        }
    }

    void CollectUseInfo(const AST::Decl& decl, const UseInfo& info, ChangePollutedMap::Idx index)
    {
        // collect direct usages
        for (auto& u : info.usedDecls) {
            (void)resp.directUses[index][u].emplace(&decl);
        }
        for (auto& u : info.usedNames) {
            CollectUsedNameInfo(decl, u.first, u.second, index);
        }
    }

    void CollectPopulation(const AST::Decl& enclosingDecl, const SemaUsage& usage)
    {
        CollectUseInfo(enclosingDecl, usage.apiUsages, ChangePollutedMap::Idx::API);
        CollectUseInfo(enclosingDecl, usage.bodyUsages, ChangePollutedMap::Idx::BODY);
        for (auto& name : usage.boxedTypes) {
            resp.boxUses[name].emplace_back(&enclosingDecl);
        }
    }

    void CollectSourceImportedPopulation(const AST::Decl& src, const std::set<Ptr<const AST::Decl>>& dsts)
    {
        for (auto& dst : dsts) {
            resp.directUses[ChangePollutedMap::Idx::BODY][src.rawMangleName].emplace(dst);
        }
    }

    void CollectAlias(const ImportManager& importMgr, const AST::Package& pkg)
    {
        for (auto& file : std::as_const(pkg.files)) {
            for (auto& import : file->imports) {
                if (!import->IsImportAlias() && !import->IsImportSingle()) {
                    continue;
                }
                auto& im = import->content;
                std::string prefix = im.GetPrefixPath();
                auto mayBeName = im.GetImportedPackageName();
                auto& name = import->IsImportAlias() ? im.aliasName : im.identifier;
                if (auto pd = importMgr.GetPackageDecl(mayBeName)) {
                    resp.packageAliasMap[mayBeName].emplace(name.Val());
                } else {
                    resp.declAliasMap[std::make_pair(prefix, im.identifier)].emplace(im.aliasName.Val());
                }
            }
        }
    }
};
}

#endif