// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * PartialInstantiation is the class to partial instantiation.
 */

#ifndef CANGJIE_SEMA_PARTIAL_INSTANTIATION_H
#define CANGJIE_SEMA_PARTIAL_INSTANTIATION_H
#include <functional>
#include <memory>

#include "cangjie/AST/Node.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Sema/CommonTypeAlias.h"
#include "cangjie/Utils/SafePointer.h"

namespace Cangjie {
void SetOptLevel(const GlobalOptions& opts);
GlobalOptions::OptimizationLevel GetOptLevel();
/**
 * Check whether the location where the instantiation is triggered is in the context with Open semantics.
 * Return true if expr is in a member of an open class or interface.
 */
bool IsInOpenContext(const std::vector<Ptr<AST::Decl>>& contextDecl);
bool RequireInstantiation(const AST::Decl& decl, bool isInOpenContext = false);

using VisitFunc = std::function<void(AST::Node&, AST::Node&)>;
void DefaultVisitFunc(const AST::Node& source, const AST::Node& target);
AST::MacroInvocation InstantiateMacroInvocation(const AST::MacroInvocation& me);
OwnedPtr<AST::Generic> InstantiateGeneric(const AST::Generic& generic, const VisitFunc& visitor);
class PartialInstantiation {
public:
    template <typename T> static OwnedPtr<T> Instantiate(Ptr<T> node, const VisitFunc& visitFunc)
    {
        OwnedPtr<AST::Node> clonedNode = PartialInstantiation().InstantiateWithRearrange(node, visitFunc);
        return OwnedPtr<T>(static_cast<T*>(clonedNode.release()));
    }

    static Ptr<AST::Decl> GetGeneralDecl(AST::Decl& clonedDecl)
    {
        if (clonedDecl.genericDecl) {
            return clonedDecl.genericDecl;
        } else if (clonedDecl.TestAttr(AST::Attribute::GENERATED_TO_MOCK)) {
            return &clonedDecl;
        } else {
            return ins2generic.at(&clonedDecl);
        }
    }

    static std::unordered_set<Ptr<AST::Decl>> GetInstantiatedDecl(const AST::Decl& genericDecl)
    {
        if (auto found = generic2ins.find(&genericDecl); found != generic2ins.end()) {
            return found->second;
        }
        return {};
    }

    static void ResetGlobalMap()
    {
        generic2ins.clear();
        ins2generic.clear();
    }

private:
    /** Map between 'pointer to source node pointer' to 'pointer to cloned node pointer'. */
    std::unordered_map<Ptr<AST::Node>*, Ptr<AST::Node>*> targetAddr2targetAddr;
    /** Map bewteen 'source node pointer' to 'cloned node pointer'. */
    std::unordered_map<Ptr<AST::Node>, Ptr<AST::Node>> source2cloned;
    static std::unordered_map<Ptr<const AST::Decl>, Ptr<AST::Decl>> ins2generic;
    static std::unordered_map<Ptr<const AST::Decl>, std::unordered_set<Ptr<AST::Decl>>> generic2ins;
    template <typename T> void TargetAddrMapInsert(Ptr<T>& from, Ptr<T>& target)
    {
        if (from == nullptr) {
            return;
        }
        targetAddr2targetAddr[reinterpret_cast<Ptr<AST::Node>*>(&from)] = reinterpret_cast<Ptr<AST::Node>*>(&target);
    }
    OwnedPtr<AST::Node> InstantiateWithRearrange(Ptr<AST::Node> node, const VisitFunc& visitor);
    template <typename NodeT> static OwnedPtr<NodeT> InstantiateNode(Ptr<NodeT> node, const VisitFunc& visitor);
    static OwnedPtr<AST::Type> InstantiateType(Ptr<AST::Type> type, const VisitFunc& visitor);
    template <typename ExprT>
    static OwnedPtr<ExprT> InstantiateExpr(Ptr<ExprT> expr, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<AST::Decl> InstantiateDecl(Ptr<AST::Decl> decl, const VisitFunc& visitor);
    static OwnedPtr<AST::Pattern> InstantiatePattern(Ptr<AST::Pattern> pattern, const VisitFunc& visitor);
    static OwnedPtr<AST::QualifiedType> InstantiateQualifiedType(
        const AST::QualifiedType& node, const VisitFunc& visitor);
    static OwnedPtr<AST::ParenType> InstantiateParenType(const AST::ParenType& node, const VisitFunc& visitor);
    static OwnedPtr<AST::OptionType> InstantiateOptionType(const AST::OptionType& node, const VisitFunc& visitor);
    static OwnedPtr<AST::FuncType> InstantiateFuncType(const AST::FuncType& node, const VisitFunc& visitor);
    static OwnedPtr<AST::TupleType> InstantiateTupleType(const AST::TupleType& node, const VisitFunc& visitor);
    static OwnedPtr<AST::ConstantType> InstantiateConstantType(const AST::ConstantType& node, const VisitFunc& visitor);
    static OwnedPtr<AST::VArrayType> InstantiateVArrayType(const AST::VArrayType& node, const VisitFunc& visitor);
    static OwnedPtr<AST::RefType> InstantiateRefType(const AST::RefType& type, const VisitFunc& visitor);
    static OwnedPtr<AST::MacroExpandExpr> InstantiateMacroExpandExpr(
        const AST::MacroExpandExpr& mee, const VisitFunc& visitor);
    static OwnedPtr<AST::TokenPart> InstantiateTokenPart(const AST::TokenPart& tp, const VisitFunc& visitor);
    static OwnedPtr<AST::QuoteExpr> InstantiateQuoteExpr(const AST::QuoteExpr& qe, const VisitFunc& visitor);
    static OwnedPtr<AST::IfExpr> InstantiateIfExpr(const AST::IfExpr& ie, const VisitFunc& visitor);
    static OwnedPtr<AST::TryExpr> InstantiateTryExpr(const AST::TryExpr& te, const VisitFunc& visitor);
    static OwnedPtr<AST::ThrowExpr> InstantiateThrowExpr(const AST::ThrowExpr& te, const VisitFunc& visitor);
    static OwnedPtr<AST::ReturnExpr> InstantiateReturnExpr(const AST::ReturnExpr& re, const VisitFunc& visitor);
    static OwnedPtr<AST::WhileExpr> InstantiateWhileExpr(const AST::WhileExpr& we, const VisitFunc& visitor);
    static OwnedPtr<AST::DoWhileExpr> InstantiateDoWhileExpr(const AST::DoWhileExpr& dwe, const VisitFunc& visitor);
    static OwnedPtr<AST::AssignExpr> InstantiateAssignExpr(const AST::AssignExpr& ae, const VisitFunc& visitor);
    static OwnedPtr<AST::IncOrDecExpr> InstantiateIncOrDecExpr(const AST::IncOrDecExpr& ide, const VisitFunc& visitor);
    static OwnedPtr<AST::UnaryExpr> InstantiateUnaryExpr(const AST::UnaryExpr& ue, const VisitFunc& visitor);
    static OwnedPtr<AST::BinaryExpr> InstantiateBinaryExpr(const AST::BinaryExpr& be, const VisitFunc& visitor);
    static OwnedPtr<AST::RangeExpr> InstantiateRangeExpr(const AST::RangeExpr& re, const VisitFunc& visitor);
    static OwnedPtr<AST::SubscriptExpr> InstantiateSubscriptExpr(
        const AST::SubscriptExpr& se, const VisitFunc& visitor);
    static OwnedPtr<AST::MemberAccess> InstantiateMemberAccess(const AST::MemberAccess& ma, const VisitFunc& visitor);
    static OwnedPtr<AST::CallExpr> InstantiateCallExpr(const AST::CallExpr& ce, const VisitFunc& visitor);
    static OwnedPtr<AST::ParenExpr> InstantiateParenExpr(const AST::ParenExpr& pe, const VisitFunc& visitor);
    static OwnedPtr<AST::LambdaExpr> InstantiateLambdaExpr(const AST::LambdaExpr& le, const VisitFunc& visitor);
    static OwnedPtr<AST::LitConstExpr> InstantiateLitConstExpr(const AST::LitConstExpr& lce, const VisitFunc& visitor);
    static OwnedPtr<AST::ArrayLit> InstantiateArrayLit(const AST::ArrayLit& al, const VisitFunc& visitor);
    static OwnedPtr<AST::ArrayExpr> InstantiateArrayExpr(const AST::ArrayExpr& ae, const VisitFunc& visitor);
    static OwnedPtr<AST::PointerExpr> InstantiatePointerExpr(const AST::PointerExpr& ptre, const VisitFunc& visitor);
    static OwnedPtr<AST::TupleLit> InstantiateTupleLit(const AST::TupleLit& tl, const VisitFunc& visitor);
    static OwnedPtr<AST::RefExpr> InstantiateRefExpr(const AST::RefExpr& re, const VisitFunc& visitor);
    static OwnedPtr<AST::ForInExpr> InstantiateForInExpr(const AST::ForInExpr& fie, const VisitFunc& visitor);
    static OwnedPtr<AST::MatchExpr> InstantiateMatchExpr(const AST::MatchExpr& me, const VisitFunc& visitor);
    static OwnedPtr<AST::JumpExpr> InstantiateJumpExpr(const AST::JumpExpr& je);
    static OwnedPtr<AST::TypeConvExpr> InstantiateTypeConvExpr(const AST::TypeConvExpr& tce, const VisitFunc& visitor);
    static OwnedPtr<AST::SpawnExpr> InstantiateSpawnExpr(const AST::SpawnExpr& se, const VisitFunc& visitor);
    static OwnedPtr<AST::SynchronizedExpr> InstantiateSynchronizedExpr(
        const AST::SynchronizedExpr& se, const VisitFunc& visitor);
    static OwnedPtr<AST::InvalidExpr> InstantiateInvalidExpr(const AST::InvalidExpr& ie);
    static OwnedPtr<AST::InterpolationExpr> InstantiateInterpolationExpr(
        const AST::InterpolationExpr& ie, const VisitFunc& visitor);
    static OwnedPtr<AST::StrInterpolationExpr> InstantiateStrInterpolationExpr(
        const AST::StrInterpolationExpr& sie, const VisitFunc& visitor);
    static OwnedPtr<AST::TrailingClosureExpr> InstantiateTrailingClosureExpr(
        const AST::TrailingClosureExpr& tc, const VisitFunc& visitor);
    static OwnedPtr<AST::IsExpr> InstantiateIsExpr(const AST::IsExpr& ie, const VisitFunc& visitor);
    static OwnedPtr<AST::AsExpr> InstantiateAsExpr(const AST::AsExpr& ae, const VisitFunc& visitor);
    static OwnedPtr<AST::OptionalExpr> InstantiateOptionalExpr(const AST::OptionalExpr& oe, const VisitFunc& visitor);
    static OwnedPtr<AST::OptionalChainExpr> InstantiateOptionalChainExpr(
        const AST::OptionalChainExpr& oce, const VisitFunc& visitor);
    static OwnedPtr<AST::LetPatternDestructor> InstantiateLetPatternDestructor(
        const AST::LetPatternDestructor& ldp, const VisitFunc& visitor);
    static OwnedPtr<AST::ConstPattern> InstantiateConstPattern(const AST::ConstPattern& cp, const VisitFunc& visitor);
    static OwnedPtr<AST::VarPattern> InstantiateVarPattern(const AST::VarPattern& vp, const VisitFunc& visitor);
    static OwnedPtr<AST::TuplePattern> InstantiateTuplePattern(const AST::TuplePattern& tp, const VisitFunc& visitor);
    static OwnedPtr<AST::TypePattern> InstantiateTypePattern(const AST::TypePattern& tp, const VisitFunc& visitor);
    static OwnedPtr<AST::EnumPattern> InstantiateEnumPattern(const AST::EnumPattern& ep, const VisitFunc& visitor);
    static OwnedPtr<AST::ExceptTypePattern> InstantiateExceptTypePattern(
        const AST::ExceptTypePattern& etp, const VisitFunc& visitor);
    static OwnedPtr<AST::VarOrEnumPattern> InstantiateVarOrEnumPattern(
        const AST::VarOrEnumPattern& vep, const VisitFunc& visitor);
    static OwnedPtr<AST::Block> InstantiateBlock(const AST::Block& block, const VisitFunc& visitor);
    static OwnedPtr<AST::ClassBody> InstantiateClassBody(const AST::ClassBody& cb, const VisitFunc& visitor);
    static OwnedPtr<AST::StructBody> InstantiateStructBody(const AST::StructBody& sb, const VisitFunc& visitor);
    static OwnedPtr<AST::InterfaceBody> InstantiateInterfaceBody(
        const AST::InterfaceBody& ib, const VisitFunc& visitor);
    static OwnedPtr<AST::GenericConstraint> InstantiateGenericConstraint(
        const AST::GenericConstraint& gc, const VisitFunc& visitor);
    static OwnedPtr<AST::FuncBody> InstantiateFuncBody(const AST::FuncBody& fb, const VisitFunc& visitor);
    static OwnedPtr<AST::VarDecl> InstantiateFuncParam(const AST::FuncParam& fp, const VisitFunc& visitor);
    static OwnedPtr<AST::FuncParamList> InstantiateFuncParamList(
        const AST::FuncParamList& fpl, const VisitFunc& visitor);
    static OwnedPtr<AST::FuncArg> InstantiateFuncArg(const AST::FuncArg& fa, const VisitFunc& visitor);
    static OwnedPtr<AST::Annotation> InstantiateAnnotation(const AST::Annotation& annotation, const VisitFunc& visitor);
    static OwnedPtr<AST::ImportSpec> InstantiateImportSpec(const AST::ImportSpec& is, const VisitFunc& visitor);
    static OwnedPtr<AST::MatchCase> InstantiateMatchCase(const AST::MatchCase& mc, const VisitFunc& visitor);
    static OwnedPtr<AST::MatchCaseOther> InstantiateMatchCaseOther(
        const AST::MatchCaseOther& mco, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiateGenericParamDecl(const AST::GenericParamDecl& gpd);
    static OwnedPtr<AST::Decl> InstantiateVarWithPatternDecl(
        const AST::VarWithPatternDecl& vwpd, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiateVarDecl(const AST::VarDecl& vd, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiateFuncDecl(const AST::FuncDecl& fd, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiatePrimaryCtorDecl(const AST::PrimaryCtorDecl& pcd, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiatePropDecl(const AST::PropDecl& pd, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiateExtendDecl(const AST::ExtendDecl& ed, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiateMacroExpandDecl(const AST::MacroExpandDecl& med);
    static OwnedPtr<AST::Decl> InstantiateStructDecl(const AST::StructDecl& sd, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiateClassDecl(const AST::ClassDecl& cd, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiateInterfaceDecl(const AST::InterfaceDecl& id, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiateEnumDecl(const AST::EnumDecl& ed, const VisitFunc& visitor);
    static OwnedPtr<AST::Decl> InstantiateTypeAliasDecl(const AST::TypeAliasDecl& tad, const VisitFunc& visitor);
};

class TypeManager;
using ReversedTypeSubst = std::map<Ptr<AST::Ty>, Ptr<TyVar>>;
class TyGeneralizer {
public:
    TyGeneralizer(TypeManager& tyMgr, const ReversedTypeSubst& mapping) : tyMgr(tyMgr), typeMapping(mapping)
    {
    }

    ~TyGeneralizer() = default;

    inline Ptr<AST::Ty> Generalize(Ptr<AST::Ty> ty)
    {
        return AST::Ty::IsTyCorrect(ty) ? Generalize(*ty) : ty;
    }

private:
    Ptr<AST::Ty> Generalize(AST::Ty& ty);
    Ptr<AST::Ty> GetGeneralizedStructTy(AST::StructTy& structTy);
    Ptr<AST::Ty> GetGeneralizedClassTy(AST::ClassTy& classTy);
    Ptr<AST::Ty> GetGeneralizedInterfaceTy(AST::InterfaceTy& interfaceTy);
    Ptr<AST::Ty> GetGeneralizedEnumTy(AST::EnumTy& enumTy);
    Ptr<AST::Ty> GetGeneralizedArrayTy(AST::ArrayTy& arrayTy);
    Ptr<AST::Ty> GetGeneralizedPointerTy(AST::PointerTy& cptrTy);
    // Get instantiated ty of set type 'IntersectionTy' and 'UnionTy'.
    template <typename SetTy> Ptr<AST::Ty> GetGeneralizedSetTy(SetTy& ty);

    TypeManager& tyMgr;
    const ReversedTypeSubst& typeMapping;
};
} // namespace Cangjie
#endif
