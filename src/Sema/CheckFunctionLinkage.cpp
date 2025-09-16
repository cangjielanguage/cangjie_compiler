// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements setting linkages for functions.
 */

#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Driver/StdlibMap.h"

using namespace Cangjie;
using namespace AST;

namespace {
bool IsSpecialFunction(const FuncDecl& fd)
{
    if (fd.TestAnyAttr(Attribute::IMPORTED, Attribute::FOREIGN)) {
        return true;
    }
    auto isGetCommandLineArgsFunc = fd.fullPackageName == CORE_PACKAGE_NAME && fd.identifier == GET_COMMAND_LINE_ARGS;
    auto isImplictUsed =
        Utils::In<std::string>(fd.fullPackageName, {CORE_PACKAGE_NAME, AST_PACKAGE_NAME}) &&
        fd.TestAttr(Attribute::IMPLICIT_USED);
    auto isToAny = fd.identifier == TO_ANY;
    auto isMainInvoke = fd.identifier == MAIN_INVOKE;
    return isGetCommandLineArgsFunc || isImplictUsed || isToAny || isMainInvoke;
}

void MarkFunctionLinkage(FuncDecl& fd, const Linkage linkage)
{
    fd.linkage = linkage;
    // Set linkage for the funcDecl obtained by desugaring parameters with default values
    if (fd.funcBody && !fd.funcBody->paramLists.empty()) {
        auto& funcParams = fd.funcBody->paramLists[0]->params;
        for (auto& param : funcParams) {
            if (param->desugarDecl) {
                param->desugarDecl->linkage = linkage;
            }
        }
    }
    if (fd.propDecl) {
        fd.propDecl->linkage = linkage;
    }
}

void MarkFunctionAsInternalLinkage(FuncDecl& fd)
{
    MarkFunctionLinkage(fd, Linkage::INTERNAL);
}

void MarkFunctionAsExternalLinkage(FuncDecl& fd)
{
    if (auto id = DynamicCast<InheritableDecl>(fd.outerDecl); id && id->linkage != Linkage::EXTERNAL) {
        id->linkage = Linkage::EXTERNAL;
    }
    if (fd.outerDecl) {
        if (fd.outerDecl->IsFunc()) {
            return; // Nested fuction should always be internal.
        }
    } else {
        if (!fd.TestAttr(Attribute::GLOBAL)) {
            return; // Non-toplevel/non-member function should always be internal.
        }
    }
    MarkFunctionLinkage(fd, Linkage::EXTERNAL);
}

inline void MarkVarDeclAbstractLinkage(VarDeclAbstract& vda)
{
    if (vda.IsExportedDecl() || vda.TestAttr(Attribute::IMPLICIT_USED)) {
        vda.linkage = Cangjie::Linkage::EXTERNAL;
    } else {
        vda.linkage = Cangjie::Linkage::INTERNAL;
    }
}

inline void MarkTypeAliasDeclLinkage(TypeAliasDecl& tad)
{
    if (tad.IsExportedDecl() || tad.TestAttr(Attribute::IMPLICIT_USED)) {
        tad.linkage = Cangjie::Linkage::EXTERNAL;
    } else {
        tad.linkage = Cangjie::Linkage::INTERNAL;
    }
}

/**
 * Some functions and classes in the core package are used by the compiler directly.
 * We filter them by their files.
 */
bool IsIntrinsicFile(const File& file)
{
    if (file.curPackage->fullPackageName != CORE_PACKAGE_NAME) {
        return false;
    }
    static const std::set<std::string> intrinsicFileNames{
        "future.cj",
        "runtime_call_throw_exception.cj",
    };
    return intrinsicFileNames.count(file.fileName) > 0;
}

inline bool IsInternalSrcExportedFunction(const FuncDecl& fd)
{
    return !fd.IsExportedDecl() && CanBeSrcExported(fd);
}

/**
 * Auxiliary functions to iterate all functions in a nominal declaration.
 */
void IterateAllFunctionInStruct(InheritableDecl& inheritableDecl, const std::function<void(FuncDecl&)> action)
{
    for (auto& decl : inheritableDecl.GetMemberDecls()) {
        if (decl->astKind == ASTKind::FUNC_DECL) {
            auto funcDecl = StaticAs<ASTKind::FUNC_DECL>(decl.get());
            action(*funcDecl);
        }
        if (decl->astKind == ASTKind::PROP_DECL) {
            auto propDecl = StaticAs<ASTKind::PROP_DECL>(decl.get());
            for (OwnedPtr<FuncDecl>& funcDecl : propDecl->getters) {
                action(*funcDecl.get());
            }
            for (OwnedPtr<FuncDecl>& funcDecl : propDecl->setters) {
                action(*funcDecl.get());
            }
        }
    }
}

/**
 * Auxiliary functions to iterate all variables and properties in a nominal declaration.
 */
void IterateAllVariableInStruct(InheritableDecl& inheritableDecl, const std::function<void(VarDecl&)> action)
{
    for (auto& decl : inheritableDecl.GetMemberDecls()) {
        if (auto vd = DynamicCast<VarDecl>(decl.get())) {
            action(*vd);
        }
    }
}

/**
 * Auxiliary functions to iterate all functions in an package.
 */
void IterateAllMembersInStruct(InheritableDecl& inheritableDecl, const std::function<void(Decl&)> action)
{
    for (auto& decl : inheritableDecl.GetMemberDecls()) {
        if (decl->astKind == ASTKind::PROP_DECL) {
            auto propDecl = StaticAs<ASTKind::PROP_DECL>(decl.get());
            for (OwnedPtr<FuncDecl>& funcDecl : propDecl->getters) {
                action(*funcDecl);
            }
            for (OwnedPtr<FuncDecl>& funcDecl : propDecl->setters) {
                action(*funcDecl);
            }
        } else if (decl->astKind != ASTKind::PRIMARY_CTOR_DECL) {
            action(*decl);
        }
    }
}

void AnalyzeMemberDeclAsInternalLinkageInStruct(InheritableDecl& id)
{
    if (!id.IsExportedDecl() && !id.TestAttr(Attribute::C)) {
        id.linkage = Linkage::INTERNAL;
    }
    IterateAllFunctionInStruct(id, [](auto& fd) {
        if (!IsSpecialFunction(fd) && !fd.IsExportedDecl()) {
            MarkFunctionAsInternalLinkage(fd);
        }
    });
    IterateAllVariableInStruct(id, MarkVarDeclAbstractLinkage);
}

void AnalyzeFunctionAsInternalLinkageInExtend(ExtendDecl& ed, bool exportForTest)
{
    bool isExportedExtend = exportForTest || ed.IsExportedDecl();
    ed.linkage = isExportedExtend ? Linkage::EXTERNAL : Linkage::INTERNAL;
    IterateAllFunctionInStruct(ed, [isExportedExtend](auto& fd) {
        // Ignore whether extend is public, to HACK function's linkage for decls used in src exported internal decls.
        bool publicInstance = fd.TestAttr(Attribute::PUBLIC) && !fd.TestAttr(Attribute::STATIC);
        if (!IsSpecialFunction(fd) && (!fd.IsExportedDecl() || (!isExportedExtend && !publicInstance))) {
            MarkFunctionAsInternalLinkage(fd);
        }
    });
}

/**
 * Iterate all functions,
 * for each *internal* function, set it with the internal linkage.
 */
void AnalyzeLinkageBasedOnModifier(Package& pkg, const GlobalOptions& opt)
{
    for (auto& file : pkg.files) {
        if (IsIntrinsicFile(*file)) {
            continue;
        }
        for (auto& decl : file->decls) {
            if (auto vda = DynamicCast<VarDeclAbstract*>(decl.get())) {
                MarkVarDeclAbstractLinkage(*vda);
            } else if (auto fd = DynamicCast<FuncDecl*>(decl.get());
                fd && !IsSpecialFunction(*fd) && !fd->IsExportedDecl()) {
                MarkFunctionAsInternalLinkage(*fd);
            } else if (auto ed = DynamicCast<ExtendDecl*>(decl.get()); ed) {
                AnalyzeFunctionAsInternalLinkageInExtend(*ed, opt.exportForTest);
            } else if (auto id = DynamicCast<InheritableDecl*>(decl.get()); id) {
                AnalyzeMemberDeclAsInternalLinkageInStruct(*id);
            } else if (auto tad = DynamicCast<TypeAliasDecl>(decl.get()); tad) {
                MarkTypeAliasDeclLinkage(*tad);
            }
        }
    }
}

class ExternalLinkageAnalyzer {
public:
    explicit ExternalLinkageAnalyzer(const Package& pkg) : pkg(pkg)
    {
    }
    void Run()
    {
        IterateToplevelDecls(pkg, [this](auto& decl) { PerformPublicType(decl); });

        while (!srcExportedDecls.empty() || !exportedTys.empty()) {
            AnalyzeExternalLinkageBySrcExportedDecl();
            AnalyzeExternalLinkageByExportedTy();
        }
    }

private:
    void PerformPublicType(const OwnedPtr<Decl>& decl);
    void HandleMemberDeclInTopLevelDecl(Decl& decl);
    void AnalyzeExternalLinkageBySrcExportedDecl();
    void AnalyzeExternalLinkageByExportedTy();
    void HandleMemberDeclsByTy(const InheritableDecl& id);
    // Target of a reference node will not be propDecl,
    // since all propDecl accesses will be rearraged to getter/setter function which belongs to the propDecl.
    void SetTargetLinkage(Ptr<Decl> target)
    {
        if (auto fd = DynamicCast<FuncDecl>(target)) {
            SetFuncTargetLinkage(*fd);
        } else if (auto vd = DynamicCast<VarDecl>(target)) {
            SetVarTargetLinkage(*vd);
        } else if (auto tad = DynamicCast<TypeAliasDecl>(target)) {
            tad->linkage = Linkage::EXTERNAL;
        } else {
            AddExportedTy(target->ty);
        }
    }
    void SetFuncTargetLinkage(FuncDecl& fd, bool byTy = false);
    void SetPropTargetLinkage(PropDecl& pd, bool byTy = false);
    void SetVarTargetLinkage(VarDecl& vd, bool byTy = false);
    void AddSrcExportedDecl(Ptr<Decl> decl)
    {
        if (visitedSrcExportedDecls.count(decl) == 0) {
            srcExportedDecls.emplace(decl);
        }
    }
    void AddExportedTy(Ptr<Ty> ty)
    {
        if (visitedExportedTys.count(ty) == 0) {
            exportedTys.emplace(ty);
        }
    }

    const Package& pkg;
    std::unordered_set<Ptr<Decl>> srcExportedDecls; // Include FuncDecl and VarDecl.
    std::unordered_set<Ptr<Ty>> exportedTys;
    std::unordered_set<Ptr<Decl>> visitedSrcExportedDecls;
    std::unordered_set<Ptr<Ty>> visitedExportedTys;
};

/**
 * Because instance member variables determine the memory layout of a type, all of its instance member variables
 * should be stored in cjo as long as the type is externally visible.
 * So we should treat the type of the member variable as externally visible and store the latter in the cjo.
 */
inline bool IsMemberInMemLayout(const Decl& member)
{
    CJC_ASSERT(member.outerDecl);
    const auto& id = *member.outerDecl;
    return (id.IsStructOrClassDecl() && member.astKind == ASTKind::VAR_DECL && !member.TestAttr(Attribute::STATIC)) ||
        member.TestAttr(Attribute::ENUM_CONSTRUCTOR);
}

void ExternalLinkageAnalyzer::PerformPublicType(const OwnedPtr<Decl>& decl)
{
    if (!decl->IsExportedDecl() || (decl->astKind == ASTKind::FUNC_DECL && decl->TestAttr(Attribute::FOREIGN))) {
        return;
    }
    decl->linkage = Linkage::EXTERNAL;
    if (auto vd = DynamicCast<VarDecl>(decl.get()); vd && vd->isConst) {
        AddSrcExportedDecl(vd);
        return;
    }
    if (auto fd = DynamicCast<FuncDecl>(decl.get()); fd && CanBeSrcExported(*fd)) {
        AddSrcExportedDecl(fd);
        return;
    }
    auto id = DynamicCast<InheritableDecl*>(decl.get());
    if (!id) {
        return;
    }
    for (auto& super : id->GetAllSuperDecls()) {
        AddExportedTy(super->ty);
    }

    IterateAllMembersInStruct(*id, [this](Decl& decl) { HandleMemberDeclInTopLevelDecl(decl); });
}

void ExternalLinkageAnalyzer::HandleMemberDeclInTopLevelDecl(Decl& decl)
{
    if (decl.astKind == ASTKind::PRIMARY_CTOR_DECL) {
        return;
    }
    if (auto fd = DynamicCast<FuncDecl>(&decl)) {
        // The finalizer of a type is special, which determines whether CHIR appends an additional bool member
        // `hasInit` to the type to indicate whether the type has been initialized,
        // so as long as the type is externally visible, the finalizer must be exported.
        if (fd->TestAttr(Attribute::FINALIZER)) {
            MarkFunctionAsExternalLinkage(*fd);
            return;
        }
        if (fd->IsExportedDecl()) {
            AddExportedTy(fd->ty);
            if (CanBeSrcExported(*fd)) {
                AddSrcExportedDecl(fd);
            }
        }
        return;
    }
    auto vd = DynamicCast<VarDecl>(&decl);
    if (!vd) {
        return;
    }
    if (IsMemberInMemLayout(*vd)) {
        AddExportedTy(vd->ty);
    }
    if (IsInstMemberVarInGenericDecl(*vd)) {
        AddSrcExportedDecl(vd);
    }
    if (!vd->IsExportedDecl()) {
        return;
    }
    AddExportedTy(vd->ty);
    if (auto pd = DynamicCast<PropDecl>(vd); pd && (pd->isConst || pd->HasAnno(AnnotationKind::FROZEN))) {
        for (auto& getter : std::as_const(pd->getters)) {
            AddSrcExportedDecl(getter.get());
        }
        for (auto& setter : std::as_const(pd->setters)) {
            AddSrcExportedDecl(setter.get());
        }
    }
}

void ExternalLinkageAnalyzer::AnalyzeExternalLinkageBySrcExportedDecl()
{
    while (!srcExportedDecls.empty()) {
        auto decl = *srcExportedDecls.begin();
        visitedSrcExportedDecls.emplace(decl);
        srcExportedDecls.erase(decl);

        auto id = Walker::GetNextWalkerID();
        std::function<VisitAction(Ptr<Node>)> visitor = [this](Ptr<Node> n) {
            if (auto fd = DynamicCast<FuncDecl*>(n)) {
                bool shouldMarkExternal = !IsInternalSrcExportedFunction(*fd);
                if (shouldMarkExternal) {
                    MarkFunctionAsExternalLinkage(*fd);
                }
                if (CanBeSrcExported(*fd)) {
                    AddSrcExportedDecl(fd);
                }
                return VisitAction::WALK_CHILDREN;
            }
            auto target = TypeCheckUtil::GetRealTarget(n->GetTarget());
            if (target == nullptr) {
                return VisitAction::WALK_CHILDREN;
            }
            // If target is TypeAliasDecl, it should be exported too.
            SetTargetLinkage(n->GetTarget());
            SetTargetLinkage(target);
            return VisitAction::WALK_CHILDREN;
        };

        Walker walker(decl, id, visitor);
        walker.Walk();
    }
}

void ExternalLinkageAnalyzer::AnalyzeExternalLinkageByExportedTy()
{
    while (!exportedTys.empty()) {
        auto ty = *exportedTys.begin();
        visitedExportedTys.emplace(ty);
        exportedTys.erase(ty);
        if (!Ty::IsTyCorrect(ty)) {
            continue;
        }
        if (ty->IsFunc()) {
            auto funcTy = StaticCast<FuncTy>(ty);
            for (auto paramTy : std::as_const(funcTy->paramTys)) {
                AddExportedTy(paramTy);
            }
            AddExportedTy(funcTy->retTy);
        } else if (ty->IsGeneric()) {
            auto genTy = StaticCast<GenericsTy>(ty);
            for (auto up : std::as_const(genTy->upperBounds)) {
                AddExportedTy(up);
            }
        } else {
            for (auto tyArg : std::as_const(ty->typeArgs)) {
                AddExportedTy(tyArg);
            }
        }

        auto decl = Ty::GetDeclPtrOfTy(ty);
        if (!decl) {
            continue;
        }
        decl->linkage = Linkage::EXTERNAL;
        if (auto id = DynamicCast<InheritableDecl>(decl)) {
            HandleMemberDeclsByTy(*id);
        }
        if (auto ed = DynamicCast<EnumDecl>(decl)) {
            for (auto& ctor : std::as_const(ed->constructors)) {
                if (auto fd = DynamicCast<FuncDecl>(ctor.get())) {
                    SetFuncTargetLinkage(*fd);
                } else if (auto vd = DynamicCast<VarDecl>(ctor.get())) {
                    SetVarTargetLinkage(*vd, true);
                }
            }
        }
    }
}

void ExternalLinkageAnalyzer::HandleMemberDeclsByTy(const InheritableDecl& id)
{
    for (auto& member : id.GetMemberDecls()) {
        const bool isFuncOrProp = member->astKind == ASTKind::FUNC_DECL || member->astKind == ASTKind::PROP_DECL;
        const bool isInstMemberInVTable = isFuncOrProp && member->TestAnyAttr(Attribute::PUBLIC, Attribute::PROTECTED);
        const bool isStaticMemberInVTable =
            isFuncOrProp && !member->TestAttr(Attribute::PRIVATE) && member->TestAttr(Attribute::STATIC);
        const bool isMemberInMemLayout = IsMemberInMemLayout(*member);
        if (!isInstMemberInVTable && !isStaticMemberInVTable && !isMemberInMemLayout && !member->IsExportedDecl()) {
            continue;
        }
        if (auto fd = DynamicCast<FuncDecl>(member.get())) {
            // The finalizer of a type is special, which determines whether CHIR appends an additional bool member
            // `hasInit` to the type to indicate whether the type has been initialized,
            // so as long as the type is externally visible, the finalizer must be exported.
            if (fd->TestAttr(Attribute::FINALIZER)) {
                MarkFunctionAsExternalLinkage(*fd);
            } else {
                SetFuncTargetLinkage(*fd, true);
            }
        } else if (auto pd = DynamicCast<PropDecl>(member.get())) {
            SetPropTargetLinkage(*pd, true);
        } else if (auto vd = DynamicCast<VarDecl>(member.get())) {
            SetVarTargetLinkage(*vd, true);
        }
    }
}

void ExternalLinkageAnalyzer::SetFuncTargetLinkage(FuncDecl& fd, [[maybe_unused]] bool byTy)
{
    if (!IsGlobalOrMember(fd)) {
        return;
    }
    MarkFunctionLinkage(fd, Linkage::EXTERNAL);
    if (CanBeSrcExported(fd)) {
        AddSrcExportedDecl(&fd);
    }
    AddExportedTy(fd.ty);
    if (fd.outerDecl) {
        fd.outerDecl->linkage = Linkage::EXTERNAL;
        if (auto id = DynamicCast<InheritableDecl>(fd.outerDecl)) {
            for (auto& type : std::as_const(id->inheritedTypes)) {
                AddExportedTy(type->ty);
            }
        }
    }
    if (auto pd = fd.propDecl) {
        SetPropTargetLinkage(*pd);
    }
}

void ExternalLinkageAnalyzer::SetPropTargetLinkage(PropDecl& pd, bool byTy)
{
    SetVarTargetLinkage(pd, byTy);
    bool canBeSrcExported =
        pd.isConst || pd.HasAnno(AnnotationKind::FROZEN) || IsInDeclWithAttribute(pd, Attribute::GENERIC);
    for (auto& getter : std::as_const(pd.getters)) {
        MarkFunctionLinkage(*getter, Linkage::EXTERNAL);
        if (canBeSrcExported && visitedSrcExportedDecls.count(getter.get()) == 0) {
            AddSrcExportedDecl(getter.get());
        }
    }
    for (auto& setter : std::as_const(pd.setters)) {
        MarkFunctionLinkage(*setter, Linkage::EXTERNAL);
        if (canBeSrcExported && visitedSrcExportedDecls.count(setter.get()) == 0) {
            AddSrcExportedDecl(setter.get());
        }
    }
}

void ExternalLinkageAnalyzer::SetVarTargetLinkage(VarDecl& vd, bool byTy)
{
    if (!IsGlobalOrMember(vd) ||
        (byTy && vd.astKind == ASTKind::VAR_DECL && vd.TestAttr(Attribute::PRIVATE, Attribute::STATIC))) {
        return;
    }
    vd.linkage = Linkage::EXTERNAL;
    if (vd.isConst || vd.HasAnno(AnnotationKind::FROZEN) || IsInstMemberVarInGenericDecl(vd) ||
        IsMemberVarShouldBeSrcExported(vd)) {
        AddSrcExportedDecl(&vd);
    }
    AddExportedTy(vd.ty);
    if (vd.outerDecl) {
        vd.outerDecl->linkage = Linkage::EXTERNAL;
        if (auto id = DynamicCast<InheritableDecl>(vd.outerDecl)) {
            for (auto& type : std::as_const(id->inheritedTypes)) {
                AddExportedTy(type->ty);
            }
        }
    }
}
} // namespace

/**
 * The term `internal/external` is used at two levels, we should tell the different first to avoid abusing them:
 *   - visibility at the Cangjie level
 *   - linkage at the binary level.
 * The visibility controls whether a package can import an item from another package;
 * while the linkage decides whether symbols in an object file (compiled from a package) can be linked by other files.
 * An item (e.g. function) with external visibility is always with external linkage;
 * however, items with internal visibility may be also with external linkage.
 * In summary,
 * A function can be set as internal (linkage) if it meets one of the following conditions:
 * 1. a top-level function and not marked as `external` (visibility)
 * 2. a method (no matter it's public or private) of an internal (visibility) class or struct
 * 3. a private method of a class/struct (no matter the class/struct is external or not)
 * Note that there exist some corner cases where functions should be *external* (linkage):
 * 1. a function is marked as intrinsic, it's never internal (linkage)
 *    because the compiler may call it directly.
 * 2. a function used by an external (linkage) generic function.
 *    Though the function's visibility is internal, its linkage should be external.
 * 3. Also, some special functions should be external (linkage), e.g., rt$CreateOverflowException_msg.
 * =====
 * The analysis process:
 * 1. mark all functions (satisfying the above conditions) as internal linkage (including generic and normal functions)
 * 2. for each external (linkage) generic functions marked by the first step,
 *    mark all functions used in the function body as external (linkage).
 * =====
 * Example:
 * func foo() {}
 * external func bar<T>() { foo() }
 * `foo` should be internal visibility but external linkage.
 * This example also shows the difference between visibility and linkage.
 * The former means whether a function can be imported and used (from the CangjieLang level);
 * while the latter means whether a function can be linked (from the binary level).
 */
void TypeChecker::TypeCheckerImpl::AnalyzeFunctionLinkage(Package& pkg) const
{
    AnalyzeLinkageBasedOnModifier(pkg, ci->invocation.globalOptions);
    ExternalLinkageAnalyzer(pkg).Run();
    IterateToplevelDecls(pkg, [](auto& decl) {
        if (auto id = DynamicCast<InheritableDecl>(decl.get()); id && id->linkage == Linkage::INTERNAL) {
            IterateAllFunctionInStruct(*id, MarkFunctionAsInternalLinkage);
            IterateAllMembersInStruct(*id, [](Decl& decl) {
                if (auto vd = DynamicCast<VarDecl>(&decl)) {
                    vd->linkage = Linkage::INTERNAL;
                }
            });
        }
    });
    // If has same name private decls are non-internal in one package diag error.
    // NOTE: It should be remove when bug of PrivateDecl.ti is fixed.
    //       The identifier for PrivateDecl.ti does not use the file name for differentiation.
    //       The above modifications are incompatible, and the action is postponed.
    std::unordered_map<std::string, Ptr<Decl>> privateDeclMap;
    IterateToplevelDecls(pkg, [this, &privateDeclMap](OwnedPtr<Decl>& decl) {
        if (decl->IsNominalDecl() && decl->TestAttr(Attribute::PRIVATE) && decl->linkage != Linkage::INTERNAL) {
            auto ret = privateDeclMap.emplace(decl->identifier.Val(), decl.get());
            if (!ret.second) {
                auto builder = diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_export_same_private_decl, MakeRange(ret.first->second->identifier));
                builder.AddNote(MakeRange(decl->identifier), "same with private declaration");
            }
        }
    });
}
