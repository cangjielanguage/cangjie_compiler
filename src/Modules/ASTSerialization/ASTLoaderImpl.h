// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares theAST Loader related classes.
 */

#ifndef CANGJIE_MODULES_ASTSERIALIZATION_ASTLOADER_IMPL_H
#define CANGJIE_MODULES_ASTSERIALIZATION_ASTLOADER_IMPL_H

#include "flatbuffers/ModuleFormat_generated.h"

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Modules/ASTSerialization.h"
#include "cangjie/Modules/CjoManager.h"

#include "ASTSerializeUtils.h"

namespace Cangjie {
class ASTLoader::ASTLoaderImpl {
public:
    ASTLoaderImpl(std::vector<uint8_t>&& data, const std::string& fullPackageName, TypeManager& typeManager,
        const CjoManager& cjoManager, const GlobalOptions& opts)
        : importedPackageName(fullPackageName),
          data(std::move(data)),
          typeManager(typeManager),
          diag(cjoManager.GetDiag()),
          sourceManager(diag.GetSourceManager()),
          cjoManager(cjoManager),
          opts(opts)
    {
        InitializeTypeLoader();
    }
    ~ASTLoaderImpl()
    {
        package = nullptr;
        curPackage = nullptr;
    }
    OwnedPtr<AST::Package> LoadPackageDependencies();
    void LoadPackageDecls();
    std::unordered_set<std::string> LoadCachedTypeForPackage(
        const AST::Package& sourcePackage, const std::map<std::string, Ptr<AST::Decl>>& mangledName2DeclMap);
    std::string LoadPackageDepInfo();
    void LoadRefs();
    void SetImportSourceCode(bool enable)
    {
        importSrcCode = enable;
    }

    std::string importedPackageName;
    Ptr<AST::Ty> LoadType(FormattedIndex type);
    // A flag to avoid conflicts when we are reusing the AST serialiser from CHIR
    bool isChirNow = false;

private:
friend ASTLoader;
    std::vector<uint8_t> data;
    TypeManager& typeManager;
    DiagnosticEngine& diag;
    SourceManager& sourceManager;
    const CjoManager& cjoManager;
    const GlobalOptions& opts;
    const PackageFormat::Package* package{nullptr};
    // Store dependent package names in loading index order.
    std::vector<std::string> importedFullPackageNames;
    // Loaded all types and decls
    std::vector<Ptr<AST::Ty>> allTypes;
    // When exporting ast, we use fileOffest in each package and index is offset in vector puls 1.
    std::vector<unsigned int> allFileIds;
    std::unordered_map<unsigned int, Ptr<AST::File>> idToFileMap;
    std::unordered_map<int64_t, Ptr<AST::Decl>> allLoadedDecls;
    std::unordered_map<int64_t, Ptr<AST::Expr>> allLoadedExprs;
    bool HasInlineDefaultParamFunc(const PackageFormat::Decl& decl) const;
    std::unordered_set<Ptr<AST::Expr>> allDummyExprs;
    std::vector<Ptr<AST::Decl>> importNonGenericSrcFuncDecls; // Only collect global and member decls.
    Ptr<AST::Package> curPackage{nullptr};
    bool importSrcCode{false};

    // Define the context status for loading type cache of incremental compilation.
    bool isLoadCache{false};
    class CacheLoadingStatus {
    public:
        explicit CacheLoadingStatus(bool& v) : status(v)
        {
            status = true;
        }
        ~CacheLoadingStatus()
        {
            status = false;
        }
    private:
        bool& status;
    };

    // Verify legality of data.
    bool VerifyForData(const std::string& id);
    // Methods for loading cache during incremental compilation.
    void PrepareForLoadTypeCache(const AST::Package& pkg);
    void ClearInstantiatedCache(const std::vector<uoffset_t>& instantiatedDeclIndexes,
        std::unordered_set<std::string>& needRemoved);
    void CollectRemovedDecls(const PackageFormat::Decl& decl, std::unordered_set<std::string>& needRemoved) const;
    void CollectRemovedDefaultImpl(
        const AST::Decl& astDecl, const PackageFormat::Decl& decl, std::unordered_set<std::string>& needRemoved) const;
    template <class DeclT> void LoadDesugarDecl(DeclT& decl, uoffset_t index);
    // Load super class type or super interface types.
    void LoadInheritedTypes(const PackageFormat::Decl& decl, AST::InheritableDecl& id);
    void LoadGenericConstraintsRef(const PackageFormat::Generic* genericRef, Ptr<AST::Generic> generic);
    template <typename DeclT> void LoadNominalDeclRef(const PackageFormat::Decl& decl, DeclT& astDecl);
    void LoadTypeAliasDeclRef(const PackageFormat::Decl& decl, AST::TypeAliasDecl& tad);
    void LoadDeclRefs(const PackageFormat::Decl& declObj, AST::Decl& decl);
    OwnedPtr<AST::Decl> LoadFuncParam(const PackageFormat::Decl& decl, int64_t declIndex);
    OwnedPtr<AST::Decl> LoadPropDecl(const PackageFormat::Decl& decl, int64_t declIndex);
    OwnedPtr<AST::Decl> LoadVarDecl(const PackageFormat::Decl& decl, int64_t declIndex);
    OwnedPtr<AST::Decl> LoadVarWithPatternDecl(const PackageFormat::Decl& decl, int64_t declIndex);
    template <typename DeclT>
    OwnedPtr<AST::Decl> LoadNominalDecl(const PackageFormat::Decl& decl, int64_t declIndex);
    OwnedPtr<AST::Decl> LoadTypeAliasDecl(const PackageFormat::Decl& decl, int64_t declIndex);
    OwnedPtr<AST::Decl> LoadBuiltInDecl(const PackageFormat::Decl& decl, int64_t declIndex);
    OwnedPtr<AST::Decl> LoadGenericParamDecl(const PackageFormat::Decl& decl, int64_t declIndex);
    OwnedPtr<AST::Decl> LoadInvalidDecl(const PackageFormat::Decl& decl);
    OwnedPtr<AST::Generic> LoadGeneric(AST::Decl& decl, const PackageFormat::Generic* generic);
    OwnedPtr<AST::FuncBody> LoadFuncBody(
        const PackageFormat::FuncBody& body, bool isInline = false, bool isConst = true);
    void LoadDeclBasicInfo(const PackageFormat::Decl& decl, AST::Decl& astDecl);
    void LoadFuncDeclAdvancedInfo(const PackageFormat::Decl& decl, AST::FuncDecl& funcDecl);
    void LoadEnumDeclAdvancedInfo(const PackageFormat::Decl& decl, AST::EnumDecl& enumDecl);
    void LoadInheritableDeclAdvancedInfo(const PackageFormat::Decl& decl, AST::InheritableDecl& id);
    void AddDeclToImportedPackage(AST::Decl& decl);

    // Get decl pointer according to DeclId obtained from flatbuffers.
    Ptr<AST::Decl> GetDeclFromIndex(const PackageFormat::FullId* fullId);

    template <typename NodeT, typename FormatT, typename... Args>
    OwnedPtr<NodeT> CreateAndLoadBasicInfo(const FormatT& nodeObj, int64_t index, Args&&... args)
    {
        auto node = MakeOwned<NodeT>(std::forward<Args>(args)...);
        if (index != INVALID_FORMAT_INDEX) {
            if constexpr (std::is_base_of<AST::Expr, NodeT>::value) {
                node->overflowStrategy = static_cast<OverflowStrategy>(nodeObj.overflowPolicy());
                (void)allLoadedExprs.emplace(index, node.get());
            } else if constexpr (std::is_base_of<AST::Decl, NodeT>::value) {
                (void)allLoadedDecls.emplace(index, node.get());
            }
        }
        LoadNodePos(nodeObj, *node);
        return node;
    }

    template <typename FormatT> void LoadNodePos(const FormatT& nodeObj, AST::Node& node)
    {
        node.begin = LoadPos(nodeObj.begin());
        node.end = LoadPos(nodeObj.end());
    }

    static inline AST::AttributePack GetAttributes(const PackageFormat::Decl& decl)
    {
        std::vector<std::bitset<AST::ATTR_SIZE>> bitSets(decl.attributes()->size());
        for (uoffset_t i = 0; i < decl.attributes()->size(); ++i) {
            bitSets[i] = decl.attributes()->Get(i);
        }
        return AST::AttributePack(bitSets);
    }

    inline const PackageFormat::Decl* GetFormatDeclByIndex(FormattedIndex declIndex) const
    {
        // NOTE: Serialized 'declIndex' is vector offset plus 1.
        CJC_ASSERT(declIndex != INVALID_FORMAT_INDEX);
        return package->allDecls()->Get(static_cast<uoffset_t>(declIndex - 1));
    }

    inline const PackageFormat::Expr* GetFormatExprByIndex(FormattedIndex exprIndex) const
    {
        // NOTE: Serialized FormattedIndex 'exprIndex' is vector offset plus 1.
        CJC_ASSERT(exprIndex != INVALID_FORMAT_INDEX);
        return package->allExprs()->Get(static_cast<uoffset_t>(exprIndex - 1));
    }

    inline static bool IsTypeDecl(const PackageFormat::Decl& decl)
    {
        return Utils::In(decl.kind(), {PackageFormat::DeclKind_StructDecl, PackageFormat::DeclKind_EnumDecl,
            PackageFormat::DeclKind_ClassDecl, PackageFormat::DeclKind_InterfaceDecl,
            PackageFormat::DeclKind_BuiltInDecl});
    }

    inline bool IgnoreDecl(FormattedIndex offset) const
    {
        auto decl = package->allDecls()->Get(static_cast<uoffset_t>(offset));
        auto attr = GetAttributes(*decl);
        // 1. Ignore main entry function during loading (it is only needed for incremental compilation).
        // 2. Ignore instantiated decl and non-external, non-implicit used decl when 'importSrcCode' is disabled.
        // 3. When current is macro package:
        //    a) ignore instantiated decl. NOTE: macro package's instantiated decls will not be used in CodeGen.
        //    b) ignore non-macro decl when interpretation is off.
        //    c) ignore decl which is not macro and not type when interpretation is on.
        return attr.TestAttr(AST::Attribute::MAIN_ENTRY) ||
            (!importSrcCode &&
                (attr.TestAttr(AST::Attribute::GENERIC_INSTANTIATED) ||
                    (attr.TestAttr(AST::Attribute::PRIVATE) && !attr.TestAttr(AST::Attribute::IMPLICIT_USED)))) ||
            (curPackage->isMacroPackage &&
                (attr.TestAttr(AST::Attribute::GENERIC_INSTANTIATED) || (!attr.TestAttr(AST::Attribute::MACRO_FUNC))));
    }
    // Load and construct Decl Node according to declIndex in package.
    template <typename DeclT = AST::Decl> OwnedPtr<DeclT> LoadDecl(FormattedIndex declIndex);
    // Load FuncParamList Node.
    OwnedPtr<AST::FuncParamList> LoadFuncParamList(const PackageFormat::FuncParamList* funcParamList);
    // Load Position Node according to Position object.
    Position LoadPos(const PackageFormat::Position* posObj);
    void LoadImportContent(AST::ImportContent& content, const PackageFormat::ImportSpec& is) const;
    std::vector<Ptr<AST::Ty>> LoadTypeArgs(const PackageFormat::SemaTy& typeObj);
    void SetGenericTy(FormattedIndex type, const PackageFormat::SemaTy& typeObj);
    bool GetPrimitiveTy(FormattedIndex type, const PackageFormat::SemaTy* typeObj);
    template <typename TypeT, typename TypeDecl = void>
    void SetTypeTy(FormattedIndex type, const PackageFormat::SemaTy& typeObj);
    OwnedPtr<AST::Package> PreLoadImportedPackageNode();
    OwnedPtr<AST::File> CreateFileNode(
        AST::Package& pkg, unsigned int fileID, std::vector<OwnedPtr<AST::ImportSpec>>&& imports);
    void LoadCachedTypeForDecl(const PackageFormat::Decl& decl, AST::Decl& astDecl);
    using DeclLoaderT =
        std::function<OwnedPtr<AST::Decl>(ASTLoaderImpl*, const PackageFormat::Decl&, int64_t exprIndex)>;
    std::unordered_map<PackageFormat::DeclKind, DeclLoaderT> declLoaderMap{
        {PackageFormat::DeclKind_FuncParam, &ASTLoaderImpl::LoadFuncParam},
        {PackageFormat::DeclKind_PropDecl, &ASTLoaderImpl::LoadPropDecl},
        {PackageFormat::DeclKind_VarDecl, &ASTLoaderImpl::LoadVarDecl},
        {PackageFormat::DeclKind_VarWithPatternDecl, &ASTLoaderImpl::LoadVarWithPatternDecl},
        {PackageFormat::DeclKind_FuncDecl, &ASTLoaderImpl::LoadNominalDecl<AST::FuncDecl>},
        {PackageFormat::DeclKind_StructDecl, &ASTLoaderImpl::LoadNominalDecl<AST::StructDecl>},
        {PackageFormat::DeclKind_EnumDecl, &ASTLoaderImpl::LoadNominalDecl<AST::EnumDecl>},
        {PackageFormat::DeclKind_InterfaceDecl, &ASTLoaderImpl::LoadNominalDecl<AST::InterfaceDecl>},
        {PackageFormat::DeclKind_ClassDecl, &ASTLoaderImpl::LoadNominalDecl<AST::ClassDecl>},
        {PackageFormat::DeclKind_ExtendDecl, &ASTLoaderImpl::LoadNominalDecl<AST::ExtendDecl>},
        {PackageFormat::DeclKind_GenericParamDecl, &ASTLoaderImpl::LoadGenericParamDecl},
        {PackageFormat::DeclKind_TypeAliasDecl, &ASTLoaderImpl::LoadTypeAliasDecl},
        {PackageFormat::DeclKind_BuiltInDecl, &ASTLoaderImpl::LoadBuiltInDecl},
    };

    using TyLoaderT = std::function<void(ASTLoaderImpl*, const FormattedIndex, const PackageFormat::SemaTy&)>;
    std::unordered_map<PackageFormat::TypeKind, TyLoaderT> tyLoaderMap;
    /** Initialize map with function to instantiate template separately. */
    void InitializeTypeLoader();

    static inline AST::TypeKind GetASTTypeKind(PackageFormat::TypeKind kind)
    {
        auto found = TYPE_KIND_RMAP.find(kind);
        return found != TYPE_KIND_RMAP.end() ? found->second : AST::TypeKind::TYPE_INVALID;
    }

    /**
     * Load and construct Expr Node according to 'exprIndex' in package.
     * NOTE: Main entrance of 'LoadExpr' is only from 'LoadFuncBody', 'LoadVarDecl' and 'LoadVarWithPatternDecl'.
     */
    template <typename ExprT = AST::Expr> OwnedPtr<ExprT> LoadExpr(FormattedIndex exprIndex);
    OwnedPtr<AST::Expr> LoadInvalidExpr(const PackageFormat::Expr& expr);
    OwnedPtr<AST::Expr> LoadWildcardExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadPrimitiveTypeExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadReturnExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadJumpExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadMemberAccess(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadRefExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadCallExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadUnaryExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadIncOrDecExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadLitConstExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadBinaryExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadSubscriptExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadAssignExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadArrayExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadPointerExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadTypeConvExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadThrowExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadSpawnExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadArrayLit(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadTupleLit(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadIfExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadTryExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadWhileExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadDoWhileExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadLambdaExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadBlock(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadMatchExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadLetPatternDestructor(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::Expr> LoadForInExpr(const PackageFormat::Expr& expr, int64_t exprIndex);
    OwnedPtr<AST::FuncArg> LoadFuncArg(FormattedIndex index);
    OwnedPtr<AST::MatchCase> LoadMatchCase(FormattedIndex index);
    OwnedPtr<AST::MatchCaseOther> LoadMatchCaseOther(FormattedIndex index);

    using ExprLoaderT =
        std::function<OwnedPtr<AST::Expr>(ASTLoaderImpl*, const PackageFormat::Expr& expr, int64_t exprIndex)>;
    std::unordered_map<PackageFormat::ExprKind, ExprLoaderT> exprLoaderMap{
        {PackageFormat::ExprKind_WildcardExpr, &ASTLoaderImpl::LoadWildcardExpr},
        {PackageFormat::ExprKind_PrimitiveTypeExpr, &ASTLoaderImpl::LoadPrimitiveTypeExpr},
        {PackageFormat::ExprKind_ReturnExpr, &ASTLoaderImpl::LoadReturnExpr},
        {PackageFormat::ExprKind_JumpExpr, &ASTLoaderImpl::LoadJumpExpr},
        {PackageFormat::ExprKind_MemberAccess, &ASTLoaderImpl::LoadMemberAccess},
        {PackageFormat::ExprKind_RefExpr, &ASTLoaderImpl::LoadRefExpr},
        {PackageFormat::ExprKind_CallExpr, &ASTLoaderImpl::LoadCallExpr},
        {PackageFormat::ExprKind_UnaryExpr, &ASTLoaderImpl::LoadUnaryExpr},
        {PackageFormat::ExprKind_IncOrDecExpr, &ASTLoaderImpl::LoadIncOrDecExpr},
        {PackageFormat::ExprKind_LitConstExpr, &ASTLoaderImpl::LoadLitConstExpr},
        {PackageFormat::ExprKind_BinaryExpr, &ASTLoaderImpl::LoadBinaryExpr},
        {PackageFormat::ExprKind_SubscriptExpr, &ASTLoaderImpl::LoadSubscriptExpr},
        {PackageFormat::ExprKind_AssignExpr, &ASTLoaderImpl::LoadAssignExpr},
        {PackageFormat::ExprKind_ArrayExpr, &ASTLoaderImpl::LoadArrayExpr},
        {PackageFormat::ExprKind_PointerExpr, &ASTLoaderImpl::LoadPointerExpr},
        {PackageFormat::ExprKind_TypeConvExpr, &ASTLoaderImpl::LoadTypeConvExpr},
        {PackageFormat::ExprKind_ThrowExpr, &ASTLoaderImpl::LoadThrowExpr},
        {PackageFormat::ExprKind_SpawnExpr, &ASTLoaderImpl::LoadSpawnExpr},
        {PackageFormat::ExprKind_ArrayLit, &ASTLoaderImpl::LoadArrayLit},
        {PackageFormat::ExprKind_TupleLit, &ASTLoaderImpl::LoadTupleLit},
        {PackageFormat::ExprKind_IfExpr, &ASTLoaderImpl::LoadIfExpr},
        {PackageFormat::ExprKind_TryExpr, &ASTLoaderImpl::LoadTryExpr},
        {PackageFormat::ExprKind_WhileExpr, &ASTLoaderImpl::LoadWhileExpr},
        {PackageFormat::ExprKind_DoWhileExpr, &ASTLoaderImpl::LoadDoWhileExpr},
        {PackageFormat::ExprKind_LambdaExpr, &ASTLoaderImpl::LoadLambdaExpr},
        {PackageFormat::ExprKind_Block, &ASTLoaderImpl::LoadBlock},
        {PackageFormat::ExprKind_MatchExpr, &ASTLoaderImpl::LoadMatchExpr},
        {PackageFormat::ExprKind_LetPatternDestructor, &ASTLoaderImpl::LoadLetPatternDestructor},
        {PackageFormat::ExprKind_ForInExpr, &ASTLoaderImpl::LoadForInExpr},
    };

    /** Only can be used after all exprs has been loaded. */
    inline Ptr<AST::Expr> GetExprByIndex(FormattedIndex index)
    {
        auto found = allLoadedExprs.find(index);
        return found != allLoadedExprs.end() ? found->second : nullptr;
    }

    /** Main entrance of 'LoadPattern' is only from 'LoadExpr' and 'LoadVarWithPatternDecl' . */
    OwnedPtr<AST::Pattern> LoadPattern(const PackageFormat::Pattern& pattern);
    OwnedPtr<AST::Pattern> LoadConstPattern(const PackageFormat::Pattern& pattern);
    OwnedPtr<AST::Pattern> LoadWildcardPattern(const PackageFormat::Pattern& pattern);
    OwnedPtr<AST::Pattern> LoadVarPattern(const PackageFormat::Pattern& pattern);
    OwnedPtr<AST::Pattern> LoadTuplePattern(const PackageFormat::Pattern& pattern);
    OwnedPtr<AST::Pattern> LoadTypePattern(const PackageFormat::Pattern& pattern);
    OwnedPtr<AST::Pattern> LoadEnumPattern(const PackageFormat::Pattern& pattern);
    OwnedPtr<AST::Pattern> LoadExceptTypePattern(const PackageFormat::Pattern& pattern);

    void LoadExprRefs(const PackageFormat::Expr& exprObj, AST::Expr& expr);
    void LoadSubNodeRefs(const PackageFormat::Expr& exprObj, AST::Expr& expr);
    void LoadSubNodeRefs2(const PackageFormat::Expr& exprObj, AST::Expr& expr);
    void LoadPatternRefs(const PackageFormat::Pattern& pObj, AST::Pattern& pattern);
    void LoadMatchCaseRef(FormattedIndex index, AST::MatchCase& mc, AST::Expr& selector);

    OwnedPtr<AST::Annotation> LoadAnnotation(const PackageFormat::Anno& rawAnno);
    OwnedPtr<AST::FuncArg> LoadAnnotationArg(const PackageFormat::AnnoArg& rawArg);
};
} // namespace Cangjie

#endif
