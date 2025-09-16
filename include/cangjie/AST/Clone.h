// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares AST clone apis. Moreover, a callback function is provided as
 * a parameter @p visitor which supports doing something during clone period.
 */

#ifndef CANGJIE_AST_CLONE_H
#define CANGJIE_AST_CLONE_H

#include <functional>
#include <memory>

#include "cangjie/AST/NodeX.h"

namespace Cangjie::AST {
using VisitFunc = std::function<void(Node&, Node&)>;
void DefaultVisitFunc(const Node& source, const Node& target);
void SetIsClonedSourceCode(const Node& source, Node& target);
void CopyBasicInfo(Ptr<const Node> source, Ptr<Node> target);
MacroInvocation CloneMacroInvocation(const MacroInvocation& me);
OwnedPtr<Generic> CloneGeneric(const Generic& generic, const VisitFunc& visitor = DefaultVisitFunc);
class ASTCloner {
public:
    template <typename T>
    static std::vector<OwnedPtr<T>> CloneVector(const std::vector<OwnedPtr<T>>& nodes)
    {
        std::vector<OwnedPtr<T>> resNodes;
        for (auto& it : nodes) {
            (void)resNodes.emplace_back(Clone(it.get()));
        }
        return resNodes;
    }

    template <typename T> static OwnedPtr<T> Clone(Ptr<T> node, const VisitFunc& visitFunc = DefaultVisitFunc)
    {
        OwnedPtr<Node> clonedNode = ASTCloner().CloneWithRearrange(node, visitFunc);
        return OwnedPtr<T>(static_cast<T*>(clonedNode.release()));
    }

private:
    /** Map between 'pointer to source node pointer' to 'pointer to cloned node pointer'. */
    std::unordered_map<Ptr<Node>*, Ptr<Node>*> targetAddr2targetAddr;
    /** Map bewteen 'source node pointer' to 'cloned node pointer'. */
    std::unordered_map<Ptr<Node>, Ptr<Node>> source2cloned;
    template <typename T> void TargetAddrMapInsert(Ptr<T>& from, Ptr<T>& target)
    {
        if (from == nullptr) {
            return;
        }
        targetAddr2targetAddr[reinterpret_cast<Ptr<Node>*>(&from)] = reinterpret_cast<Ptr<Node>*>(&target);
    }
    OwnedPtr<Node> CloneWithRearrange(Ptr<Node> node, const VisitFunc& visitor = DefaultVisitFunc);
    /** Get a cloned unique_ptr of a given node. */
    template <typename NodeT>
    static OwnedPtr<NodeT> CloneNode(Ptr<NodeT> node, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<Type> CloneType(Ptr<Type> type, const VisitFunc& visitor = DefaultVisitFunc);
    template <typename ExprT>
    static OwnedPtr<ExprT> CloneExpr(Ptr<ExprT> expr, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<Decl> CloneDecl(Ptr<Decl> decl, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<Pattern> ClonePattern(Ptr<Pattern> pattern, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<QualifiedType> CloneQualifiedType(const QualifiedType& node, const VisitFunc& visitor);
    static OwnedPtr<ParenType> CloneParenType(const ParenType& node, const VisitFunc& visitor);
    static OwnedPtr<OptionType> CloneOptionType(const OptionType& node, const VisitFunc& visitor);
    static OwnedPtr<FuncType> CloneFuncType(const FuncType& node, const VisitFunc& visitor);
    static OwnedPtr<TupleType> CloneTupleType(const TupleType& node, const VisitFunc& visitor);
    static OwnedPtr<ConstantType> CloneConstantType(const ConstantType& node, const VisitFunc& visitor);
    static OwnedPtr<VArrayType> CloneVArrayType(const VArrayType& node, const VisitFunc& visitor);
    static OwnedPtr<RefType> CloneRefType(const RefType& type, const VisitFunc& visitor);
    static OwnedPtr<MacroExpandExpr> CloneMacroExpandExpr(const MacroExpandExpr& mee, const VisitFunc& visitor);
    static OwnedPtr<TokenPart> CloneTokenPart(const TokenPart& tp, const VisitFunc& visitor);
    static OwnedPtr<QuoteExpr> CloneQuoteExpr(const QuoteExpr& qe, const VisitFunc& visitor);
    static OwnedPtr<IfExpr> CloneIfExpr(const IfExpr& ie, const VisitFunc& visitor);
    static OwnedPtr<TryExpr> CloneTryExpr(const TryExpr& te, const VisitFunc& visitor);
    static OwnedPtr<ThrowExpr> CloneThrowExpr(const ThrowExpr& te, const VisitFunc& visitor);
    static OwnedPtr<PerformExpr> ClonePerformExpr(const PerformExpr& pe, const VisitFunc& visitor);
    static OwnedPtr<ResumeExpr> CloneResumeExpr(const ResumeExpr& re, const VisitFunc& visitor);
    static OwnedPtr<ReturnExpr> CloneReturnExpr(const ReturnExpr& re, const VisitFunc& visitor);
    static OwnedPtr<WhileExpr> CloneWhileExpr(const WhileExpr& we, const VisitFunc& visitor);
    static OwnedPtr<DoWhileExpr> CloneDoWhileExpr(const DoWhileExpr& dwe, const VisitFunc& visitor);
    static OwnedPtr<AssignExpr> CloneAssignExpr(const AssignExpr& ae, const VisitFunc& visitor);
    static OwnedPtr<IncOrDecExpr> CloneIncOrDecExpr(const IncOrDecExpr& ide, const VisitFunc& visitor);
    static OwnedPtr<UnaryExpr> CloneUnaryExpr(const UnaryExpr& ue, const VisitFunc& visitor);
    static OwnedPtr<BinaryExpr> CloneBinaryExpr(const BinaryExpr& be, const VisitFunc& visitor);
    static OwnedPtr<RangeExpr> CloneRangeExpr(const RangeExpr& re, const VisitFunc& visitor);
    static OwnedPtr<SubscriptExpr> CloneSubscriptExpr(const SubscriptExpr& se, const VisitFunc& visitor);
    static OwnedPtr<MemberAccess> CloneMemberAccess(const MemberAccess& ma, const VisitFunc& visitor);
    static OwnedPtr<CallExpr> CloneCallExpr(const CallExpr& ce, const VisitFunc& visitor);
    static OwnedPtr<ParenExpr> CloneParenExpr(const ParenExpr& pe, const VisitFunc& visitor);
    static OwnedPtr<LambdaExpr> CloneLambdaExpr(const LambdaExpr& le, const VisitFunc& visitor);
    static OwnedPtr<LitConstExpr> CloneLitConstExpr(const LitConstExpr& lce, const VisitFunc& visitor);
    static OwnedPtr<ArrayLit> CloneArrayLit(const ArrayLit& al, const VisitFunc& visitor);
    static OwnedPtr<ArrayExpr> CloneArrayExpr(const ArrayExpr& ae, const VisitFunc& visitor);
    static OwnedPtr<PointerExpr> ClonePointerExpr(const PointerExpr& ptre, const VisitFunc& visitor);
    static OwnedPtr<TupleLit> CloneTupleLit(const TupleLit& tl, const VisitFunc& visitor);
    static OwnedPtr<RefExpr> CloneRefExpr(const RefExpr& re, const VisitFunc& visitor);
    static OwnedPtr<ForInExpr> CloneForInExpr(const ForInExpr& fie, const VisitFunc& visitor);
    static OwnedPtr<MatchExpr> CloneMatchExpr(const MatchExpr& me, const VisitFunc& visitor);
    static OwnedPtr<JumpExpr> CloneJumpExpr(const JumpExpr& je);
    static OwnedPtr<TypeConvExpr> CloneTypeConvExpr(const TypeConvExpr& tce, const VisitFunc& visitor);
    static OwnedPtr<SpawnExpr> CloneSpawnExpr(const SpawnExpr& se, const VisitFunc& visitor);
    static OwnedPtr<SynchronizedExpr> CloneSynchronizedExpr(const SynchronizedExpr& se, const VisitFunc& visitor);
    static OwnedPtr<InvalidExpr> CloneInvalidExpr(const InvalidExpr& ie);
    static OwnedPtr<InterpolationExpr> CloneInterpolationExpr(const InterpolationExpr& ie, const VisitFunc& visitor);
    static OwnedPtr<StrInterpolationExpr> CloneStrInterpolationExpr(
        const StrInterpolationExpr& sie, const VisitFunc& visitor);
    static OwnedPtr<TrailingClosureExpr> CloneTrailingClosureExpr(
        const TrailingClosureExpr& tc, const VisitFunc& visitor);
    static OwnedPtr<IsExpr> CloneIsExpr(const IsExpr& ie, const VisitFunc& visitor);
    static OwnedPtr<AsExpr> CloneAsExpr(const AsExpr& ae, const VisitFunc& visitor);
    static OwnedPtr<OptionalExpr> CloneOptionalExpr(const OptionalExpr& oe, const VisitFunc& visitor);
    static OwnedPtr<OptionalChainExpr> CloneOptionalChainExpr(const OptionalChainExpr& oce, const VisitFunc& visitor);
    static OwnedPtr<LetPatternDestructor> CloneLetPatternDestructor(
        const LetPatternDestructor& ldp, const VisitFunc& visitor);
    static OwnedPtr<IfAvailableExpr> CloneIfAvailableExpr(const IfAvailableExpr& e, const VisitFunc& visitor);
    static OwnedPtr<ConstPattern> CloneConstPattern(const ConstPattern& cp, const VisitFunc& visitor);
    static OwnedPtr<VarPattern> CloneVarPattern(const VarPattern& vp, const VisitFunc& visitor);
    static OwnedPtr<TuplePattern> CloneTuplePattern(const TuplePattern& tp, const VisitFunc& visitor);
    static OwnedPtr<TypePattern> CloneTypePattern(const TypePattern& tp, const VisitFunc& visitor);
    static OwnedPtr<EnumPattern> CloneEnumPattern(const EnumPattern& ep, const VisitFunc& visitor);
    static OwnedPtr<ExceptTypePattern> CloneExceptTypePattern(const ExceptTypePattern& etp, const VisitFunc& visitor);
    static OwnedPtr<CommandTypePattern> CloneCommandTypePattern(
        const CommandTypePattern& ctp, const VisitFunc& visitor);
    static OwnedPtr<VarOrEnumPattern> CloneVarOrEnumPattern(const VarOrEnumPattern& vep, const VisitFunc& visitor);
    static OwnedPtr<Block> CloneBlock(const Block& block, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<ClassBody> CloneClassBody(const ClassBody& cb, const VisitFunc& visitor);
    static OwnedPtr<StructBody> CloneStructBody(const StructBody& sb, const VisitFunc& visitor);
    static OwnedPtr<InterfaceBody> CloneInterfaceBody(const InterfaceBody& ib, const VisitFunc& visitor);
    static OwnedPtr<GenericConstraint> CloneGenericConstraint(
        const GenericConstraint& gc, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<FuncBody> CloneFuncBody(const FuncBody& fb, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<VarDecl> CloneFuncParam(const FuncParam& fp, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<FuncParamList> CloneFuncParamList(
        const FuncParamList& fpl, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<FuncArg> CloneFuncArg(const FuncArg& fa, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<Annotation> CloneAnnotation(
        const Annotation& annotation, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<ImportSpec> CloneImportSpec(const ImportSpec& is, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<MatchCase> CloneMatchCase(const MatchCase& mc, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<MatchCaseOther> CloneMatchCaseOther(
        const MatchCaseOther& mco, const VisitFunc& visitor = DefaultVisitFunc);
    static OwnedPtr<Decl> CloneGenericParamDecl(const GenericParamDecl& gpd);
    static OwnedPtr<Decl> CloneVarWithPatternDecl(const VarWithPatternDecl& vwpd, const VisitFunc& visitor);
    static OwnedPtr<Decl> CloneVarDecl(const VarDecl& vd, const VisitFunc& visitor);
    static OwnedPtr<Decl> CloneFuncDecl(const FuncDecl& fd, const VisitFunc& visitor);
    static OwnedPtr<Decl> ClonePrimaryCtorDecl(const PrimaryCtorDecl& pcd, const VisitFunc& visitor);
    static OwnedPtr<Decl> ClonePropDecl(const PropDecl& pd, const VisitFunc& visitor);
    static OwnedPtr<Decl> CloneExtendDecl(const ExtendDecl& ed, const VisitFunc& visitor);
    static OwnedPtr<Decl> CloneMacroExpandDecl(const MacroExpandDecl& med);
    static OwnedPtr<Decl> CloneStructDecl(const StructDecl& sd, const VisitFunc& visitor);
    static OwnedPtr<Decl> CloneClassDecl(const ClassDecl& cd, const VisitFunc& visitor);
    static OwnedPtr<Decl> CloneInterfaceDecl(const InterfaceDecl& id, const VisitFunc& visitor);
    static OwnedPtr<Decl> CloneEnumDecl(const EnumDecl& ed, const VisitFunc& visitor);
    static OwnedPtr<Decl> CloneTypeAliasDecl(const TypeAliasDecl& tad, const VisitFunc& visitor);
};
} // namespace Cangjie::AST
#endif // CANGJIE_AST_CLONE_H
