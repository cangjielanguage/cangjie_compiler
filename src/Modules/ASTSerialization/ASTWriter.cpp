// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements the AST Writer related classes.
 */

#include "ASTWriterImpl.h"

#include <queue>

#include "flatbuffers/ModuleFormat_generated.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/Mangle/ASTMangler.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Mangle/CHIRMangler.h"
#include "cangjie/Modules/CjoManager.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;

namespace Cangjie {
const std::unordered_map<AST::AccessLevel, PackageFormat::AccessLevel> ACCESS_LEVEL_MAP = {
#define ACCESS_LEVEL(AST_KIND, FBS_KIND) {AST::AccessLevel::AST_KIND, PackageFormat::AccessLevel_##FBS_KIND},
#include "Mapping.h"
#undef ACCESS_LEVEL
};

const std::unordered_map<TokenKind, PackageFormat::AccessModifier> ACCESS_MODIFIER_MAP = {
#define ACCESS_MODIFIER(AST_KIND, FBS_KIND) {TokenKind::AST_KIND, PackageFormat::AccessModifier_##FBS_KIND},
#include "Mapping.h"
#undef ACCESS_MODIFIER
};

const std::unordered_map<AST::BuiltInType, PackageFormat::BuiltInType> BUILTIN_TYPE_MAP = {
#define BUILTIN_TYPE(AST_KIND, FBS_KIND) {AST::BuiltInType::AST_KIND, PackageFormat::BuiltInType_##FBS_KIND},
#include "Mapping.h"
#undef BUILTIN_TYPE
};

const std::unordered_map<OverflowStrategy, PackageFormat::OverflowPolicy> STRATEGY_MAP = {
#define OVERFLOW_STRATEGY(AST_KIND, FBS_KIND) {OverflowStrategy::AST_KIND, PackageFormat::OverflowPolicy_##FBS_KIND},
#include "Mapping.h"
#undef OVERFLOW_STRATEGY
};

const std::unordered_map<AST::TypeKind, PackageFormat::TypeKind> TYPE_KIND_MAP = {
#define TYPE_KIND(AST_KIND, FBS_KIND) {AST::TypeKind::AST_KIND, PackageFormat::TypeKind_##FBS_KIND},
#include "Mapping.h"
#undef TYPE_KIND
};

const std::unordered_map<TokenKind, PackageFormat::OperatorKind> OP_KIND_MAP = {
#define OPERATOR_KIND(AST_KIND, FBS_KIND) {TokenKind::AST_KIND, PackageFormat::OperatorKind_##FBS_KIND},
#include "Mapping.h"
#undef OPERATOR_KIND
};

const std::unordered_map<AST::CallKind, PackageFormat::CallKind> CALL_KIND_MAP = {
#define CALL_KIND(AST_KIND, FBS_KIND) {AST::CallKind::AST_KIND, PackageFormat::CallKind_##FBS_KIND},
#include "Mapping.h"
#undef CALL_KIND
};

const std::unordered_map<AST::LitConstKind, PackageFormat::LitConstKind> LIT_CONST_KIND_MAP = {
#define LIT_CONST_KIND(AST_KIND, FBS_KIND) {AST::LitConstKind::AST_KIND, PackageFormat::LitConstKind_##FBS_KIND},
#include "Mapping.h"
#undef LIT_CONST_KIND
};

const std::unordered_map<AST::StringKind, PackageFormat::StringKind> STRING_KIND_MAP = {
#define STRING_KIND(AST_KIND, FBS_KIND) {AST::StringKind::AST_KIND, PackageFormat::StringKind_##FBS_KIND},
#include "Mapping.h"
#undef STRING_KIND
};

const std::unordered_map<AST::ForInKind, PackageFormat::ForInKind> FOR_IN_KIND_MAP = {
#define FOR_IN_KIND(AST_KIND, FBS_KIND) {AST::ForInKind::AST_KIND, PackageFormat::ForInKind_##FBS_KIND},
#include "Mapping.h"
#undef FOR_IN_KIND
};
} // namespace Cangjie

namespace {
void AppendDecl(std::vector<Ptr<const Decl>>& decls, Ptr<const Decl> decl)
{
    CJC_NULLPTR_CHECK(decl);
    if (auto md = DynamicCast<MainDecl*>(decl); md && md->desugarDecl) {
        decls.push_back(md->desugarDecl.get()); // Collect desugared main decl for incremental compilation.
    } else if (auto macroDecl = DynamicCast<MacroDecl*>(decl); macroDecl && macroDecl->desugarDecl) {
        // Find references in macro scenarios for lsp.
        decls.push_back(macroDecl->desugarDecl.get());
    } else if (auto vpd = DynamicCast<VarWithPatternDecl*>(decl); vpd) {
        // A VarWithPatternDecl is viewed as a collection of VarDecls.
        Walker walker(vpd->irrefutablePattern.get(), [&decls](Ptr<Node> node) {
            if (node->astKind == ASTKind::VAR_DECL) {
                decls.push_back(StaticCast<VarDecl*>(node));
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
    } else if (auto fd = DynamicCast<FuncDecl*>(decl); fd == nullptr || fd->identifier != MAIN_INVOKE) {
        // The following two cases will be exported:
        // 1. decl is not a FuncDecl;
        // 2. decl is a FuncDecl but not a mainInvoke function.
        decls.push_back(decl);
    }
}

void CheckForConflictExportId(const std::vector<Ptr<const Decl>>& decls)
{
    std::unordered_map<std::string, Ptr<const Decl>> exportIdDeclMap;
    for (auto decl : decls) {
        // NOTE: ExtendDecl will not be reference by any other expression or type node, so it does not need exportId.
        // Only global decl and member decls that may be referenced from other package need exportId!
        if (!decl->TestAttr(Attribute::GLOBAL) || decl->astKind == ASTKind::EXTEND_DECL) {
            continue;
        }

        if (auto found = exportIdDeclMap.find(decl->exportId); found != exportIdDeclMap.end()) {
            InternalError("Found same exportID when export the package.");
            return;
        }
        exportIdDeclMap[decl->exportId] = decl;

        for (auto it : decl->GetMemberDeclPtrs()) {
            if (it->doNotExport) {
                continue;
            }
            if (auto found = exportIdDeclMap.find(it->exportId); found != exportIdDeclMap.end()) {
                InternalError("Found same exportID when export the package.");
                return;
            }
            exportIdDeclMap[it->exportId] = it;
        }
    }
}

inline bool SaveInlineLabel(const FuncDecl& funcDecl)
{
    // Instantiated decl or decl inside instantiated decl should not export inline condition.
    return !IsInDeclWithAttribute(funcDecl, Attribute::GENERIC_INSTANTIATED) && funcDecl.isInline;
}

inline bool IsExternalNorminalDecl(const Decl& decl)
{
    return decl.IsNominalDecl() && decl.linkage != Linkage::INTERNAL;
}

inline bool ShouldAlwaysExport(const Decl& decl, bool serializingCommon)
{
    // When 'exportAddition' is enabled, we have following rules:
    // 1. For BCHIR(include const evaluation) type usage, type decl must be exported.
    // Otherwise, only export external decls.
    // 2. For an extend in serializingCommon, always export it.
    return decl.IsExportedDecl() || decl.TestAttr(Attribute::IMPLICIT_USED) ||
        (decl.TestAttr(Attribute::GLOBAL) && decl.linkage != Linkage::INTERNAL) ||
        (serializingCommon && decl.astKind == ASTKind::EXTEND_DECL);
}

void MangleExportId(Package& pkg)
{
    BaseMangler mangler;
    mangler.MangleExportId(pkg);
}

void CollectBodyToQueue(const Decl& decl, std::queue<Ptr<Decl>>& queue)
{
    Ptr<Node> nodeToBeWalk = nullptr;
    if (auto vd = DynamicCast<const VarDecl*>(&decl)) {
        nodeToBeWalk = vd->initializer.get();
    } else if (auto fd = DynamicCast<const FuncDecl*>(&decl)) {
        nodeToBeWalk = fd->funcBody->body.get();
    }
    Walker(nodeToBeWalk, [&queue](auto node) {
        // Collect used global decls, NOTE: type decls are treated as must exported. Only non-types are needed here.
        if (auto target = node->GetTarget();
            target && !target->TestAttr(Attribute::IMPORTED) && target->TestAttr(Attribute::GLOBAL)) {
            queue.push(target);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void CollectFullExportParamDecl(std::vector<Ptr<Decl>>& decls, FuncDecl& fd, std::queue<Ptr<Decl>>& queue)
{
    if (!Ty::IsTyCorrect(fd.ty)) {
        return;
    }
    // When 'fd''s type is correct, following conditions must fit.
    CJC_NULLPTR_CHECK(fd.funcBody);
    CJC_ASSERT(!fd.funcBody->paramLists.empty());
    bool fullExport =
        fd.isConst || fd.isInline || fd.isFrozen || IsDefaultImplementation(fd);
    if (fullExport) {
        decls.emplace_back(&fd);
        CollectBodyToQueue(fd, queue);
    }
    for (auto& param : fd.funcBody->paramLists[0]->params) {
        // If parent function is full exported, default param decl must also exported.
        if (param->desugarDecl && (param->desugarDecl->isInline || fullExport)) {
            decls.emplace_back(param->desugarDecl.get());
            CollectBodyToQueue(*param->desugarDecl, queue);
        }
    }
}

void CollectInstantiatedTys(Ty* ty, std::unordered_set<Ty*>& tys)
{
    if (!Ty::IsTyCorrect(ty) || ty->typeArgs.empty()) {
        return;
    }
    if (ty->IsNominal()) {
        tys.emplace(ty);
    }
    for (auto it : ty->typeArgs) {
        CollectInstantiatedTys(it, tys);
    }
}

// Collect instantiated types used by type declarations' members.
std::unordered_set<Ty*> CollectInstantiations(const Decl& decl)
{
    if (decl.TestAttr(Attribute::GENERIC)) {
        return {};
    }
    std::unordered_set<Ty*> instTys;
    if (auto id = DynamicCast<const InheritableDecl*>(&decl)) {
        for (auto& it : id->inheritedTypes) {
            CollectInstantiatedTys(it->ty, instTys);
        }
        for (auto it : id->GetMemberDeclPtrs()) {
            if (!it->TestAttr(Attribute::GENERIC)) {
                CollectInstantiatedTys(it->ty, instTys);
            }
        }
    } else {
        CollectInstantiatedTys(decl.ty, instTys);
    }
    return instTys;
}

void CollectInstantiationRecursively(std::unordered_set<Ty*>& instTys)
{
    std::unordered_set<Ty*> visited;
    std::queue<Ty*> q;
    for (auto it : instTys) {
        q.push(it);
    }
    while (!q.empty()) {
        auto ty = q.front();
        q.pop();
        if (auto [_, success] = visited.emplace(ty); !success) {
            continue;
        }
        auto id = Ty::GetDeclOfTy<InheritableDecl>(ty);
        CJC_NULLPTR_CHECK(id);
        std::unordered_set<Ty*> newTys;
        for (auto& it : id->inheritedTypes) {
            CollectInstantiatedTys(it->ty, newTys);
        }
        for (auto it : id->GetMemberDeclPtrs()) {
            if (!it->TestAttr(Attribute::GENERIC)) {
                CollectInstantiatedTys(it->ty, newTys);
            }
        }
        for (auto it : newTys) {
            instTys.emplace(it);
            q.push(it);
        }
    }
}

inline Ptr<Decl> GetCallee(const CallExpr& ce)
{
    return ce.resolvedFunction ? ce.resolvedFunction : ce.baseFunc->GetTarget();
}

bool ShouldExportSource(const VarDecl& varDecl)
{
    if (!Ty::IsTyCorrect(varDecl.ty) || !varDecl.initializer || varDecl.TestAttr(Attribute::IMPORTED)) {
        return false;
    }
    if (varDecl.IsCommonMatchedWithPlatform()) {
        return false;
    }
    // If 'varDecl' is not global variable and:
    // it does not have outerDecl or it is an instance member variable in generic decl, always export initializer.
    if (!varDecl.TestAttr(Attribute::GLOBAL) && (!varDecl.outerDecl || IsInstMemberVarInGenericDecl(varDecl))) {
        // Only export complicated content when flag is enabled.
        return true;
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // For other case, we will export source code for:
    // 1) public global const variables
    // 2) public const or frozen Class/Struct(has const or frozen constructor) member variables
    if (varDecl.outerDecl && varDecl.outerDecl->TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        return false; // Do not export source code for instantiated decl's static var.
    }
    bool isExternal = varDecl.linkage != Linkage::INTERNAL || varDecl.IsExportedDecl();
    bool isGlobalVar = varDecl.TestAttr(Attribute::GLOBAL);
    // Here we assume the outer ClassDecl or StructDecl must be `public` when we check on the member variable
    bool isMemberVar = (varDecl.TestAttr(Attribute::IN_CLASSLIKE) || varDecl.TestAttr(Attribute::IN_STRUCT));
    bool isConstGlobalOrMemberVar = varDecl.isConst && (isGlobalVar || isMemberVar);
    if (isExternal && (isConstGlobalOrMemberVar || IsMemberVarShouldBeSrcExported(varDecl))) {
        auto ce = DynamicCast<CallExpr>(varDecl.initializer.get());
        auto callee = ce ? GetCallee(*ce) : nullptr;
        bool isCallInstantiatedFunc = callee && IsInDeclWithAttribute(*callee, Attribute::GENERIC_INSTANTIATED);
        if (!isCallInstantiatedFunc) {
            return true;
        }
    }
#endif
    return false;
}

void SaveDeclBasicInfo(const DeclInfo& declInfo, PackageFormat::DeclBuilder& dbuilder)
{
    dbuilder.add_identifier(declInfo.name);
    dbuilder.add_exportId(declInfo.exportId);
    dbuilder.add_mangledName(declInfo.mangledName);
    dbuilder.add_fullPkgName(declInfo.fullPackageName);
    // NOTE: Positions should be alive before 'dbuilder.Finish' is called.
    dbuilder.add_begin(&declInfo.begin);
    dbuilder.add_end(&declInfo.end);
    dbuilder.add_identifierPos(&declInfo.identifierPos);
    dbuilder.add_attributes(declInfo.attributes);
    dbuilder.add_type(declInfo.ty);
    dbuilder.add_isTopLevel(declInfo.isTopLevel);
    // For incremental compilation.
    dbuilder.add_mangledBeforeSema(declInfo.mangledBeforeSema);
    dbuilder.add_hash(&declInfo.hash);
    dbuilder.add_annotations(declInfo.annotations);
    // Only add for common part of CJMP
    if (!declInfo.dependencies.IsNull()) {
        dbuilder.add_dependencies(declInfo.dependencies);
    }
}

// Check whether the decl is exported for common part of CJMP when it is not exported for general package.
inline bool CanSkip4CJMP(const Decl& decl, bool serializingCommon)
{
    // Export global func or var, maybe used by global var initializer.
    if (serializingCommon && !decl.IsNominalDecl() && decl.TestAttr(Attribute::GLOBAL)) {
        return false;
    }
    return true;
}

/**
 * @brief Get the import package name by import spec.
 * @param importSpec The import spec.
 * @return The import package name.
*/
std::string GetImportPackageNameByImportSpec(const AST::ImportSpec& importSpec)
{
    if (importSpec.IsImportMulti()) {
        return "";
    }
    std::stringstream ss;
    for (size_t i{0}; i < importSpec.content.prefixPaths.size(); ++i) {
        ss << importSpec.content.prefixPaths[i];
        if (i == 0 && importSpec.content.hasDoubleColon) {
            ss << TOKENS[static_cast<int>(TokenKind::DOUBLE_COLON)];
        } else if (i + 1 != importSpec.content.prefixPaths.size()) {
            ss << TOKENS[static_cast<int>(TokenKind::DOT)];
        }
    }
    if (!importSpec.IsImportAll() && !importSpec.content.isDecl) {
        ss << TOKENS[static_cast<int>(TokenKind::DOT)];
        ss << importSpec.content.identifier.Val();
    }
    return ss.str();
}
} // namespace

ASTWriter::ASTWriter(DiagnosticEngine& diag, const std::string& packageDepInfo, const ExportConfig& exportCfg,
    const CjoManager& cjoManager)
{
    pImpl = std::make_unique<ASTWriter::ASTWriterImpl>(diag, packageDepInfo, exportCfg, cjoManager);
}

ASTWriter::~ASTWriter()
{
}

void ASTWriter::ExportAST(const PackageDecl& package) const
{
    CJC_NULLPTR_CHECK(pImpl);
    pImpl->ExportAST(package);
}

void ASTWriter::PreSaveFullExportDecls(Package& package) const
{
    CJC_NULLPTR_CHECK(pImpl);
    pImpl->PreSaveFullExportDecls(package);
}

template <typename T> TVectorOffset<FormattedIndex> ASTWriter::ASTWriterImpl::GetBody(T& decl)
{
    // Body.
    std::vector<FormattedIndex> body;
    // Incr compilation need load ty by cached cjo, so not only cache visible signature
    bool onlyVisibleSig = !config.exportForIncr && !config.exportContent;
    // For LSP usage, when decl is not external, ignore all members, only keep the typeDecl it self.
    if (onlyVisibleSig && decl.TestAttr(AST::Attribute::PRIVATE)) {
        return builder.CreateVector<FormattedIndex>(body);
    }
    for (auto& it : decl.GetMemberDeclPtrs()) {
        CJC_NULLPTR_CHECK(it);
        // Because member variables determine the memory layout of a type, all of its member variables should be
        // stored in cjo as long as the type is externally visible.
        const bool isInstMemberVar = it->astKind == ASTKind::VAR_DECL && !it->TestAttr(Attribute::STATIC);
        // For LSP usage, the invalid vpd will exists, we can ignore it.
        if (it->doNotExport || it->astKind == AST::ASTKind::VAR_WITH_PATTERN_DECL ||
            // For LSP usage, we can ignore invisible members.
            (onlyVisibleSig && !it->IsExportedDecl())) {
            continue;
        }
        if (decl.astKind == AST::ASTKind::EXTEND_DECL && serializingCommon) {
            body.push_back(GetDeclIndex(it));
            continue;
        }
        // Incr compilation need load ty by cached cjo, so still cache internal or inst member var decls
        if (!config.exportForIncr && !decl.TestAttr(Attribute::COMMON) && !isInstMemberVar &&
            it->linkage == Linkage::INTERNAL) {
            continue;
        }
        body.push_back(GetDeclIndex(it));
    }
    return builder.CreateVector<FormattedIndex>(body);
}

/**
 * Update AST attributes related to common/platform, e.g. set FROM_COMMON_PART.
 */
void ASTWriter::ASTWriterImpl::SetAttributesIfSerializingCommonPartOfPackage(Package &package)
{
    for (auto &file : package.files) {
        if (file->package && file->package->hasCommon) {
            serializingCommon = true;
            break;
        }
    }
    if (!serializingCommon) {
        return;
    }
    std::function<VisitAction(Ptr<Node>)> visitor = [&visitor](const Ptr<Node> &node) {
        switch (node->astKind) {
            case ASTKind::PACKAGE: {
                return VisitAction::WALK_CHILDREN;
            }
            case ASTKind::FILE: {
                auto file = StaticAs<ASTKind::FILE>(node);
                for (auto &decl : file->decls) {
                    decl->EnableAttr(Attribute::FROM_COMMON_PART);
                    Walker(decl->generic.get(), visitor).Walk();
                }
                return VisitAction::WALK_CHILDREN;
            }
            case ASTKind::INTERFACE_DECL: {
                auto id = StaticAs<ASTKind::INTERFACE_DECL>(node);
                for (auto &member : id->body->decls) {
                    member->EnableAttr(Attribute::FROM_COMMON_PART);
                    Walker(member.get(), visitor).Walk();
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::CLASS_DECL: {
                auto cd = StaticAs<ASTKind::CLASS_DECL>(node);
                for (auto &member : cd->body->decls) {
                    member->EnableAttr(Attribute::FROM_COMMON_PART);
                    Walker(member.get(), visitor).Walk();
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::STRUCT_DECL: {
                auto sd = StaticAs<ASTKind::STRUCT_DECL>(node);
                for (auto& member : sd->body->decls) {
                    member->EnableAttr(Attribute::FROM_COMMON_PART);
                    Walker(member.get(), visitor).Walk();
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::ENUM_DECL: {
                auto ed = StaticAs<ASTKind::ENUM_DECL>(node);
                for (auto& member: ed->members) {
                    member->EnableAttr(Attribute::FROM_COMMON_PART);
                    Walker(member.get(), visitor).Walk();
                }
                for (auto& constructor: ed->constructors) {
                    constructor->EnableAttr(Attribute::FROM_COMMON_PART);
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::EXTEND_DECL: {
                auto ed = StaticAs<ASTKind::EXTEND_DECL>(node);
                for (auto& member: ed->members) {
                    member->EnableAttr(Attribute::FROM_COMMON_PART);
                    Walker(member.get(), visitor).Walk();
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::FUNC_DECL: {
                auto fd = StaticAs<ASTKind::FUNC_DECL>(node);
                for (auto &param : fd->funcBody->paramLists[0]->params) {
                    if (param->desugarDecl) {
                        param->desugarDecl->EnableAttr(Attribute::FROM_COMMON_PART);
                    }
                }
                Walker(fd->funcBody->generic.get(), visitor).Walk();
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::PROP_DECL: {
                auto pd = StaticAs<ASTKind::PROP_DECL>(node);
                for (auto &getter : pd->getters) {
                    getter->EnableAttr(Attribute::FROM_COMMON_PART);
                }
                for (auto &setter : pd->setters) {
                    setter->EnableAttr(Attribute::FROM_COMMON_PART);
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::GENERIC: {
                auto generic = StaticAs<ASTKind::GENERIC>(node);
                for (auto &it : generic->typeParameters) {
                    it->EnableAttr(Attribute::FROM_COMMON_PART);
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::VAR_WITH_PATTERN_DECL: {
                auto varDecl = StaticAs<ASTKind::VAR_WITH_PATTERN_DECL>(node);
                varDecl->irrefutablePattern->EnableAttr(Attribute::FROM_COMMON_PART);
                Walker(varDecl->irrefutablePattern, visitor).Walk();
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::TUPLE_PATTERN: {
                auto tuplePattern = StaticAs<ASTKind::TUPLE_PATTERN>(node);
                for (auto& pattern : tuplePattern->patterns) {
                    Walker(pattern, visitor).Walk();
                }
                return VisitAction::SKIP_CHILDREN;
            }
            case ASTKind::VAR_PATTERN: {
                auto varPattern = StaticAs<ASTKind::VAR_PATTERN>(node);
                varPattern->varDecl->EnableAttr(Attribute::FROM_COMMON_PART);
                return VisitAction::SKIP_CHILDREN;
            }
            default:
                return VisitAction::SKIP_CHILDREN;
        }
    };

    Walker walker(&package, visitor);
    walker.Walk();
}

/**
 * Pre-save full exporting decls after sema's desugar before generic instantiation.
 * NOTE: avoid export boxed decl creation.
 */
void ASTWriter::ASTWriterImpl::PreSaveFullExportDecls(Package& package)
{
    SetAttributesIfSerializingCommonPartOfPackage(package);

    for (auto& file : package.files) {
        CJC_NULLPTR_CHECK(file.get());
        SaveFileInfo(*file);
    }
    // 1. If definition body does not need export, skip decl collection step.
    // 2. If current package is macro package, should not export any decl's definition.
    if (!config.exportContent || package.isMacroPackage) {
        return;
    }
    // Since using 'searched' to avoid duplication, we can use vector to collect all decls.
    // Visiting order of 'searchingQueue' is base on user code's calling stack, it will have stable order.
    std::vector<Ptr<Decl>> fullExportDecls;
    std::queue<Ptr<Decl>> searchingQueue;
    std::unordered_set<Ptr<Decl>> searched;
    IterateToplevelDecls(package, [&searchingQueue, this](auto& decl) {
        // External decls, type decls should always be considered as exported (For bchir's expression type usage).
        if (ShouldAlwaysExport(*decl, serializingCommon)) {
            searchingQueue.push(decl.get());
        }
    });
    while (!searchingQueue.empty()) {
        auto decl = searchingQueue.front();
        searchingQueue.pop();
        if (auto [it, success] = searched.emplace(decl); !success) {
            continue;
        }
        // Since signature of public decl must only using external type, we do not need to check type separately.
        // Collect generic, inline, default toplevel & member decls.
        if (auto fd = DynamicCast<FuncDecl*>(decl)) {
            CollectFullExportParamDecl(fullExportDecls, *fd, searchingQueue);
        } else if (auto pd = DynamicCast<PropDecl*>(decl)) {
            auto addToQueue = [&searchingQueue](auto& it) { searchingQueue.push(it.get()); };
            std::for_each(pd->getters.begin(), pd->getters.end(), addToQueue);
            std::for_each(pd->setters.begin(), pd->setters.end(), addToQueue);
        } else if (auto vd = DynamicCast<VarDecl>(decl); vd && ShouldExportSource(*vd)) {
            CollectBodyToQueue(*decl, searchingQueue);
            fullExportDecls.emplace_back(decl);
        }
        if (decl->TestAttr(Attribute::GLOBAL)) {
            topLevelDecls.emplace(decl);
        }
        if (!IsExternalNorminalDecl(*decl)) {
            continue;
        }
        for (auto& member : decl->GetMemberDecls()) {
            CJC_NULLPTR_CHECK(member);
            if (member->linkage != Linkage::INTERNAL) {
                searchingQueue.emplace(member.get());
            }
        }
    }

    if (fullExportDecls.empty()) {
        return;
    }
    MangleExportId(package);
    // Serialize decls.
    for (auto decl : fullExportDecls) {
        (void)GetDeclIndex(decl);
    }
}

inline bool ASTWriter::ASTWriterImpl::NeedToExportDecl(Ptr<const Decl> decl)
{
    if (decl->doNotExport) {
        return false;
    }
    // For incremental data or common part, export all toplevel decls.
    // For normal cases, decl which does not always need to be export or does not be collected during preExport.
    bool canSkip = !config.exportForIncr && !ShouldAlwaysExport(*decl, serializingCommon) &&
        (topLevelDecls.count(decl.get()) == 0 || decl->linkage == Linkage::INTERNAL) &&
        CanSkip4CJMP(*decl, serializingCommon);
    if (canSkip) {
        return false;
    }

    return true;
}

// Collect declarations file by file, so,
// all dependent file declarations goes before declaration of file that depend.
// NOTE: File dependency defined in specification.
void ASTWriter::ASTWriterImpl::DFSCollectFilesDeclarations(Ptr<File> file,
    std::unordered_set<File*>& alreadyVisitedFiles,
    std::vector<Ptr<const Decl>>& topLevelDeclsOrdered,
    std::unordered_set<Ty*>& usedTys)
{
    if (alreadyVisitedFiles.find(file) != alreadyVisitedFiles.end()) {
        // File was visited as dependency of another file
        return;
    }
    alreadyVisitedFiles.emplace(file);

    std::vector<Ptr<const Decl>> topLevelDeclsOfFile;
    for (auto& decl : file->decls) {
        if (!NeedToExportDecl(decl)) {
            continue;
        }
        AppendDecl(topLevelDeclsOfFile, decl);

        // Collecting decls in all dependent files
        std::unordered_set<File*> dependentFiles;
        for (auto dependency : decl->dependencies) {
            dependentFiles.emplace(dependency->curFile);
        }
        for (auto dependentFile : dependentFiles) {
            if (alreadyVisitedFiles.find(dependentFile) != alreadyVisitedFiles.end()) {
                continue;
            }
            if (dependentFile->curPackage != file->curPackage) {
                continue;
            }
            DFSCollectFilesDeclarations(dependentFile, alreadyVisitedFiles, topLevelDeclsOrdered, usedTys);
        }

        if (config.exportContent) {
            usedTys.merge(CollectInstantiations(*decl));
        }
        AddCurFile(*decl, file); // For decls added in AD stage which may not have 'curFile'.
    }

    std::move(topLevelDeclsOfFile.begin(), topLevelDeclsOfFile.end(), std::back_inserter(topLevelDeclsOrdered));
}

void ASTWriter::ASTWriterImpl::MarkImplicitExportOfImportSpec(Package& package)
{
    for (auto& file : package.files) {
        for (auto& import : file->imports) {
            if (import->IsImportMulti()) {
                continue;
            }
            auto importPkgName = GetImportPackageNameByImportSpec(*import);
            // Compile with common part, all imports should be load when compile platform part.
            if (!Utils::In(importPkgName, importedDeclPkgNames) && !import->IsReExport(package.noSubPkg) &&
                !serializingCommon) {
                import->withImplicitExport = false;
            } else {
                importedDeclPkgNames.erase(importPkgName);
            }
        }
    }
    for (auto implicitAddedPkgName : importedDeclPkgNames) {
        auto implicitAdd = CreateImportSpec(implicitAddedPkgName);
        package.files[0]->imports.emplace_back(std::move(implicitAdd));
    }
    // Clear 'importedDeclPkgNames' to reduce memory overhead.
    importedDeclPkgNames.clear();
    for (auto& file : package.files) {
        allFileImports.push_back(SaveFileImports(*file));
    }
}

// Export external decls of a Package AST to a buffer.
void ASTWriter::ASTWriterImpl::ExportAST(const PackageDecl& package)
{
    exportFuncBody = false; // Content can only be saved during 'PreSaveFullExportDecls' step.
    CJC_NULLPTR_CHECK(package.srcPackage);

    // 1. Mangle exportId.
    MangleExportId(*package.srcPackage);
    // 2. Obtain all topLevelDecl
    std::vector<Ptr<const Decl>> topLevelDeclsOrdered;
    std::unordered_set<File*> alreadyVisitedFiles;
    std::unordered_set<Ty*> usedTys;
    for (auto& file : package.srcPackage->files) {
        CJC_NULLPTR_CHECK(file.get());
        DFSCollectFilesDeclarations(file.get(), alreadyVisitedFiles, topLevelDeclsOrdered, usedTys);
    }
    if (config.exportContent) {
        // Instantiated tys are only need to be collected when 'config.exportContent' is enabled.
        CollectInstantiationRecursively(usedTys);
    }
    // If error count is not zero, we should not check the exportId.
    // NOTE: Only lsp or unittest will call current function when error count is not 0, which may contains error.
    if (diag.GetErrorCount() == 0) {
        CheckForConflictExportId(topLevelDeclsOrdered);
    }
    topLevelDecls.insert(topLevelDeclsOrdered.begin(), topLevelDeclsOrdered.end());

    // 3. Serialize decls.
    // NOTE: All the toplevel and member decls are exported now, no matter they are internal or external.
    for (auto decl : topLevelDeclsOrdered) {
        (void)GetDeclIndex(decl);
    }
    MarkImplicitExportOfImportSpec(*package.srcPackage);
}

void ASTWriter::SetSerializingCommon()
{
    pImpl->SetSerializingCommon();
}
 
void ASTWriter::ASTWriterImpl::SetSerializingCommon()
{
    serializingCommon = true;
}

void ASTWriter::AST2FB(std::vector<uint8_t>& data, const PackageDecl& package) const
{
    CJC_NULLPTR_CHECK(pImpl);
    pImpl->AST2FB(data, package);
}

void ASTWriter::ASTWriterImpl::AST2FB(std::vector<uint8_t>& data, const PackageDecl& package)
{
    auto cjcVersion = builder.CreateString(CANGJIE_VERSION);
    auto packageName = builder.CreateString(package.srcPackage->fullPackageName);
    auto dependencyInfo = builder.CreateString(packageDepInfo);
    auto vimports = builder.CreateVector<TStringOffset>(allPackages);
    auto vfiles = builder.CreateVector<TStringOffset>(allFiles);
    auto vfileImports = builder.CreateVector<TImportsOffset>(allFileImports);
    // Only for common part of CJMP
    flatbuffers::Offset<flatbuffers::Vector<TFileInfoOffset>> vfileInfo;
    if (serializingCommon) {
        vfileInfo = builder.CreateVector<TFileInfoOffset>(allFileInfo);
    }
    auto vdecls = builder.CreateVector<TDeclOffset>(allDecls);
    auto vexprs = builder.CreateVector<TExprOffset>(allExprs);
    auto vtypes = builder.CreateVector<TTypeOffset>(allTypes);
    std::vector<TStringOffset> vpackageNames;
    for (auto& depStdPkg : package.srcPackage->GetAllDependentStdPkgs()) {
        vpackageNames.emplace_back(builder.CreateString(depStdPkg));
    }
    auto vdependentStdPkgs = builder.CreateVector<TStringOffset>(vpackageNames);
    PackageFormat::PackageKind kind = PackageFormat::PackageKind_Normal;
    if (package.srcPackage->isMacroPackage) {
        kind = PackageFormat::PackageKind_Macro;
    } else if (package.srcPackage->TestAttr(AST::Attribute::TOOL_ADD)) {
        kind = PackageFormat::PackageKind_Foreign;
    } else if (package.srcPackage->TestAttr(AST::Attribute::MOCK_SUPPORTED)) {
        kind = PackageFormat::PackageKind_Mock;
    }
    auto access = ACCESS_LEVEL_MAP.at(package.srcPackage->accessible);
    bool hasModuleName = package.srcPackage->TestAttr(Attribute::TOOL_ADD) && !package.srcPackage->files.empty() &&
        !package.srcPackage->files.front()->decls.empty();
    auto moduleName =
        builder.CreateString(hasModuleName ? package.srcPackage->files.front()->decls.front()->moduleName : "");
    PackageFormat::CjoVersion cjoVersion(CJO_MAJOR_VERSION, CJO_MINOR_VERSION, CJO_PATCH_VERSION);
    auto root = PackageFormat::CreatePackage(builder, cjcVersion, &cjoVersion, packageName, dependencyInfo, vimports,
        vfiles, vfileImports, vtypes, vdecls, vexprs, INVALID_FORMAT_INDEX, kind, access, moduleName, vfileInfo,
        vdependentStdPkgs);
    FinishPackageBuffer(builder, root);
    auto size = static_cast<size_t>(builder.GetSize());
    data.resize(size);
    uint8_t* buf = builder.GetBufferPointer();
    std::copy(buf, buf + size, data.begin());
}

// Get decl index from savedDeclMap, if not found, save the Decl node.
FormattedIndex ASTWriter::ASTWriterImpl::GetDeclIndex(Ptr<const Decl> decl)
{
    if (!decl) {
        return INVALID_FORMAT_INDEX;
    }
    auto found = savedDeclMap.find(decl);
    if (found != savedDeclMap.end() && preSavedDecls.count(decl) == 0) {
        return found->second;
    } else {
        // need to check whether the decl is topLevel.
        // NOTE: desugared param decl is not real global decl.
        bool isGlobal = decl->TestAttr(Attribute::GLOBAL) && !decl->TestAttr(Attribute::HAS_INITIAL);
        bool isTopLevel = isGlobal || topLevelDecls.count(decl) == 1;
        return SaveDecl(*decl, isTopLevel);
    }
}

// Get full decl index.
// 1. if decl is packageDecl, get the index from imported map.
// 2. if decl is imported, get the index form imported map.
// 3. if decl is not imported, get the index from savedDeclMap.
TFullIdOffset ASTWriter::ASTWriterImpl::GetFullDeclIndex(Ptr<const Decl> decl)
{
    if (!decl || decl->doNotExport) {
        return PackageFormat::CreateFullId(builder, INVALID_PACKAGE_INDEX, builder.CreateString(""));
    }
    if (decl->astKind == ASTKind::PACKAGE_DECL) {
        return PackageFormat::CreateFullId(builder, PKG_REFERENCE_INDEX, builder.CreateString(decl->fullPackageName));
    } else if (decl->TestAttr(Attribute::IMPORTED)) {
        // Get full decl index from imported map.
        auto pkgIndex = static_cast<PackageIndex>(SavePackageName(decl->fullPackageName));
        importedDeclPkgNames.emplace(decl->fullPackageName);
        // NOTE: FullId using 'int32' to distinguish with current package and invalid references.
        return PackageFormat::CreateFullId(
            builder, pkgIndex, builder.CreateString(decl->exportId.empty() ? decl->identifier.Val() : decl->exportId));
    } else {
        auto index = PreSaveDecl(*decl);
        return PackageFormat::CreateFullId(builder, CURRENT_PKG_INDEX, 0, index);
    }
}

flatbuffers::Offset<PackageFormat::Imports> ASTWriter::ASTWriterImpl::SaveFileImports(const File& file)
{
    auto importSpecs = std::vector<TImportSpecOffset>();
    auto [pkgIndex, fileIndex] = GetFileIndex(file.begin.fileID);
    for (auto& importSpec : file.imports) {
        if (importSpec->IsImportMulti()) {
            continue;
        }
        auto posBegin = TPosition(fileIndex, pkgIndex, importSpec->begin.line, importSpec->begin.column, false);
        auto posEnd = TPosition(fileIndex, pkgIndex, importSpec->end.line, importSpec->end.column, false);
        auto reExport = !importSpec->modifier ? PackageFormat::AccessModifier_Private
                                              : ACCESS_MODIFIER_MAP.at(importSpec->modifier->modifier);
        importSpecs.push_back(PackageFormat::CreateImportSpec(builder, &posBegin, &posEnd,
            builder.CreateVectorOfStrings(importSpec->content.prefixPaths),
            builder.CreateString(importSpec->content.identifier.Val()),
            builder.CreateString(importSpec->content.aliasName.Val()), reExport, importSpec->content.isDecl,
            importSpec->content.hasDoubleColon, importSpec->withImplicitExport));
        if (importSpec->IsReExport()) {
            // If the import package is reExported, it should be stored as used.
            SavePackageName(cjoManager.GetPackageNameByImport(*importSpec));
        }
    }
    auto vimportSpecs = builder.CreateVector<TImportSpecOffset>(importSpecs);
    PackageFormat::ImportsBuilder importsBuilder(builder);
    importsBuilder.add_importSpecs(vimportSpecs);
    return importsBuilder.Finish();
}

// Save fileNames, and add to savedFileMap and return its index.
void ASTWriter::ASTWriterImpl::SaveFileInfo(const File& file)
{
    // if already saved, just return its index.
    auto found = savedFileMap.find(file.begin.fileID);
    if (found == savedFileMap.end()) {
        // NOTE: current package's fileIndex is vector offset plus 1.
        FormattedIndex fileIndex = static_cast<FormattedIndex>(allFiles.size()) + 1;
        if (config.needAbsPath || serializingCommon) {
            allFiles.push_back(builder.CreateString(file.filePath));
        } else {
            allFiles.push_back(builder.CreateString(
                FileUtil::JoinPath(file.curPackage->fullPackageName, FileUtil::GetFileName(file.filePath))));
        }
        savedFileMap.emplace(file.begin.fileID, std::make_pair(0, fileIndex));
        // Add additional file info for CJMP
        auto fileID = file.begin.fileID;
        auto begin = TPosition(fileIndex, 0, file.begin.line, file.begin.column, false);
        auto end = TPosition(fileIndex, 0, file.end.line, file.end.column, false);
        allFileInfo.push_back(PackageFormat::CreateFileInfo(builder, fileID, &begin, &end));
    }
}

// Save fileNames, and add to savedFileMap and return its index.
std::pair<FormattedIndex, FormattedIndex> ASTWriter::ASTWriterImpl::GetFileIndex(unsigned int fileId)
{
    auto found = savedFileMap.find(fileId);
    return found == savedFileMap.end() ? std::make_pair(INVALID_FORMAT_INDEX, INVALID_FORMAT_INDEX) : found->second;
}

// Save packageName. If already saved, return its index; if not, save it, add to savedPackageMap and return its index.
FormattedIndex ASTWriter::ASTWriterImpl::SavePackageName(const std::string& fullPackageName)
{
    CJC_ASSERT(!fullPackageName.empty());
    // If already saved, just return its index.
    auto found = savedPackageMap.find(fullPackageName);
    if (found != savedPackageMap.end()) {
        return found->second;
    }
    FormattedIndex pkgIndex = static_cast<FormattedIndex>(allPackages.size());
    allPackages.push_back(builder.CreateString(fullPackageName));
    savedPackageMap.emplace(fullPackageName, pkgIndex);
    return pkgIndex;
}

FormattedIndex ASTWriter::SaveType(Ptr<const Ty> pType) const
{
    CJC_NULLPTR_CHECK(pImpl);
    return pImpl->SaveType(pType);
}

namespace {

template <typename T>
std::optional<Ptr<const Ty>> TryGetPlatformImplementationTy(const Ptr<const Ty>& pType)
{
    auto ty = StaticCast<T*>(pType);
    if (ty->decl && ty->decl->platformImplementation) {
        return ty->decl->platformImplementation->ty;
    }

    return std::nullopt;
}

std::optional<Ptr<const Ty>> TryGetPlatformImplementationTy(const Ptr<const Ty>& pType)
{
    switch (pType->kind) {
        case TypeKind::TYPE_CLASS:
            return TryGetPlatformImplementationTy<ClassTy>(pType);
        case TypeKind::TYPE_STRUCT:
            return TryGetPlatformImplementationTy<StructTy>(pType);
        case TypeKind::TYPE_ENUM:
            return TryGetPlatformImplementationTy<EnumTy>(pType);
        default:
            return std::nullopt;
    }
}

} // namespace

// Save ty. If already saved, return its index; if not saved, save its
// member according to different ty kind, add to savedTypeMap and return
// its index.
FormattedIndex ASTWriter::ASTWriterImpl::SaveType(Ptr<const Ty> pType)
{
    // Do not use !Ty::IsTyCorrect(pType).
    if (Ty::IsInitialTy(pType)) {
        return INVALID_FORMAT_INDEX;
    }
    // if ty already saved, just return its index.
    auto found = savedTypeMap.find(pType);
    if (found != savedTypeMap.end()) {
        return found->second;
    }

    // Checking if has already saved platform decl
    auto platformImplTy = TryGetPlatformImplementationTy(pType);
    if (platformImplTy) {
        found = savedTypeMap.find(*platformImplTy);
        if (found != savedTypeMap.end()) {
            savedTypeMap.emplace(pType, found->second);
            return found->second;
        }
    }

    // Create a type index BEFORE actually creating the type object
    // this is needed to handle recursion in type details, like a class using  its own pointer for a member.
    // NOTE: valid index start from 1.
    FormattedIndex typeIndex = static_cast<FormattedIndex>(allTypes.size()) + 1;
    allTypes.emplace_back(TTypeOffset());
    savedTypeMap.emplace(pType, typeIndex);

    // Also saving type for platform decl if any
    if (platformImplTy) {
        savedTypeMap.emplace(*platformImplTy, typeIndex);
    }

    TTypeOffset typeObject;
    switch (pType->kind) {
        case TypeKind::TYPE:
        case TypeKind::TYPE_ENUM:
        case TypeKind::TYPE_STRUCT:
        case TypeKind::TYPE_CLASS:
        case TypeKind::TYPE_INTERFACE:
            typeObject = SaveNominalTy(*pType);
            break;
        case TypeKind::TYPE_POINTER:
            typeObject = SavePointerTy(*StaticCast<const PointerTy*>(pType));
            break;
        case TypeKind::TYPE_ARRAY:
            typeObject = SaveArrayTy(*StaticCast<const ArrayTy*>(pType));
            break;
        case TypeKind::TYPE_VARRAY:
            typeObject = SaveVArrayTy(*StaticCast<const VArrayTy*>(pType));
            break;
        case TypeKind::TYPE_TUPLE:
            typeObject = SaveTupleTy(*StaticCast<const TupleTy*>(pType));
            break;
        case TypeKind::TYPE_FUNC:
            typeObject = SaveFuncTy(*StaticCast<const FuncTy*>(pType));
            break;
        case TypeKind::TYPE_GENERICS:
            typeObject = SaveGenericsTy(*StaticCast<const GenericsTy*>(pType));
            break;
        default:
            // PrimitiveTy, ThisTy and CStringTy only need store type kind.
            PackageFormat::SemaTyBuilder tbuilder(builder);
            tbuilder.add_kind(GetFormatTypeKind(pType->kind));
            typeObject = tbuilder.Finish();
            break;
    }
    // Overwrite the slot with the actual type object.
    allTypes[static_cast<unsigned long>(typeIndex - 1)] = typeObject;
    return typeIndex;
}

TTypeOffset ASTWriter::ASTWriterImpl::SaveNominalTy(const Ty& type)
{
    auto declPtr = Ty::GetDeclPtrOfTy(&type);
    CJC_NULLPTR_CHECK(declPtr);
    auto fullIdDeclPtr = GetFullDeclIndex(declPtr);
    std::vector<FormattedIndex> typeArgs;
    for (auto& it : type.typeArgs) {
        typeArgs.push_back(SaveType(it));
    }
    auto vtypeArgs = builder.CreateVector<FormattedIndex>(typeArgs);
    auto info = PackageFormat::CreateCompositeTyInfo(builder, fullIdDeclPtr, Is<ClassThisTy, Ty>(type));

    PackageFormat::SemaTyBuilder tbuilder(builder);
    tbuilder.add_kind(GetFormatTypeKind(type.kind));
    tbuilder.add_typeArgs(vtypeArgs);
    tbuilder.add_info_type(PackageFormat::SemaTyInfo_CompositeTyInfo);
    tbuilder.add_info(info.Union());
    return tbuilder.Finish();
}

TTypeOffset ASTWriter::ASTWriterImpl::SaveGenericsTy(const GenericsTy& gty)
{
    std::vector<FormattedIndex> uppers;
    std::set<Ptr<Ty>, CmpTyByName> sortedUppers; // Sort for bep.
    sortedUppers.insert(gty.upperBounds.begin(), gty.upperBounds.end());
    for (auto& it : sortedUppers) {
        uppers.push_back(SaveType(it));
    }
    auto vUppers = builder.CreateVector<FormattedIndex>(uppers);
    auto decl = GetFullDeclIndex(gty.decl);
    auto info = PackageFormat::CreateGenericTyInfo(builder, decl, vUppers);
    PackageFormat::SemaTyBuilder tbuilder(builder);
    tbuilder.add_kind(GetFormatTypeKind(gty.kind));
    tbuilder.add_info_type(PackageFormat::SemaTyInfo_GenericTyInfo);
    tbuilder.add_info(info.Union());
    return tbuilder.Finish();
}

TTypeOffset ASTWriter::ASTWriterImpl::SaveFuncTy(const FuncTy& type)
{
    std::vector<FormattedIndex> paramTypes;
    for (auto& it : type.paramTys) {
        paramTypes.push_back(SaveType(it));
    }
    auto vParamTypes = builder.CreateVector<FormattedIndex>(paramTypes);
    // SaveType has side effect (it allocates an offset for the type)
    // DO NOT put it in another expression
    FormattedIndex retType = SaveType(type.retTy);
    auto info = PackageFormat::CreateFuncTyInfo(builder, retType, type.isC, type.hasVariableLenArg);
    PackageFormat::SemaTyBuilder tbuilder(builder);
    tbuilder.add_kind(GetFormatTypeKind(type.kind));
    tbuilder.add_typeArgs(vParamTypes);
    tbuilder.add_info_type(PackageFormat::SemaTyInfo_FuncTyInfo);
    tbuilder.add_info(info.Union());
    // AST node's ty will never be closureTy.
    return tbuilder.Finish();
}

TTypeOffset ASTWriter::ASTWriterImpl::SaveTupleTy(const TupleTy& type)
{
    std::vector<FormattedIndex> paramTypes;
    for (auto& it : type.typeArgs) {
        paramTypes.push_back(SaveType(it));
    }
    auto vTypeArgs = builder.CreateVector<FormattedIndex>(paramTypes);
    PackageFormat::SemaTyBuilder tbuilder(builder);
    tbuilder.add_kind(GetFormatTypeKind(type.kind));
    tbuilder.add_typeArgs(vTypeArgs);
    // AST node's ty will never be closureTy.
    return tbuilder.Finish();
}

TTypeOffset ASTWriter::ASTWriterImpl::SaveArrayTy(const ArrayTy& type)
{
    CJC_ASSERT(!type.typeArgs.empty());
    FormattedIndex elemTy = SaveType(type.typeArgs[0]);
    auto vTypeArgs = builder.CreateVector({elemTy});
    auto info = PackageFormat::CreateArrayTyInfo(builder, type.dims);
    PackageFormat::SemaTyBuilder tbuilder(builder);
    tbuilder.add_kind(GetFormatTypeKind(type.kind));
    tbuilder.add_typeArgs(vTypeArgs);
    tbuilder.add_info_type(PackageFormat::SemaTyInfo_ArrayTyInfo);
    tbuilder.add_info(info.Union());
    return tbuilder.Finish();
}

TTypeOffset ASTWriter::ASTWriterImpl::SaveVArrayTy(const VArrayTy& type)
{
    CJC_ASSERT(!type.typeArgs.empty());
    FormattedIndex elemTy = SaveType(type.typeArgs[0]);
    auto vTypeArgs = builder.CreateVector({elemTy});
    auto info = PackageFormat::CreateArrayTyInfo(builder, type.size);
    PackageFormat::SemaTyBuilder tbuilder(builder);
    tbuilder.add_kind(GetFormatTypeKind(type.kind));
    tbuilder.add_typeArgs(vTypeArgs);
    tbuilder.add_info_type(PackageFormat::SemaTyInfo_ArrayTyInfo);
    tbuilder.add_info(info.Union());
    return tbuilder.Finish();
}

TTypeOffset ASTWriter::ASTWriterImpl::SavePointerTy(const PointerTy& type)
{
    CJC_ASSERT(!type.typeArgs.empty());
    FormattedIndex elemTy = SaveType(type.typeArgs[0]);
    auto vTypeArgs = builder.CreateVector({elemTy});
    PackageFormat::SemaTyBuilder tbuilder(builder);
    tbuilder.add_kind(GetFormatTypeKind(type.kind));
    tbuilder.add_typeArgs(vTypeArgs);
    return tbuilder.Finish();
}

// Save FuncParamList.
flatbuffers::Offset<PackageFormat::FuncParamList> ASTWriter::ASTWriterImpl::SaveFuncParamList(
    const FuncParamList& paramList)
{
    std::vector<FormattedIndex> params;
    std::vector<FormattedIndex> desugars;
    for (auto& it : paramList.params) {
        params.push_back(GetDeclIndex(it.get()));
        desugars.push_back(GetDeclIndex(it->desugarDecl.get()));
    }
    auto vParamList = builder.CreateVector<FormattedIndex>(params);
    auto vDesugarList = builder.CreateVector<FormattedIndex>(desugars);
    PackageFormat::FuncParamListBuilder dbuilder(builder);
    dbuilder.add_params(vParamList);
    dbuilder.add_desugars(vDesugarList);
    return dbuilder.Finish();
}

// Save Generic.
flatbuffers::Offset<PackageFormat::Generic> ASTWriter::ASTWriterImpl::SaveGeneric(const Decl& decl)
{
    // 1. Enum constructor and other constructor must not have generic.
    // 2. Partial instantiated decl's 'generic'can be ignored. See 'SaveDecl'
    bool ignored = decl.TestAnyAttr(Attribute::ENUM_CONSTRUCTOR, Attribute::CONSTRUCTOR) ||
        decl.TestAttr(Attribute::GENERIC_INSTANTIATED, Attribute::GENERIC);
    auto generic = ignored ? nullptr : decl.GetGeneric();
    if (generic == nullptr) {
        return {};
    }
    // Export generic type parameters if existing.
    std::vector<FormattedIndex> typeParameters;
    std::vector<flatbuffers::Offset<PackageFormat::Constraint>> constraints;
    for (auto& gpd : generic->typeParameters) {
        auto gty = DynamicCast<GenericsTy*>(gpd->ty);
        // When 'gty' is valid, and its parent decl is a local function, using generic type decl,
        // otherwise keep 'gpd' itself.
        CJC_NULLPTR_CHECK(gpd->outerDecl);
        typeParameters.push_back(GetDeclIndex(gty && !IsGlobalOrMember(*gpd->outerDecl) ? gty->decl : gpd.get()));
    }
    for (auto& constraint : generic->genericConstraints) {
        CJC_NULLPTR_CHECK(constraint);
        std::vector<FormattedIndex> uppers;
        for (auto& upper : constraint->upperBounds) {
            CJC_NULLPTR_CHECK(upper);
            uppers.emplace_back(SaveType(upper->ty));
        }
        constraint->ty = constraint->type->ty; // Sync ty to re-use 'PackNodeInfo'.
        auto info = PackNodeInfo(*constraint);
        auto vUppers = builder.CreateVector<FormattedIndex>(uppers);
        constraints.emplace_back(PackageFormat::CreateConstraint(builder, &info.begin, &info.end, info.ty, vUppers));
    }
    auto vTypeParameters = builder.CreateVector<FormattedIndex>(typeParameters);
    auto vConstraints = builder.CreateVector<flatbuffers::Offset<PackageFormat::Constraint>>(constraints);
    PackageFormat::GenericBuilder dbuilder(builder);
    dbuilder.add_typeParameters(vTypeParameters);
    dbuilder.add_constraints(vConstraints);
    return dbuilder.Finish();
}

void ASTWriter::SetIsChirNow(bool isChirNow)
{
    CJC_NULLPTR_CHECK(pImpl);
    pImpl->isChirNow = isChirNow;
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveParamDecl(const FuncParam& paramDecl, const DeclInfo& declInfo)
{
    FormattedIndex defaultVal = INVALID_FORMAT_INDEX;
    if (paramDecl.assignment &&
        (paramDecl.TestAttr(Attribute::TOOL_ADD) || paramDecl.assignment->isConst ||
            Is<LitConstExpr>(paramDecl.assignment.get()))) {
        defaultVal = SaveExpr(*paramDecl.assignment);
    }
    auto info = PackageFormat::CreateParamInfo(builder, paramDecl.isNamedParam, paramDecl.isMemberParam, defaultVal);
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_FuncParam);
    dbuilder.add_info_type(PackageFormat::DeclInfo_ParamInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SavePropDecl(const PropDecl& propDecl, const DeclInfo& declInfo)
{
    flatbuffers::Offset<PackageFormat::PropInfo> info;
    if (!config.compileCjd) {
        // Global variable initializers should be dropped too, they are
        // implemented in the global constructor.
        // add setters and getters
        std::vector<FormattedIndex> setBody;
        for (auto& it : propDecl.setters) {
            setBody.push_back(GetDeclIndex(it.get()));
        }
        auto setters = builder.CreateVector<FormattedIndex>(setBody);

        std::vector<FormattedIndex> getBody;
        for (auto& it : propDecl.getters) {
            getBody.push_back(GetDeclIndex(it.get()));
        }
        auto getters = builder.CreateVector<FormattedIndex>(getBody);
        info = PackageFormat::CreatePropInfo(builder, propDecl.isConst, propDecl.isVar, setters, getters);
    } else {
        info = PackageFormat::CreatePropInfo(builder, propDecl.isConst, propDecl.isVar);
    }
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_PropDecl);
    dbuilder.add_info_type(PackageFormat::DeclInfo_PropInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveVarDecl(const VarDecl& varDecl, const DeclInfo& declInfo)
{
    bool exportSourceCode = config.exportContent && ShouldExportSource(varDecl);
    FormattedIndex initializer = exportSourceCode ? SaveExpr(*varDecl.initializer) : INVALID_FORMAT_INDEX;
    auto info =
        PackageFormat::CreateVarInfo(builder, varDecl.isVar, varDecl.isConst, varDecl.isMemberParam, initializer);
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_VarDecl);
    dbuilder.add_info_type(PackageFormat::DeclInfo_VarInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveVarWithPatternDecl(const VarWithPatternDecl& vpd, const DeclInfo& declInfo)
{
    // Save 'VarWithPatternDecl' is always local variable.
    CJC_NULLPTR_CHECK(vpd.initializer);
    FormattedIndex initializer = SaveExpr(*vpd.initializer);
    auto pIdx = SavePattern(*vpd.irrefutablePattern);
    auto info = PackageFormat::CreateVarWithPatternInfo(builder, vpd.isVar, vpd.isConst, pIdx, initializer);
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_VarWithPatternDecl);
    dbuilder.add_info_type(PackageFormat::DeclInfo_VarWithPatternInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

auto ASTWriter::ASTWriterImpl::GetVirtualParamLists(const FuncBody& funcBody)
{
    // FuncParamList.
    std::vector<flatbuffers::Offset<PackageFormat::FuncParamList>> paramLists;
    for (auto& it : funcBody.paramLists) {
        paramLists.push_back(SaveFuncParamList(*it.get()));
    }
    return builder.CreateVector<flatbuffers::Offset<PackageFormat::FuncParamList>>(paramLists);
}

TFuncBodyOffset ASTWriter::ASTWriterImpl::SaveFuncBody(const FuncBody& funcBody)
{
    auto fd = funcBody.funcDecl;
    // Partial instantiated decl's can be ignored. Reason see 'SaveDecl' and 'UNREACHABLE' was set for export.
    if (fd && fd->TestAttr(Attribute::GENERIC_INSTANTIATED, Attribute::GENERIC)) {
        std::vector<flatbuffers::Offset<PackageFormat::FuncParamList>> paramLists;
        auto dummyList = builder.CreateVector<flatbuffers::Offset<PackageFormat::FuncParamList>>(paramLists);
        return PackageFormat::CreateFuncBody(builder, dummyList, INVALID_FORMAT_INDEX, INVALID_FORMAT_INDEX, false, 0);
    }
    auto vparamLists = GetVirtualParamLists(funcBody);
    FormattedIndex retType = funcBody.retType ? SaveType(funcBody.retType->ty) : INVALID_FORMAT_INDEX;
    // The frozen attribute is passed to a nested function.
    if (fd && fd->outerDecl && fd->outerDecl->astKind == ASTKind::FUNC_DECL) {
        auto outerFunc = StaticCast<FuncDecl>(fd->outerDecl);
        fd->isFrozen = outerFunc->isFrozen;
    }
    // If funcBody is valid, and for following situations need to export func body content:
    // 1. funcBody of LamdaExpr or inside other FuncDecl/VarDecl;
    // 2. funcBody of @Frozen modified generic funcDecl;
    // 3. funcbody of inline function;
    // 4. funcBody of constant decl;
    // 5. funcBody of default implementation which is defined in interface.
    // NOTE: desugared param function has same 'outerDecl' and 'GLOBAL' attribute will its owner function.
    bool shouldExportBody = config.exportContent && exportFuncBody && (!fd || CanBeSrcExported(*fd));
    bool validBody = shouldExportBody && Ty::IsTyCorrect(funcBody.ty) && funcBody.body;
    auto bodyIdx = validBody ? SaveExpr(*funcBody.body) : INVALID_FORMAT_INDEX;
    // CaptureKind is need if the 'funcBody' is exported.
    uint8_t kind = validBody ? static_cast<uint8_t>(funcBody.captureKind) : 0;
    return PackageFormat::CreateFuncBody(builder, vparamLists, retType, bodyIdx, false, kind);
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveFuncDecl(const FuncDecl& funcDecl, const DeclInfo& declInfo)
{
    auto isInline = SaveInlineLabel(funcDecl);
    CJC_NULLPTR_CHECK(funcDecl.funcBody);
    auto body = SaveFuncBody(*funcDecl.funcBody);
    auto generic = SaveGeneric(funcDecl);
    auto genericDeclIndex = GetGenericDeclIndex(funcDecl);
    auto info = PackageFormat::CreateFuncInfo(builder, body, STRATEGY_MAP.at(funcDecl.overflowStrategy),
        OP_KIND_MAP.at(funcDecl.op), 0, funcDecl.isConst, isInline, funcDecl.isFastNative);

    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_FuncDecl);
    if (!generic.IsNull()) {
        dbuilder.add_generic(generic);
    }
    dbuilder.add_genericDecl(genericDeclIndex);

    dbuilder.add_info_type(PackageFormat::DeclInfo_FuncInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveStructDecl(const StructDecl& structDecl, const DeclInfo& declInfo)
{
    auto structBody = GetBody(structDecl);
    auto vInterfaceTypes = GetVirtualInterfaces(structDecl);
    auto generic = SaveGeneric(structDecl);
    auto genericDeclIndex = GetGenericDeclIndex(structDecl);
    auto info = PackageFormat::CreateStructInfo(builder, vInterfaceTypes, structBody, 0);
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_StructDecl);
    if (!generic.IsNull()) {
        dbuilder.add_generic(generic);
    }
    dbuilder.add_genericDecl(genericDeclIndex);
    dbuilder.add_info_type(PackageFormat::DeclInfo_StructInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveEnumDecl(const EnumDecl& enumDecl, const DeclInfo& declInfo)
{
    auto enumBody = GetBody(enumDecl);
    auto vinterfaceTypes = GetVirtualInterfaces(enumDecl);
    auto generic = SaveGeneric(enumDecl);
    auto genericDeclIndex = GetGenericDeclIndex(enumDecl);
    auto& pos = enumDecl.ellipsisPos;
    auto [pkgIndex, fileIndex] = GetFileIndex(pos.fileID);
    TPosition position{fileIndex, pkgIndex, pos.line, pos.column, pos.GetStatus() == PositionStatus::IGNORE};
    auto info = PackageFormat::CreateEnumInfo(
        builder, vinterfaceTypes, enumBody, 0, enumDecl.hasArguments, enumDecl.hasEllipsis, &position);
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_EnumDecl);
    if (!generic.IsNull()) {
        dbuilder.add_generic(generic);
    }
    dbuilder.add_genericDecl(genericDeclIndex);
    dbuilder.add_info_type(PackageFormat::DeclInfo_EnumInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveInterfaceDecl(const InterfaceDecl& interfaceDecl, const DeclInfo& declInfo)
{
    auto vinterfaceBody = GetBody(interfaceDecl);
    auto vinterfaceTypes = GetVirtualInterfaces(interfaceDecl);
    auto generic = SaveGeneric(interfaceDecl);
    auto genericDeclIndex = GetGenericDeclIndex(interfaceDecl);
    auto info = PackageFormat::CreateInterfaceInfo(builder, vinterfaceTypes, vinterfaceBody);
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_InterfaceDecl);
    if (!generic.IsNull()) {
        dbuilder.add_generic(generic);
    }
    dbuilder.add_genericDecl(genericDeclIndex);
    dbuilder.add_info_type(PackageFormat::DeclInfo_InterfaceInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

static void WriteAnnoTargets(PackageFormat::ClassInfoBuilder& builder, AnnotationTargetT targets)
{
    // these manglings are for backward compatibility of .fbs
    constexpr AnnotationTargetT allTargetsEnabled{0xffff};
    constexpr unsigned char allTargetsSerialised{0xff};
    if (targets == allTargetsEnabled) {
        builder.add_annoTargets(allTargetsSerialised);
        return;
    }
    constexpr AnnotationTargetT anno2Mask{0x180};
    constexpr AnnotationTargetT anno1Mask{0x7f};
    if (auto anno2 = static_cast<unsigned>(anno2Mask & targets); anno2 != 0) {
        builder.add_annoTargets2(static_cast<unsigned char>(anno2 >> (CHAR_BIT - 1)));
        builder.add_annoTargets(static_cast<unsigned char>(anno1Mask & targets));
    } else {
        builder.add_annoTargets(static_cast<unsigned char>(targets));
    }
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveClassDecl(const ClassDecl& classDecl, const DeclInfo& declInfo)
{
    auto vclassBody = GetBody(classDecl);
    auto vinheritedTypes = GetVirtualInterfaces(classDecl);
    auto generic = SaveGeneric(classDecl);
    auto genericDeclIndex = GetGenericDeclIndex(classDecl);
    PackageFormat::ClassInfoBuilder ibuilder(builder);
    ibuilder.add_body(vclassBody);
    ibuilder.add_inheritedTypes(vinheritedTypes);
    if (classDecl.TestAttr(Attribute::IS_ANNOTATION)) {
        ibuilder.add_isAnno(true);
        for (auto& ann : classDecl.annotations) {
            CJC_NULLPTR_CHECK(ann);
            if (ann->kind == AnnotationKind::ANNOTATION) {
                WriteAnnoTargets(ibuilder, ann->target);
                ibuilder.add_runtimeVisible(ann->runtimeVisible);
            }
        }
    }
    auto info = ibuilder.Finish();
    PackageFormat::DeclBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::DeclKind_ClassDecl);
    SaveDeclBasicInfo(declInfo, dbuilder);
    if (!generic.IsNull()) {
        dbuilder.add_generic(generic);
    }
    dbuilder.add_genericDecl(genericDeclIndex);
    dbuilder.add_info_type(PackageFormat::DeclInfo_ClassInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveExtendDecl(const ExtendDecl& extendDecl, const DeclInfo& declInfo)
{
    auto vextendInterfaceTypes = GetVirtualInterfaces(extendDecl);
    auto vextendMembers = GetBody(extendDecl);
    auto generic = SaveGeneric(extendDecl);
    auto genericDeclIndex = GetGenericDeclIndex(extendDecl);
    auto info = PackageFormat::CreateExtendInfo(builder, vextendInterfaceTypes, vextendMembers);
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_ExtendDecl);
    if (!generic.IsNull()) {
        dbuilder.add_generic(generic);
    }
    dbuilder.add_genericDecl(genericDeclIndex);
    dbuilder.add_info_type(PackageFormat::DeclInfo_ExtendInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveTypeAliasDecl(const TypeAliasDecl& typeAliasDecl, const DeclInfo& declInfo)
{
    FormattedIndex aliasedTy = SaveType(typeAliasDecl.type->ty);
    auto generic = SaveGeneric(typeAliasDecl);
    auto info = PackageFormat::CreateAliasInfo(builder, aliasedTy);
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_TypeAliasDecl);
    if (!generic.IsNull()) {
        dbuilder.add_generic(generic);
    }
    dbuilder.add_info_type(PackageFormat::DeclInfo_AliasInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveBuiltInDecl(const BuiltInDecl& builtInDecl, const DeclInfo& declInfo)
{
    auto generic = SaveGeneric(builtInDecl);
    auto info = PackageFormat::CreateBuiltInInfo(builder, BUILTIN_TYPE_MAP.at(builtInDecl.type));
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_BuiltInDecl);
    if (!generic.IsNull()) {
        dbuilder.add_generic(generic);
    }
    dbuilder.add_info_type(PackageFormat::DeclInfo_BuiltInInfo);
    dbuilder.add_info(info.Union());
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveGenericParamDecl(const GenericParamDecl& /* gpd */, const DeclInfo& declInfo)
{
    // In most case, user defined 'GenericParamDecl' referenced in current package,
    // except for interface's generic type and its generic default function,
    // their 'GenericParamDecl' may be reference by other package.
    PackageFormat::DeclBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::DeclKind_GenericParamDecl);
    SaveDeclBasicInfo(declInfo, dbuilder);
    return dbuilder.Finish();
}

TDeclOffset ASTWriter::ASTWriterImpl::SaveUnsupportDecl(const DeclInfo& declInfo, const Decl& decl)
{
    (void)diag.DiagnoseRefactor(
        DiagKindRefactor::package_unsupport_save, decl, "declaration", std::string(typeid(decl).name()));
    PackageFormat::DeclBuilder dbuilder(builder);
    SaveDeclBasicInfo(declInfo, dbuilder);
    dbuilder.add_kind(PackageFormat::DeclKind_InvalidDecl);
    return dbuilder.Finish();
}

flatbuffers::Offset<flatbuffers::Vector<AttrSizeType>> ASTWriter::ASTWriterImpl::SaveAttributes(
    const AttributePack& attrs
)
{
    std::vector<AttrSizeType> attrVec;
    for (auto it : attrs.GetRawAttrs()) {
        (void)attrVec.emplace_back(static_cast<AttrSizeType>(it.to_ullong()));
    }

    return builder.CreateVector<AttrSizeType>(attrVec);
}

bool ASTWriter::ASTWriterImpl::PlannedToBeSerialized(Ptr<const Decl> decl)
{
    return savedDeclMap.find(decl) != savedDeclMap.end();
}

/// Collect dependencies on which the current `decl` depends.
/// In case if declaration is not exported, its transitive dependencies will be saved.
std::vector<TFullIdOffset> ASTWriter::ASTWriterImpl::CollectInitializationDependencies(
    const Decl& decl, std::set<const Decl*> visited)
{
    bool wasVisited = visited.find(&decl) != visited.end();
    if (wasVisited) {
        return {};
    }
    visited.insert(&decl);

    std::vector<TFullIdOffset> dependencies;

    std::vector<TFullIdOffset> dependenciesVector;
    for (auto dependency : decl.dependencies) {
        if (PlannedToBeSerialized(dependency)) {
            TFullIdOffset depFullId = GetFullDeclIndex(dependency);
            dependencies.push_back(depFullId);
        } else {
            for (auto transitiveFullId : CollectInitializationDependencies(*dependency, visited)) {
                dependencies.push_back(transitiveFullId);
            }
        }
    }
    
    return dependencies;
}

// Save decl, add to savedDeclMap and return its index.
// It should be noted that: first saved all decls, then save types, to avoid
// searching unsaved decls in SaveType.
FormattedIndex ASTWriter::ASTWriterImpl::SaveDecl(const Decl& decl, bool isTopLevel)
{
    FormattedIndex declIndex = PreSaveDecl(decl);
    preSavedDecls.erase(&decl);
    // Names: identifier, exportId, packageName.
    auto name = builder.CreateSharedString(decl.identifier.Val());
    auto exportId = builder.CreateSharedString(decl.exportId);
    // The Decl meet one of the following conditions does not need to export 'mangledName':
    // 1. decl in generic decls.
    // 2. decl is GenericParamDecl, propDecl, typeAliasDecl，extendDecl
    // 3. deck is instance member variable.
    bool ignoreMangledName = IsInDeclWithAttribute(decl, Attribute::GENERIC) ||
        Utils::In(decl.astKind,
            {ASTKind::GENERIC_PARAM_DECL, ASTKind::PROP_DECL, ASTKind::TYPE_ALIAS_DECL, ASTKind::EXTEND_DECL}) ||
        (decl.astKind == ASTKind::VAR_DECL && !decl.TestAnyAttr(Attribute::STATIC, Attribute::GLOBAL));
    bool isInExtendToExportForTest = decl.outerDecl && Is<ExtendDecl>(decl.outerDecl) &&
        !decl.outerDecl->TestAttr(Attribute::IMPORTED) && !decl.outerDecl->TestAttr(Attribute::COMPILER_ADD) &&
        !decl.TestAttr(Attribute::PRIVATE);
    auto mangledName = builder.CreateSharedString(
        ignoreMangledName && (!config.exportForTest || !isInExtendToExportForTest) ? "" : decl.mangledName);
    auto rawMangleName = !config.exportForIncr && decl.TestAttr(Attribute::DEFAULT) && decl.rawMangleName.empty()
        ? builder.CreateSharedString(ASTMangler(decl.fullPackageName).Mangle(decl))
        : builder.CreateSharedString(decl.rawMangleName);
    auto fullPackageName = builder.CreateSharedString(decl.fullPackageName);
    // NOTE: Type of partial instantiated member function can be ignored.
    //       Since this kind of member function will never be referenced during 'Sema' step
    //       and will be replaced during instantiation step, we ignore the ty of this kind of decl.
    auto attrs = decl.GetAttrs();
    if (decl.TestAttr(Attribute::GENERIC_INSTANTIATED, Attribute::GENERIC)) {
        attrs.SetAttr(Attribute::UNREACHABLE, true); // Set 'UNREACHABLE' for export.
    }

    if (auto varDecl = DynamicCast<VarDecl>(&decl)) {
        if (varDecl->TestAttr(Attribute::FROM_COMMON_PART) && varDecl->outerDecl &&
            varDecl->outerDecl->TestAttr(Attribute::PLATFORM)) {
            attrs.SetAttr(Attribute::COMMON, false);
            attrs.SetAttr(Attribute::FROM_COMMON_PART, false);
        }
        if (varDecl->outerDecl && varDecl->outerDecl->TestAttr(Attribute::COMMON)) {
            bool hasInitializer = varDecl->initializer;
            attrs.SetAttr(Attribute::INITIALIZED, hasInitializer);
            if (varDecl->TestAttr(Attribute::STATIC) && varDecl->TestAttr(Attribute::INITIALIZED)) {
                attrs.SetAttr(Attribute::INITIALIZED, true);
            }
        }

        if (varDecl->TestAttr(Attribute::PLATFORM)) {
            attrs.SetAttr(Attribute::PLATFORM, false);
        }
    }
    auto type = attrs.TestAttr(Attribute::UNREACHABLE) ? INVALID_FORMAT_INDEX : SaveType(decl.ty);
    auto begin = decl.GetBegin();
    auto end = decl.GetEnd();
    auto [pkgIndex, fileIndex] = GetFileIndex(begin.fileID);
    // When fileIndex is not in savedMap, it means the fileID is for imported package,
    // and current decl may be or is inside cloned default member decl.
    // NOTE: cloned default member decl should keep origin 'curFile'.
    if (fileIndex == INVALID_FORMAT_INDEX) {
        CJC_ASSERT(decl.curFile && decl.curFile->curPackage);
        // New non-current package's file must be first found from decl, then expression.
        fileIndex = GetFileIndexInPkg(decl);
        // NOTE: Other package's 'pkgIndex' is vector offset plus 1.
        pkgIndex = SavePackageName(decl.curFile->curPackage->fullPackageName) + 1;
        // Update file map cache.
        savedFileMap.emplace(begin.fileID, std::make_pair(pkgIndex, fileIndex));
    }
    TPosition posBegin(fileIndex, pkgIndex, begin.line, begin.column, begin.GetStatus() == PositionStatus::IGNORE);
    TPosition posEnd(fileIndex, pkgIndex, end.line, end.column, end.GetStatus() == PositionStatus::IGNORE);
    TPosition identifierPos(fileIndex, pkgIndex, decl.GetIdentifierPos().line, decl.GetIdentifierPos().column, false);
    TDeclHash declHash(decl.hash.instVar, decl.hash.virt, decl.hash.sig, decl.hash.srcUse, decl.hash.bodyHash);
    auto attributes = SaveAttributes(attrs);
    auto isConst = decl.IsConst();
    auto annotations = builder.CreateVector<TAnnoOffset>(SaveAnnotations(decl));
    // The dependencies default is null for compatibility, only may have value for common part of CJMP.
    flatbuffers::Offset<flatbuffers::Vector<TFullIdOffset>> dependencies;
    if (serializingCommon) {
        dependencies = builder.CreateVector<TFullIdOffset>(CollectInitializationDependencies(decl, {}));
    }
    DeclInfo declInfo{name, exportId, mangledName, rawMangleName, declHash, fullPackageName, posBegin, posEnd,
        identifierPos, attributes, isConst, type, isTopLevel, annotations, dependencies};
    TDeclOffset offset;
    auto foundWriter = declWriterMap.find(decl.astKind);
    if (foundWriter != declWriterMap.end()) {
        offset = foundWriter->second(decl, declInfo);
    } else {
        offset = SaveUnsupportDecl(declInfo, decl);
    }
    // Overwrite the slot with the actual decl.
    allDecls[static_cast<unsigned long>(declIndex - 1)] = offset;
    return declIndex;
}

// `public let x!: Bool = false`
// Affect both **member variable** and **primary constructor parameter**.
// But separate records will be created for them.
// Means there is no global `allSavedAnnotations` map.
std::vector<TAnnoOffset> ASTWriter::ASTWriterImpl::SaveAnnotations(const Decl& decl)
{
    std::vector<TAnnoOffset> annotations;

    for (auto& annotation : std::as_const(decl.annotations)) {
        if (annotation->kind == AST::AnnotationKind::DEPRECATED) {
            auto args = builder.CreateVector<TAnnoArgOffset>(SaveAnnotationArgs(*annotation));

            auto serialized = PackageFormat::CreateAnno(
                builder, PackageFormat::AnnoKind_Deprecated, builder.CreateString(annotation->identifier.Val()), args);
            annotations.emplace_back(serialized);
        } else if (annotation->kind == AST::AnnotationKind::ATTRIBUTE) {
            auto hasTestRegisterAttr = false;

            for (auto attr : std::as_const(annotation->attrs)) {
                if (attr == "TEST_REGISTER") {
                    hasTestRegisterAttr = true;
                    break;
                }
            }

            if (hasTestRegisterAttr) {
                auto serialized = PackageFormat::CreateAnno(builder, PackageFormat::AnnoKind_TestRegistration,
                    builder.CreateString(annotation->identifier.Val()), builder.CreateVector<TAnnoArgOffset>({}));
                annotations.emplace_back(serialized);
            }
        } else if (annotation->kind == AST::AnnotationKind::FROZEN) {
            auto frozen = PackageFormat::CreateAnno(builder, PackageFormat::AnnoKind_Frozen,
                builder.CreateString(annotation->identifier.Val()), builder.CreateVector<TAnnoArgOffset>({}));
            annotations.emplace_back(frozen);
        } else if (annotation->kind == AST::AnnotationKind::JAVA_MIRROR) {
            auto args = builder.CreateVector<TAnnoArgOffset>(SaveAnnotationArgs(*annotation));
            auto mirror = PackageFormat::CreateAnno(builder, PackageFormat::AnnoKind_JavaMirror,
                builder.CreateString(annotation->identifier.Val()), args);
                annotations.emplace_back(mirror);
        } else if (annotation->kind == AST::AnnotationKind::JAVA_IMPL) {
            auto args = builder.CreateVector<TAnnoArgOffset>(SaveAnnotationArgs(*annotation));
            auto impl = PackageFormat::CreateAnno(builder, PackageFormat::AnnoKind_JavaImpl,
                builder.CreateString(annotation->identifier.Val()), args);
                annotations.emplace_back(impl);
        }  else if (annotation->kind == AST::AnnotationKind::JAVA_HAS_DEFAULT) {
            auto args = builder.CreateVector<TAnnoArgOffset>(SaveAnnotationArgs(*annotation));
            auto javaHasDefault = PackageFormat::CreateAnno(builder, PackageFormat::AnnoKind_JavaHasDefault,
                builder.CreateString(annotation->identifier.Val()), args);
                annotations.emplace_back(javaHasDefault);
        } else if (annotation->kind == AST::AnnotationKind::OBJ_C_MIRROR) {
            auto args = builder.CreateVector<TAnnoArgOffset>(SaveAnnotationArgs(*annotation));
            auto mirror = PackageFormat::CreateAnno(builder, PackageFormat::AnnoKind_ObjCMirror,
                builder.CreateString(annotation->identifier.Val()), args);
                annotations.emplace_back(mirror);
        } else if (annotation->kind == AST::AnnotationKind::OBJ_C_IMPL) {
            auto args = builder.CreateVector<TAnnoArgOffset>(SaveAnnotationArgs(*annotation));
            auto impl = PackageFormat::CreateAnno(builder, PackageFormat::AnnoKind_ObjCImpl,
                builder.CreateString(annotation->identifier.Val()), args);
                annotations.emplace_back(impl);
        } else if (annotation->kind == AST::AnnotationKind::FOREIGN_NAME) {
            auto args = builder.CreateVector<TAnnoArgOffset>(SaveAnnotationArgs(*annotation));
            auto impl = PackageFormat::CreateAnno(builder, PackageFormat::AnnoKind_ForeignName,
                builder.CreateString(annotation->identifier.Val()), args);
                annotations.emplace_back(impl);
        } else if (annotation->kind == AST::AnnotationKind::CUSTOM && annotation->isCompileTimeVisible) {
            auto args = builder.CreateVector<TAnnoArgOffset>(SaveAnnotationArgs(*annotation));
            auto custom = PackageFormat::CreateAnno(
                builder, PackageFormat::AnnoKind_Custom, builder.CreateString(annotation->identifier.Val()), args);
            annotations.emplace_back(custom);
        }
    }

    return annotations;
}

std::vector<TAnnoArgOffset> ASTWriter::ASTWriterImpl::SaveAnnotationArgs(const Annotation& annotation)
{
    std::vector<TAnnoArgOffset> args;

    for (auto& arg : annotation.args) {
        // Only literal support yet.
        if (arg->expr->astKind != ASTKind::LIT_CONST_EXPR) {
            continue;
        }
        auto serialized =
            PackageFormat::CreateAnnoArg(builder, builder.CreateString(arg->name.GetRawText()), SaveExpr(*arg->expr));
        args.emplace_back(serialized);
    }

    return args;
}
