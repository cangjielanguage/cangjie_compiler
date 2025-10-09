// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the ASTContext related classes, which holds the data for typecheck and later procedures.
 */

#ifndef CANGJIE_AST_ASTCONTEXT_H
#define CANGJIE_AST_ASTCONTEXT_H

#include <list>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "cangjie/AST/Node.h"
#include "cangjie/AST/ScopeManagerApi.h"
#include "cangjie/AST/Searcher.h"
#include "cangjie/AST/Symbol.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/AST/Cache.h"

namespace Cangjie {
/** Lucene like inverted index. */
struct InvertedIndex {
    /** Inverted index of Symbol's name. */
    std::unordered_map<std::string, std::set<AST::Symbol*>> nameIndexes;
    /** Inverted index of Symbol's scope name. */
    std::unordered_map<std::string, std::set<AST::Symbol*>> scopeNameIndexes;
    /** Map of scope gate. */
    std::unordered_map<std::string, AST::Symbol*> scopeGateMap;
    /** Inverted index of Symbol's scope level. */
    std::unordered_map<uint32_t, std::set<AST::Symbol*>> scopeLevelIndexes;
    /** Inverted index of Symbol's ast kind. */
    std::unordered_map<std::string, std::set<AST::Symbol*>> astKindIndexes;
    /** Put @c name into a trie, easy to do prefix search. */
    std::unique_ptr<Trie> nameTrie = std::make_unique<Trie>();
    /** Put @c scopeName into a trie, easy to do prefix search. */
    std::unique_ptr<Trie> scopeNameTrie = std::make_unique<Trie>();
    /** Put @c astKind into a trie, easy to do suffix search. */
    std::unique_ptr<Trie> astKindTrie = std::make_unique<Trie>();
    /** Put the begin position of a symbol into a trie, easy to do range search. */
    std::unique_ptr<Trie> posBeginTrie = std::make_unique<Trie>();
    /** Put the begin position of a symbol into a trie, easy to do range search. */
    std::unique_ptr<Trie> posEndTrie = std::make_unique<Trie>();
    /** The minimum possible position. */
    Position minPos = BEGIN_POSITION;
    /** The maximum possible position. */
    Position maxPos = {0, PosSearchApi::MAX_LINE, PosSearchApi::MAX_COLUMN};

    /** Clear all indexes. */
    void Reset();

    /** Build inverted index. */
    void Index(AST::Symbol* symbol, bool withTrie = true);

    /** Delete inverted index. */
    void Delete(AST::Symbol* symbol);
};

// Names is constructed with 'declaration name, scopeName'
using Names = std::pair<std::string, std::string>;
// ty var -> upperbound -> AST nodes of generic constraints
using GCBlames = std::map<Ptr<AST::Ty>, std::map<Ptr<AST::Ty>, std::set<Ptr<const AST::Node>>>>;

/** AST Context for sema and codegen. */
class ASTContext {
public:
    ASTContext(DiagnosticEngine& diag, AST::Package& pkg);
    ~ASTContext() = default;

    /**
     * Delete desugar expr do two things:
     * 1. Recursively delete inverted indexes of the sub symbols of the symbol.
     * 2. Reset the desugar expr.
     *
     * @see DeleteInvertedIndexes
     */
    void DeleteDesugarExpr(OwnedPtr<AST::Expr>& desugar);
    void DeleteInvertedIndexes(Ptr<AST::Node> root);
    void DeleteCurrentInvertedIndexes(Ptr<const AST::Node> node);
    // recursively clear all cache entries of sub-tree
    void ClearTypeCheckCache(const AST::Node& root);
    // only remove cache for this node
    void RemoveTypeCheckCache(const AST::Node& node);
    // set a dummy last key so that the synthesize of the node will be skipped when possible
    void SkipSynForCorrectTy(const AST::Node& node);
    // Skip Syn for all nodes with correct ty
    void SkipSynForCorrectTyRec(const AST::Node& root);

    /**
     * Reset a AST context.
     */
    void Reset();

    static std::string GetPackageName(Ptr<AST::Node> node);
    bool HasTargetTy(Ptr<const AST::Node> node) const;
    void AddDeclName(const Names& names, AST::Decl& decl);
    void RemoveDeclByName(const Names& names, const AST::Decl& decl);
    std::vector<Ptr<AST::Decl>> GetDeclsByName(const Names& names) const;

    /**
     * @brief Store outer variable with pattern declaration.
     * @details This function stores a variable declaration from the outer scope that includes a pattern.
     *
     * @param vd The variable declaration from the outer scope.
     * @param vpd The variable declaration with pattern to be stored.
     */
    void StoreOuterVarWithPatternDecl(const AST::VarDecl& vd, AST::VarWithPatternDecl& vpd);

    /**
     * @brief Get the outer variable declaration abstract.
     * @details This function retrieves the abstract of a variable declaration from the outer scope.
     *
     * @param vd The variable declaration from the outer scope.
     * @return The abstract of the variable declaration.
     */
    AST::VarDeclAbstract& GetOuterVarDeclAbstract(AST::VarDecl& vd) const;

    /**
     * @brief Insert an enum constructor.
     * @details This function inserts a constructor for an enum type with the specified name, argument size, and
     * declaration.
     *
     * @param name The name of the enum constructor.
     * @param argSize The size of the arguments for the enum constructor.
     * @param decl The declaration of the enum type.
     * @param enableMacroInLsp Whether enable macro in LSP.
     */
    void InsertEnumConstructor(const std::string& name, size_t argSize, AST::Decl& decl, bool enableMacroInLsp);
    bool IsEnumConstructor(const std::string& name) const;
    /**
     * @brief Find an enum constructor.
     * @details This function finds a constructor for an enum type with the specified name and argument size.
     *
     * @param name The name of the enum constructor.
     * @param argSize The size of the arguments for the enum constructor.
     * @return A vector of pointers to the declarations of the enum constructors that match the specified name and
     * argument size.
    */
    const std::vector<Ptr<AST::Decl>>& FindEnumConstructor(const std::string& name, size_t argSize) const;
    std::set<Ptr<AST::Decl>> Mem2Decls(const AST::MemSig& memSig);

    DiagnosticEngine& diag;
    Ptr<AST::Package> const curPackage{nullptr};
    const std::string fullPackageName;

    /** An unified table, contain all info. */
    std::list<std::unique_ptr<AST::Symbol>> symbolTable;
    InvertedIndex invertedIndex;
    unsigned currentScopeLevel{0};
    unsigned currentMaxDepth{0};
    std::string currentScopeName{TOPLEVEL_SCOPE_NAME};
    /**
     * Current symbols being checked which are stored in a stack. Push the node to the stack when entering a
     * `VarDecl`, `FuncDecl` or other high level check target, and pop the node after finishing the checking.
     */
    std::stack<Ptr<AST::Node>> currentCheckingNodes{};

    std::unordered_map<Ptr<const AST::Node>, Ptr<AST::Ty>>
        targetTypeMap; /**< The node should be inferred to the corresponding type. */
    std::unordered_map<Ptr<const AST::Node>, Ptr<AST::Ty>>
        lastTargetTypeMap; /**< Target last time the expr is checked */
    std::unordered_map<Ptr<const AST::Node>, AST::TypeCheckCache> typeCheckCache;
    std::unordered_map<Ptr<AST::Ty>, OwnedPtr<AST::ClassDecl>> typeToAutoBoxedDeclMap;
    std::unordered_map<Ptr<AST::Ty>, OwnedPtr<AST::ClassDecl>> typeToAutoBoxedDeclBaseMap;
    std::unique_ptr<Searcher> searcher = std::make_unique<Searcher>();
    /** A vector for checking qualified types. */
    std::vector<Ptr<AST::PackageDecl>> packageDecls;
    /* Used to lookup possible types given a member's signature, not considering inherited members.
     * Use function Mem2Decls to include inheriting types. */
    std::unordered_map<AST::MemSig, std::set<Ptr<AST::Decl>>, AST::MemSigHash> mem2Decls;
    /* from a decl to its subtype decls visible in current package, as a supplement to mem2Decls */
    std::unordered_map<Ptr<AST::Decl>, std::set<Ptr<AST::Decl>>> subtypeDeclsMap;
    /** track source of generic parameters' upperbounds, for diagnose **/
    GCBlames gcBlames;
    /** Lambda nodes that are 'direct' sub-expression of some func call's arg.
     *  Should never create new placeholder ty vars for these lambdas.
     *  In case the func params' instantiated types can be known, then there's expected type.
     *  In case some of the func's generic args can't be solved, placeholders for these generic args will
     *  be propagated, and no new ones will need to be created. */
    std::unordered_set<Ptr<const AST::LambdaExpr>> funcArgReachable;

private:
    /** Mapping from VarDecl to the outer VarWithPatternDecl, helps for finding the initializer. */
    std::unordered_map<Ptr<const AST::VarDecl>, Ptr<AST::VarWithPatternDecl>> varDeclToVarWithPatternDeclMap;
    /** Mapping from name to mapping from arguments size to constructor declaration. */
    std::unordered_map<std::string, std::unordered_map<size_t, std::vector<Ptr<AST::Decl>>>> enumConstructors;
    /**
     * declMap is a map used to look up an target declaration of a reference with known reference name and scope name.
     * Therefore, the key of declMap is an pair of type <string, string>, whose first element is the declaration name
     * and the second element is the name of the scope it locates to make an declaration unique.
     */
    std::unordered_map<std::pair<std::string, std::string>, std::vector<Ptr<AST::Decl>>, HashPair> declMap;
    bool IsNodeInOriginalMacroCallNodes(AST::Decl& decl) const;
};

/**
 * This structure is used for lsp dot completion.
 * Since std::variant cannot perform correctly in windows, we chose to use this custom type.
 */
struct Candidate {
    std::vector<Ptr<AST::Decl>> decls;
    std::unordered_set<Ptr<AST::Ty>> tys;
    bool hasDecl;
    explicit Candidate(const std::vector<Ptr<AST::Decl>>& decls) : decls(decls), hasDecl(true)
    {
    }
    explicit Candidate(const std::unordered_set<Ptr<AST::Ty>>& tys) : tys(tys), hasDecl(false)
    {
    }
    Candidate() = default;
};
} // namespace Cangjie

#endif // CANGJIE_AST_ASTCONTEXT_H
