// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CACHE_DATA_POLLUTION_ANALYZER_IMPL_H
#define CANGJIE_CACHE_DATA_POLLUTION_ANALYZER_IMPL_H

#include "cangjie/IncrementalCompilation/IncrementalScopeAnalysis.h"

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/IncrementalCompilation/ASTCacheCalculator.h"
#include "CompilationCacheSerialization.h"
#include "cangjie/IncrementalCompilation/Utils.h"
#include "cangjie/Parse/ASTHasher.h"

#include "ASTDiff.h"

namespace Cangjie::IncrementalCompilation {
// A map that records additional relation info among types
struct TypeMap {
    using TypeRel = std::unordered_map<Ptr<const AST::Decl>, std::set<Ptr<const AST::Decl>>>;
    // A map that record interface extends. key is interface, value is types that extends it
    std::unordered_map<RawMangledName, std::set<RawMangledName>> interfaceExtendTypes;
    // likewise, an instance of this map (a, {b...}) means decl a has direct child types {b...}
    TypeRel children;

    void AddParent(const AST::Decl& parent, const AST::Decl& child)
    {
        (void)children[&parent].insert(&child);
    }

    // truncate raw mangled name of extend to only the type it extends
    static inline std::string TruncateExtendName(const RawMangledName& mangled)
    {
        if (auto pos = mangled.find("<:"); pos != std::string::npos) {
            return mangled.substr(0, pos);
        }
        CJC_ABORT();
        return std::string{};
    }

    const RawMangledName* FindExtendedTypeByExtendDeclMangleName(const RawMangledName& mangle) const
    {
        auto it = extend2Decl.find(mangle);
        if (it != extend2Decl.cend()) {
            return &it->second;
        }
        return {};
    }

    const std::list<RawMangledName>& GetAllExtendsOfType(const RawMangledName& decl) const
    {
        if (auto it = extends.find(decl); it != extends.end()) {
            return it->second;
        }
        return dummyExtends;
    }

    void AddExtend(const RawMangledName& extendedType, const RawMangledName& extend)
    {
        extends[extendedType].push_back(extend);
        extend2Decl[extend] = extendedType;
    }

    void CollectImportedDeclExtraRelation(const AST::Decl& decl);

    // Merge another TypeMap into this. This function is now only used to merge a cached TypeMap instance and a
    // TypeMap generated from reading AST's of imported packages.
    void Merge(TypeMap&& other)
    {
        extend2Decl.merge(other.extend2Decl);
        for (std::move_iterator childrenIt{other.children.begin()};
            childrenIt != std::move_iterator{other.children.end()}; ++childrenIt) {
            if (auto it = children.find(childrenIt->first); it != children.cend()) {
                it->second.merge(childrenIt->second);
            } else {
                children[childrenIt->first] = std::move(childrenIt->second);
            }
        }
        extends.merge(other.extends);
        for (std::move_iterator extendsIt{other.extends.begin()};
            extendsIt != std::move_iterator{other.extends.end()}; ++extendsIt) {
            if (auto it = extends.find(extendsIt->first); it != extends.cend()) {
                it->second.splice(it->second.end(), extendsIt->second);
            } else {
                extends[extendsIt->first] = std::move(extendsIt->second);
            }
        }
        CJC_ASSERT(other.interfaceExtendTypes.empty());
    }

private:
    // map from RawMangledName of extend decl to the decl of the type it extends. used when looking for the extended
    // type when an ExtendDecl is deleted
    std::unordered_map<RawMangledName, RawMangledName> extend2Decl;
    static const inline std::list<RawMangledName> dummyExtends{};
    // A map to record the ExtendDecl infos, where the key here will be the raw mangle name of
    // a type `A`, and the value will be the raw mangle name of all the ExtendDecl which extend `A`
    std::unordered_map<RawMangledName, std::list<RawMangledName>> extends;
};

// A qualified usage is identified by the name being used and the type to the left of '.' operator
struct QualifiedUsage {
    RawMangledName leftDecl;
    std::string name;

    bool operator==(const QualifiedUsage& other) const
    {
        return name == other.name && leftDecl == other.leftDecl;
    }
};
struct Hasher {
    size_t operator()(const QualifiedUsage& usage) const
    {
        return std::hash<std::string>{}(usage.name) ^ std::hash<RawMangledName>{}(usage.leftDecl);
    }
};
// the first key is the identifier of the unqualified name being used, and
// the second key is the scope of the user.
// scope of an unqualified usage is the outerDecl of the decl that uses the unqualified name
// specially, the scope of a top level decl is its package
using UnqualifiedName2Usages =
    std::unordered_map<std::string, std::map<Ptr<const AST::Decl>, std::set<Ptr<const AST::Decl>>>>;
using QualifiedName2Usages = std::unordered_map<QualifiedUsage, std::set<Ptr<const AST::Decl>>, Hasher>;
// the first key is the type identifier decl being used, and
// the second key is the package identifier left to the type name, and is empty when it is an unqualified type usage
using TypeUsages = std::unordered_map<std::string, std::map<std::string, std::set<Ptr<const AST::Decl>>>>;
using PackageQualifedUsages = TypeUsages;

// A map sufficient to populate the impact of any AST change
struct ChangePollutedMap {
    static const inline std::set<Ptr<const AST::Decl>> emptyUses{};
    enum class Idx : size_t { BODY, API };

    template <typename T> struct Pack {
        T body;
        T api;
        const T& operator [](Idx i) const
        {
            return i == Idx::BODY ? body : api;
        }
        T& operator[](Idx i)
        {
            return i == Idx::BODY ? body : api;
        }
    };
    // unqualified usages that ever resolve to imported decl must be recompiled when the name of either the source
    // package or imported packages change
    // A member decl affects only unqualified usages of the same name within its subtype trees;
    // while a global decl affects all unqualifed usages.
    Pack<UnqualifiedName2Usages> unqUses;
    // unqualified usages that resolve to imported decl can only be dirtified by the change of the same name
    // from imported packages
    Pack<UnqualifiedName2Usages> unqUsesOfImported;
    // qualified usages. When index == API, it contains usages in API; when index == BODY, it contains usages in body.
    Pack<QualifiedName2Usages> qUses;
    // package qualified usages
    Pack<PackageQualifedUsages> pqUses;
    // direct usages
    Pack<std::unordered_map<RawMangledName, std::set<Ptr<const AST::Decl>>>> directUses;
    // a pair (A, [B...]) indicates type A is boxed in decls B...
    std::unordered_map<RawMangledName, std::list<Ptr<const AST::Decl>>> boxUses;
    // Map of fullPackageName -> aliased package name. NOTE: this will only be used as pkg qualified usage.
    std::unordered_map<std::string, std::set<std::string>> packageAliasMap;
    // Map of (fullPackageName, decl identifier) -> aliased name. NOTE: this will only be used as unqualified usage.
    std::unordered_map<std::pair<std::string, std::string>, std::set<std::string>, HashPair> declAliasMap;

    const std::set<Ptr<const AST::Decl>>& GetUnqUses(
        bool getImportOnly, ChangePollutedMap::Idx index, const std::string& identifier, const AST::Decl& scope) const
    {
        auto& cont = getImportOnly ? unqUsesOfImported[index] : unqUses[index];
        auto pit = cont.find(identifier);
        if (pit == cont.end()) {
            return emptyUses;
        }
        auto sit = pit->second.find(&scope);
        if (sit == pit->second.end()) {
            return emptyUses;
        }
        return sit->second;
    }

    std::set<Ptr<const AST::Decl>> GetQUses(ChangePollutedMap::Idx index, const std::string& identifier) const
    {
        auto& cont = qUses[index];
        std::set<Ptr<const AST::Decl>> usages;
        for (auto& element : cont) {
            if (element.first.name == identifier) {
                usages.insert(element.second.begin(), element.second.end());
            }
        }
        return usages;
    }

    const std::set<Ptr<const AST::Decl>>& GetPackageQualifiedUses(
        ChangePollutedMap::Idx index, const std::string& identifier, const std::string& packageName) const
    {
        auto& cont = pqUses[index];
        auto pit = cont.find(identifier);
        if (pit == cont.end()) {
            return emptyUses;
        }
        auto sit = pit->second.find(packageName);
        if (sit == pit->second.end()) {
            return emptyUses;
        }
        return sit->second;
    }

    inline std::set<std::string> GetAccessibleDeclName(const AST::Decl& decl) const
    {
        auto pair = std::make_pair(decl.fullPackageName, decl.identifier.Val());
        auto found = declAliasMap.find(pair);
        return found == declAliasMap.end() ? std::set<std::string>{decl.identifier} : found->second;
    }

    inline std::set<std::string> GetAccessiblePackageName(const std::string& fullPackageName) const
    {
        auto found = packageAliasMap.find(fullPackageName);
        return found == packageAliasMap.end() ? std::set<std::string>{fullPackageName} : found->second;
    }
    };

struct PollutionResult {
    IncreKind kind;
    std::unordered_set<Ptr<AST::Decl>> declsToRecompile;
    std::list<RawMangledName> deleted;
    std::list<RawMangledName> reBoxedTypes;
};

struct PollutionAnalyseArgs {
    ModifiedDecls&& rawModified;
    const AST::PackageDecl& pkg;
    const std::unordered_map<Ptr<const AST::Decl>, std::set<Ptr<const AST::Decl>>>& sourcePopulations;
    const SemanticInfo& semaInfo;
    const OptEffectStrMap& chirOptInfo;
    const CachedFileMap& fileMap;
    const ImportManager& man;
    const RawMangled2DeclMap& mangled2Decl;
    std::unordered_map<RawMangledName, std::list<std::pair<Ptr<AST::ExtendDecl>, int>>>&& extends;
    TypeMap&& importedRelations;
};

class PollutionAnalyzer {
public:
    static PollutionResult Get(PollutionAnalyseArgs&& args);

private:
    void AddToPollutedDecls(const AST::Decl& decl);

    // Propagate pollution for added decl, which can be either:
    // 1) a top-level VarDecl, VarWithPatternDecl, FuncDecl, type related decl
    // 2) a member VarDecl, FuncDecl, PropDecl. In this case, we assume the parent decl has been handled
    void PollutionForAddedDecl(const AST::Decl& decl);

    void PollutionForAddedTypeDecl(const AST::Decl& decl);
    void PollutionForAddedNonTypeDecl(const AST::Decl& decl);
    void PollutionForAddedNonTypeDeclImpl(const AST::Decl& decl);

    void AdditionPollutionForAddedExtendDecl(const AST::ExtendDecl& decl);
    void AdditionPollutionAPIOfExtendDecl(const AST::ExtendDecl& decl);
    void AdditionPollutionAPIOfExtendedDecl(const AST::ExtendDecl& decl);
    // Specially, for direct extends, since we merge them into one when calculating the raw mangle name and
    // hash, so we have to manually pollute all other same direct extends
    void AdditionPollutionForBodyChangedExtendDecl(const AST::ExtendDecl& decl);
    void AdditionPollutionAPIOfDirectExtendDecls(const RawMangledName& mangle);
    void PollutionForConstDecl(const AST::Decl& decl);
    void PollutionForOrderChangeDecl(const AST::Decl& decl);

    // Propagate pollution for deleted decl, which can be either:
    // 1) a top-level VarDecl, VarWithPatternDecl, FuncDecl, type related decl
    // 2) a member VarDecl, FuncDecl, PropDecl. In this case, we assume the parent decl has been handled
    void PollutionForDeletedDecl(const RawMangledName& mangle);

    // Propagate pollution for changed non-type decl, which can be either:
    // 1) a top-level VarDecl, VarWithPatternDecl, FuncDecl, type related decl
    // 2) a member VarDecl, FuncDecl, PropDecl. In this case, we assume the parent decl has been handled
    void PollutionForChangedNonTypeDecl(const CommonChange& c);

    void PollutionForBodyChangedDecl(const AST::Decl& decl);

    void PollutionForSigChangedDecl(const AST::Decl& decl);

    void PollutionForSigChangedFuncDecl(const AST::Decl& decl);

    // Following changes will leads to sig change of a VarDecl:
    // 1) body change of a non-explicitly-typed VarDecl
    void PollutionForSigChangedVarDecl(const AST::VarDecl& decl);

    // Following changes will leads to sig change of a VarWithPatternDecl:
    // 1) body change of a non-explicitly-typed VarWithPatternDecl
    void PollutionForSigChangedVarWithPatternDecl(const AST::VarWithPatternDecl& decl);

    void PollutionForSigChangedPropDecl(const AST::PropDecl& decl);

    void PollutionForSigChangedInheritableDecl(const AST::InheritableDecl& decl);

    void PollutionForSrcUseChangedDecl(const AST::Decl& decl);

    // Propagate pollution for changed type decl, which can be enum/struct/class/interface/extend decl
    void PollutionForChangedTypeDecl(const AST::InheritableDecl& decl, const TypeChange& c);

    void PolluteBoxUsesFromDecl(const AST::Decl& decl);

    void PollutionForLayoutChangedDecl(const AST::InheritableDecl& decl);

    void PollutionForVTableChangedDecl(const AST::InheritableDecl& decl);

    void PolluteBodyOfDecl(const AST::Decl& decl);

    std::set<Ptr<const AST::Decl>> FindAllExtendDeclsOfType(const RawMangledName& name);

    std::optional<std::string> GetExtendedTypeRawMangleNameImpl(const AST::Type& extendedType);
    std::optional<std::string> GetExtendedTypeRawMangleName(const AST::ExtendDecl& extend);

    void PolluteAPIOfDecl(const AST::Decl& decl);

    std::unordered_set<Ptr<AST::Decl>> GetPollutionResult() const;

    std::list<RawMangledName> GetDeletedResult() const;
    std::list<RawMangledName> GetReBoxedTypes() const
    {
        return reBoxedTypes;
    }

    // Returns true when the body change of decl need populate,
    // that is, it is src-imported or in generic or is default implementation inside interface.
    // inlining and generic instantiations will be triggered by SEMA and CHIR if the users  of the change decl
    // are populate to.
    bool NeedPollutedInstantiationChange(const AST::Decl& decl) const;

    void PollutedInstantiationChangeFromDecl(const AST::Decl& decl);

    void PolluteCHIROptAffectDecl(const AST::Decl& decl);

    void PollutedToBoxUses(const RawMangledName& mangle);
    // propagate to constructors of a type decl. This function is used when a type has instvar change or the initial
    // value of member variables changed.
    void PolluteToConstructors(const AST::Decl& decl);

    void PollutedToExprUsages(const std::set<Ptr<const AST::Decl>>& usages);
    void PollutePreciseUsages(const AST::Decl& decl);
    void PollutePreciseUsages(const RawMangledName& mangled);
    void PolluteDownStreamTypes(const AST::Decl& decl);
    void PolluteDownStreamTypes(const RawMangledName& mangled);

    // populate to changes of a global name to unqualified usages
    void PollutedUnqualifiedUses(const AST::Decl& decl);

    void PolluteGlobalChangeToQualifiedUses(const AST::Decl& decl);

    // Populate to changes of a global name to qualified usages.
    void PollutedGlobalChangeToPackageQualifiedUses(const AST::Decl& decl);

    // Returns true if we can tell incremental compilation must rollback to full
    bool FallBack() const;

    void PrintFallbackInfo();

    PollutionAnalyzer(ChangePollutedMap&& p, TypeMap&& t, const RawMangled2DeclMap& mp, const OptEffectStrMap& chirOpt,
        std::unordered_map<RawMangledName, std::list<std::pair<Ptr<AST::ExtendDecl>, int>>>&& ex)
        : p(p), t(t), mangled2Decl(mp)
    {
        for (auto pair : chirOpt) {
            std::vector<Ptr<const AST::Decl>> affected{};
            for (auto use : pair.second) {
                if (auto it = mangled2Decl.find(use); it != mangled2Decl.cend()) {
                    affected.push_back(it->second);
                }
            }
            if (!affected.empty()) {
                (void)chirOptMap.emplace(pair.first, std::move(affected));
            }
        }
        for (auto extend : std::move(ex)) {
            std::list<Ptr<AST::ExtendDecl>> l;
            for (auto e : std::as_const(extend.second)) {
                l.emplace_back(e.first);
            }
            directExtends.emplace(std::move(extend.first), std::move(l));
        }
    }

    ChangePollutedMap p;
    TypeMap t;
    const RawMangled2DeclMap& mangled2Decl;
    std::unordered_map<RawMangledName, std::list<Ptr<AST::ExtendDecl>>> directExtends;

    // changed decls that cause rollback
    std::list<Ptr<const AST::Decl>> typeAliases;    // type alias cannot be incrementally compiled and must fall back to
                                                    // full compilation if any change to them exists
    std::list<Ptr<const AST::Decl>> unfoundExtends; // ExtendDecl's whose extended type is not found.
    std::list<RawMangledName> unfoundNames;
    // std::list<Ptr<const Decl>> virtChanges{}; // changes of vtable shall rollback
    std::unordered_map<RawMangledName, std::vector<Ptr<const AST::Decl>>> chirOptMap{};

    // cache changed types to prevent circular population and to boost performance
    struct PollutedCommonChangeRecord {
        bool sig{false}, srcUse{false}, body{false};

        // returns true if any field is true in this record
        operator bool() const
        {
            return sig || srcUse || body;
        }
    };
    std::unordered_map<Ptr<const AST::Decl>, PollutedCommonChangeRecord> changes{};

    struct PollutedTypeChangeRecord {
        bool instVar{false}, virt{false}, sig{false}, visibleAPI{false}, srcUse{false}, body{false}, member{false};
        operator bool() const
        {
            return instVar || virt || sig || visibleAPI || srcUse || member;
        }
    };
    std::unordered_map<Ptr<const AST::InheritableDecl>, PollutedTypeChangeRecord> typeChanges{};

    std::list<RawMangledName> removedNotSupported{};

    struct PollutedOtherUseRecord {
        bool instantiation{false}, chirOpt{false}, box{false};
    };
    std::unordered_map<RawMangledName, PollutedOtherUseRecord> otherChanges{};

    // The set of polluted decls, note that we will NOT use this data structure to further
    // propagate the pollution.
    std::unordered_set<Ptr<const AST::Decl>> pollutedDecls;
    std::list<RawMangledName> deletedDecls;
    std::list<RawMangledName> reBoxedTypes;

    // To avoid repeat analysis
    std::unordered_set<Ptr<const AST::Decl>> visitedBodyPollutedDecls;
    std::unordered_set<Ptr<const AST::Decl>> visitedAPIPollutedDecls;
};
} // namespace Cangjie::IncrementalCompilation
#endif
