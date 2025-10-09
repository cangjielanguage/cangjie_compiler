// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the AST Writer related classes.
 */

#ifndef CANGJIE_MODULES_ASTSERIALIZATION_ASTWRITER_IMPL_H
#define CANGJIE_MODULES_ASTSERIALIZATION_ASTWRITER_IMPL_H

#include "flatbuffers/ModuleFormat_generated.h"

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Modules/ASTSerialization.h"

#include "ASTSerializeUtils.h"

namespace Cangjie {
struct DeclInfo {
    TStringOffset name;
    TStringOffset exportId;
    TStringOffset mangledName;
    TStringOffset mangledBeforeSema;
    TDeclHash hash;
    TStringOffset fullPackageName;
    TPosition begin;
    TPosition end;
    TPosition identifierPos;
    flatbuffers::Offset<flatbuffers::Vector<AST::AttrSizeType>> attributes;
    bool isConst = false;
    FormattedIndex ty;
    bool isTopLevel;
    flatbuffers::Offset<flatbuffers::Vector<TAnnoOffset>> annotations;
    flatbuffers::Offset<flatbuffers::Vector<TFullIdOffset>> dependencies;
};

struct NodeInfo {
    TPosition begin;
    TPosition end;
    FormattedIndex ty;
    Ptr<AST::Expr> mapExpr = nullptr;
    OverflowStrategy ov{OverflowStrategy::NA};
};

class ASTWriter::ASTWriterImpl {
public:
    ASTWriterImpl(DiagnosticEngine& diag, const std::string& packageDepInfo, const ExportConfig& exportCfg,
        const CjoManager& cjoManager)
        : diag(diag), config(exportCfg), packageDepInfo(packageDepInfo), cjoManager(cjoManager)
    {
    }
    ~ASTWriterImpl()
    {
    }

    void SetSerializingCommon();
    void SetAttributesIfSerializingCommonPartOfPackage(AST::Package &package);
    void PreSaveFullExportDecls(AST::Package& package);
    inline bool NeedToExportDecl(Ptr<const AST::Decl> decl);
    void DFSCollectFilesDeclarations(Ptr<AST::File> file,
        std::unordered_set<AST::File*>& alreadyVisitedFiles,
        std::vector<Ptr<const AST::Decl>>& topLevelDeclsOrdered,
        std::unordered_set<AST::Ty*>& usedTys);
    // Export external decls of a package AST to a buffer.
    void ExportAST(const AST::PackageDecl& package);
    void AST2FB(std::vector<uint8_t>& data, const AST::PackageDecl& package);

    // Save semaTypes, return TyIndex and construct savedTypeMap.
    FormattedIndex SaveType(Ptr<const AST::Ty> pType);

    bool isChirNow = false;

private:
    DiagnosticEngine& diag;
    ExportConfig config;
    bool exportFuncBody = true;
    bool serializingCommon = false;
    flatbuffers::FlatBufferBuilder builder{INITIAL_FILE_SIZE};
    std::string packageDepInfo;
    const CjoManager& cjoManager;
    /** All serialized node index in order. */
    // Save all imported packages, the vector pos is package index.
    std::vector<TStringOffset> allPackages;
    // Save all file names in the package, the vector pos is file index.
    std::vector<TStringOffset> allFiles;
    std::vector<TImportsOffset> allFileImports;
    std::vector<TFileInfoOffset> allFileInfo;
    std::vector<TTypeOffset> allTypes;
    std::vector<TDeclOffset> allDecls;
    std::vector<TExprOffset> allExprs;

    // Saved packages with indexes, which is used for indexing imported decl.
    std::unordered_map<std::string, FormattedIndex> savedPackageMap;
    // Saved files with indexes, which is used for indexing source file.
    // fileID -> package index, file index
    std::unordered_map<unsigned int, std::pair<FormattedIndex, FormattedIndex>> savedFileMap;
    // Saved types, decls and exprs with indexes, that are used for mapping in sema.
    std::unordered_map<Ptr<const AST::Ty>, FormattedIndex> savedTypeMap;
    std::unordered_map<Ptr<const AST::Decl>, FormattedIndex> savedDeclMap;
    std::unordered_map<Ptr<const AST::Expr>, FormattedIndex> savedExprMap;

    std::unordered_set<Ptr<const AST::Decl>> topLevelDecls;
    std::unordered_set<Ptr<const AST::Decl>> preSavedDecls;
    
    inline FormattedIndex PreSaveDecl(const AST::Decl& decl)
    {
        if (auto found = savedDeclMap.find(&decl); found != savedDeclMap.end()) {
            return found->second;
        }
        // NOTE: valid index start from 1.
        FormattedIndex declIndex = static_cast<FormattedIndex>(allDecls.size()) + 1;
        (void)allDecls.emplace_back(TDeclOffset());
        (void)savedDeclMap.emplace(&decl, declIndex);
        (void)preSavedDecls.emplace(&decl);
        return declIndex;
    }

    // Save imported packageNames, return FormattedIndex and construct  'savedPackageMap'.
    // NOTE: saved package indices are valid which start from 0.
    FormattedIndex SavePackageName(const std::string& fullPackageName);
    // Save file names and add to savedFileMap.
    void SaveFileInfo(const AST::File& file);
    flatbuffers::Offset<PackageFormat::Imports> SaveFileImports(const AST::File& file);
    /**
     * Save decls, return FormattedIndex and construct savedDeclMap, isTopLevel
     * Indicates saving toplevel decl.
     * NOTE: should only be called in GetDeclIndex or used when saving local decl.
     */
    FormattedIndex SaveDecl(const AST::Decl& decl, bool isTopLevel = false);
    TDeclOffset SaveVarDecl(const AST::VarDecl& varDecl, const DeclInfo& declInfo);
    // Only store local 'VarWithPatternDecl' inside expression exporting.
    TDeclOffset SaveVarWithPatternDecl(const AST::VarWithPatternDecl& vpd, const DeclInfo& declInfo);
    TDeclOffset SavePropDecl(const AST::PropDecl& propDecl, const DeclInfo& declInfo);
    TDeclOffset SaveFuncDecl(const AST::FuncDecl& funcDecl, const DeclInfo& declInfo);
    TDeclOffset SaveParamDecl(const AST::FuncParam& paramDecl, const DeclInfo& declInfo);
    TDeclOffset SaveStructDecl(const AST::StructDecl& structDecl, const DeclInfo& declInfo);
    TDeclOffset SaveEnumDecl(const AST::EnumDecl& enumDecl, const DeclInfo& declInfo);
    TDeclOffset SaveInterfaceDecl(const AST::InterfaceDecl& interfaceDecl, const DeclInfo& declInfo);
    TDeclOffset SaveClassDecl(const AST::ClassDecl& classDecl, const DeclInfo& declInfo);
    TDeclOffset SaveExtendDecl(const AST::ExtendDecl& extendDecl, const DeclInfo& declInfo);
    TDeclOffset SaveTypeAliasDecl(const AST::TypeAliasDecl& typeAliasDecl, const DeclInfo& declInfo);
    TDeclOffset SaveBuiltInDecl(const AST::BuiltInDecl& builtInDecl, const DeclInfo& declInfo);
    TDeclOffset SaveGenericParamDecl(const AST::GenericParamDecl& gpd, const DeclInfo& declInfo);
    TDeclOffset SaveUnsupportDecl(const DeclInfo& declInfo, const AST::Decl& decl);
    using DeclWriterT = std::function<TDeclOffset(const AST::Decl&, const DeclInfo&)>;
    template <typename DeclT, typename = std::enable_if_t<std::is_base_of_v<AST::Decl, DeclT>, void>>
    DeclWriterT Proxy(TDeclOffset (ASTWriterImpl::*saveFunc)(const DeclT&, const DeclInfo&))
    {
        return [this, saveFunc](const AST::Decl& decl, const DeclInfo& declInfo) {
            return (this->*saveFunc)(StaticCast<const DeclT&>(decl), declInfo);
        };
    }
    std::unordered_map<AST::ASTKind, DeclWriterT> declWriterMap {
        {AST::ASTKind::VAR_DECL, Proxy<AST::VarDecl>(&ASTWriterImpl::SaveVarDecl)},
        {AST::ASTKind::VAR_WITH_PATTERN_DECL, Proxy<AST::VarWithPatternDecl>(&ASTWriterImpl::SaveVarWithPatternDecl)},
        {AST::ASTKind::PROP_DECL, Proxy<AST::PropDecl>(&ASTWriterImpl::SavePropDecl)},
        {AST::ASTKind::FUNC_DECL, Proxy<AST::FuncDecl>(&ASTWriterImpl::SaveFuncDecl)},
        {AST::ASTKind::FUNC_PARAM, Proxy<AST::FuncParam>(&ASTWriterImpl::SaveParamDecl)},
        {AST::ASTKind::STRUCT_DECL, Proxy<AST::StructDecl>(&ASTWriterImpl::SaveStructDecl)},
        {AST::ASTKind::ENUM_DECL, Proxy<AST::EnumDecl>(&ASTWriterImpl::SaveEnumDecl)},
        {AST::ASTKind::INTERFACE_DECL, Proxy<AST::InterfaceDecl>(&ASTWriterImpl::SaveInterfaceDecl)},
        {AST::ASTKind::CLASS_DECL, Proxy<AST::ClassDecl>(&ASTWriterImpl::SaveClassDecl)},
        {AST::ASTKind::EXTEND_DECL, Proxy<AST::ExtendDecl>(&ASTWriterImpl::SaveExtendDecl)},
        {AST::ASTKind::TYPE_ALIAS_DECL, Proxy<AST::TypeAliasDecl>(&ASTWriterImpl::SaveTypeAliasDecl)},
        {AST::ASTKind::BUILTIN_DECL, Proxy<AST::BuiltInDecl>(&ASTWriterImpl::SaveBuiltInDecl)},
        {AST::ASTKind::GENERIC_PARAM_DECL, Proxy<AST::GenericParamDecl>(&ASTWriterImpl::SaveGenericParamDecl)},
    };

    bool PlannedToBeSerialized(Ptr<const AST::Decl> decl);
    std::vector<TFullIdOffset> CollectInitializationDependencies(const AST::Decl& decl,
        std::set<const AST::Decl*> visited);
    TFuncBodyOffset SaveFuncBody(const AST::FuncBody& funcBody);
    // Save generic Information
    flatbuffers::Offset<PackageFormat::Generic> SaveGeneric(const AST::Decl& decl);
    flatbuffers::Offset<PackageFormat::FuncParamList> SaveFuncParamList(const AST::FuncParamList& paramList);

    // Get package index and file index from saveFileMap.
    std::pair<FormattedIndex, FormattedIndex> GetFileIndex(unsigned int fileId);
    // Get decl index in current package from savedDeclMap, if not found, save
    // the decl node.
    FormattedIndex GetDeclIndex(Ptr<const AST::Decl> decl);
    // Get full decl index from current package or imported packages.
    TFullIdOffset GetFullDeclIndex(Ptr<const AST::Decl> decl);
    std::vector<TAnnoOffset> SaveAnnotations(const AST::Decl& decl);
    std::vector<TAnnoArgOffset> SaveAnnotationArgs(const AST::Annotation& annotation);
    flatbuffers::Offset<flatbuffers::Vector<AST::AttrSizeType>> SaveAttributes(const AST::AttributePack& attrs);
    template <class T> auto GetGenericDeclIndex(T& decl)
    {
        // NOTE: do not add partial instnatied decl's generic reference.
        if (IsFullInstantiatedDecl(decl) || (decl.IsMemberDecl() && IsFullInstantiatedDecl(*decl.outerDecl))) {
            return GetFullDeclIndex(decl.genericDecl);
        }
        return PackageFormat::CreateFullId(builder, INVALID_PACKAGE_INDEX, builder.CreateString(""));
    }
    auto GetVirtualParamLists(const AST::FuncBody& funcBody);

    template <typename T> TVectorOffset<FormattedIndex> GetBody(T& decl);
    template <typename T> auto GetVirtualInterfaces(T& decl)
    {
        // SuperInterfaceTypes.
        std::vector<FormattedIndex> superInterfaceTypes;
        for (auto& it : decl.inheritedTypes) {
            superInterfaceTypes.push_back(SaveType(it->ty));
        }
        return builder.CreateVector<FormattedIndex>(superInterfaceTypes);
    }

    NodeInfo PackNodeInfo(const AST::Node& node);
    /** NOTE: Main entrance of 'SaveExpr' is only from 'SaveFuncBody', 'SaveVarDecl' and 'SaveVarWithPatternDecl'. */
    FormattedIndex SaveExpr(const AST::Expr& expr);
    TExprOffset SaveExpression(const AST::PrimitiveTypeExpr& pte, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::WildcardExpr& we, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::ReturnExpr& re, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::JumpExpr& je, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::MemberAccess& ma, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::RefExpr& re, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::CallExpr& ce, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::UnaryExpr& ue, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::IncOrDecExpr& ide, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::LitConstExpr& lce, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::BinaryExpr& be, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::SubscriptExpr& se, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::AssignExpr& ae, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::ArrayExpr& ae, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::PointerExpr& pe, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::TypeConvExpr& tce, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::ThrowExpr& te, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::PerformExpr& pe, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::SpawnExpr& se, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::ArrayLit& al, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::TupleLit& tl, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::IfExpr& ie, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::TryExpr& te, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::WhileExpr& we, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::DoWhileExpr& dwe, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::LambdaExpr& le, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::Block& block, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::MatchExpr& me, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::LetPatternDestructor& e, const NodeInfo& info);
    TExprOffset SaveUnsupportExpr(const AST::Expr& expr, const NodeInfo& info);
    TExprOffset SaveExpression(const AST::ForInExpr& fie, const NodeInfo& info);

    using ExprWriterT = std::function<TExprOffset(const AST::Expr&, const NodeInfo&)>;
    template <typename ExprT, typename = std::enable_if_t<std::is_base_of_v<AST::Expr, ExprT>, void>>
    ExprWriterT Proxy(TExprOffset (ASTWriterImpl::*saveFunc)(const ExprT&, const NodeInfo&))
    {
        return [this, saveFunc](const AST::Expr& expr, const NodeInfo& info) {
            return (this->*saveFunc)(StaticCast<const ExprT&>(expr), info);
        };
    }
    std::unordered_map<AST::ASTKind, ExprWriterT> exprWriterMap{
        {AST::ASTKind::PRIMITIVE_TYPE_EXPR, Proxy<AST::PrimitiveTypeExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::WILDCARD_EXPR, Proxy<AST::WildcardExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::RETURN_EXPR, Proxy<AST::ReturnExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::JUMP_EXPR, Proxy<AST::JumpExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::MEMBER_ACCESS, Proxy<AST::MemberAccess>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::REF_EXPR, Proxy<AST::RefExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::CALL_EXPR, Proxy<AST::CallExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::UNARY_EXPR, Proxy<AST::UnaryExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::INC_OR_DEC_EXPR, Proxy<AST::IncOrDecExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::LIT_CONST_EXPR, Proxy<AST::LitConstExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::BINARY_EXPR, Proxy<AST::BinaryExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::SUBSCRIPT_EXPR, Proxy<AST::SubscriptExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::ASSIGN_EXPR, Proxy<AST::AssignExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::ARRAY_EXPR, Proxy<AST::ArrayExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::POINTER_EXPR, Proxy<AST::PointerExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::TYPE_CONV_EXPR, Proxy<AST::TypeConvExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::THROW_EXPR, Proxy<AST::ThrowExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::PERFORM_EXPR, Proxy<AST::PerformExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::SPAWN_EXPR, Proxy<AST::SpawnExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::ARRAY_LIT, Proxy<AST::ArrayLit>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::TUPLE_LIT, Proxy<AST::TupleLit>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::IF_EXPR, Proxy<AST::IfExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::TRY_EXPR, Proxy<AST::TryExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::WHILE_EXPR, Proxy<AST::WhileExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::DO_WHILE_EXPR, Proxy<AST::DoWhileExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::LAMBDA_EXPR, Proxy<AST::LambdaExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::BLOCK, Proxy<AST::Block>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::MATCH_EXPR, Proxy<AST::MatchExpr>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::LET_PATTERN_DESTRUCTOR, Proxy<AST::LetPatternDestructor>(&ASTWriterImpl::SaveExpression)},
        {AST::ASTKind::FOR_IN_EXPR, Proxy<AST::ForInExpr>(&ASTWriterImpl::SaveExpression)},
    };

    // Following types of ast also saved with 'PackageFormat::Expr'.
    FormattedIndex SaveFuncArg(const AST::FuncArg& arg);
    FormattedIndex SaveMatchCase(const AST::MatchCase& mc);
    FormattedIndex SaveMatchCaseOther(const AST::MatchCaseOther& mco);

    /** NOTE: Main entrance of 'SavePattern' is only from 'SaveExpr' and 'SaveVarWithPatternDecl' */
    TPatternOffset SavePattern(const AST::Pattern& pattern);
    TPatternOffset SaveConstPattern(const AST::ConstPattern& cp);
    TPatternOffset SaveWildcardPattern(const AST::WildcardPattern& wp);
    TPatternOffset SaveVarPattern(const AST::VarPattern& vp);
    TPatternOffset SaveTuplePattern(const AST::TuplePattern& tp);
    TPatternOffset SaveTypePattern(const AST::TypePattern& tp);
    TPatternOffset SaveEnumPattern(const AST::EnumPattern& ep);
    TPatternOffset SaveExceptTypePattern(const AST::ExceptTypePattern& etp);
    TPatternOffset SaveCommandTypePattern(const AST::CommandTypePattern& ctp);

    // Save sema types.
    TTypeOffset SavePointerTy(const AST::PointerTy& type);
    TTypeOffset SaveArrayTy(const AST::ArrayTy& type);
    TTypeOffset SaveVArrayTy(const AST::VArrayTy& type);
    TTypeOffset SaveTupleTy(const AST::TupleTy& type);
    TTypeOffset SaveFuncTy(const AST::FuncTy& type);
    TTypeOffset SaveGenericsTy(const AST::GenericsTy& gty);
    TTypeOffset SaveNominalTy(const AST::Ty& type);

    void SaveBasicNodeInfo(PackageFormat::ExprBuilder& dbuilder, const NodeInfo& info);
    Ptr<const AST::Expr> GetRealExpr(const AST::Expr& expr);

    /** Only can be used after @p expr has been saved. */
    inline FormattedIndex GetExprIndex(const AST::Expr& expr)
    {
        auto realExpr = GetRealExpr(expr);
        auto found = savedExprMap.find(realExpr);
        CJC_ASSERT(found != savedExprMap.end());
        return found->second;
    }

    static inline PackageFormat::TypeKind GetFormatTypeKind(AST::TypeKind kind)
    {
        auto found = TYPE_KIND_MAP.find(kind);
        return found != TYPE_KIND_MAP.end() ? found->second : PackageFormat::TypeKind_Invalid;
    }
};
} // namespace Cangjie

#endif
