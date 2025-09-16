// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements AST clone apis.
 */

#include "cangjie/AST/Clone.h"

#include <memory>

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/NodeX.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Basic/Print.h"

namespace Cangjie::AST {
using namespace Meta;

void DefaultVisitFunc(const Node& /* source */, const Node& /* target */)
{
}

void SetIsClonedSourceCode(const Node& /* source */, Node& target)
{
    target.EnableAttr(Attribute::IS_CLONED_SOURCE_CODE);
}

void CopyBasicInfo(Ptr<const Node> source, Ptr<Node> target)
{
    if (source && target) {
        target->begin = source->begin;
        target->end = source->end;
        target->curMacroCall = source->curMacroCall;
        target->isInMacroCall = source->isInMacroCall;
        target->comments = source->comments;
        CopyNodeScopeInfo(target, source);
    }
}

OwnedPtr<Generic> CloneGeneric(const Generic& generic, const VisitFunc& visitor)
{
    auto ret = MakeOwned<Generic>();
    for (auto& it : generic.typeParameters) {
        auto gpd = MakeOwned<GenericParamDecl>();
        auto d = ASTCloner::Clone(it.get(), visitor);
        gpd.reset(As<ASTKind::GENERIC_PARAM_DECL>(d.release()));
        ret->typeParameters.push_back(std::move(gpd));
    }
    for (auto& it : generic.genericConstraints) {
        ret->genericConstraints.push_back(ASTCloner::Clone(it.get(), visitor));
    }
    ret->leftAnglePos = generic.leftAnglePos;
    ret->rightAnglePos = generic.rightAnglePos;
    return ret;
}

MacroInvocation CloneMacroInvocation(const MacroInvocation& me)
{
    MacroInvocation mi;
    mi.fullName = me.fullName;
    mi.fullNameDotPos = me.fullNameDotPos;
    mi.identifier = me.identifier;
    mi.identifierPos = me.identifierPos;
    mi.leftSquarePos = me.leftSquarePos;
    mi.attrs = me.attrs;
    mi.rightSquarePos = me.rightSquarePos;
    mi.leftParenPos = me.leftParenPos;
    mi.args = me.args;
    mi.rightParenPos = me.rightParenPos;
    mi.newTokens = me.newTokens;
    mi.newTokensStr = me.newTokensStr;
    mi.parent = me.parent;
    mi.scope = me.scope;
    return mi;
}

namespace {
// This function should be called in each node which inherit Node directly.
void CopyNodeField(Ptr<Node> ret, const Node& e)
{
    ret->begin = e.begin;
    ret->end = e.end;
    ret->ty = e.ty;
    ret->curMacroCall = e.curMacroCall;
    ret->isInMacroCall = e.isInMacroCall;
    ret->comments = e.comments;
    CopyNodeScopeInfo(ret, &e);
    ret->CopyAttrs(e.GetAttrs());
}
} // namespace

// Clone nodes which inherit Node indirectly, should only be call by clone function which inherit Node directly.
OwnedPtr<Decl> ASTCloner::CloneGenericParamDecl(const GenericParamDecl& gpd)
{
    auto ret = MakeOwned<GenericParamDecl>();
    ret->outerDecl = gpd.outerDecl;
    ret->commaPos = gpd.commaPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<VarDecl> ASTCloner::CloneFuncParam(const FuncParam& fp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncParam>();
    ret->colonPos = fp.colonPos;
    ret->assignment = CloneExpr(fp.assignment.get(), visitor);
    if (fp.desugarDecl) {
        auto decl = CloneDecl(fp.desugarDecl.get(), visitor);
        ret->desugarDecl.reset(RawStaticCast<FuncDecl*>(decl.release()));
    }
    ret->isNamedParam = fp.isNamedParam;
    ret->isMemberParam = fp.isMemberParam;
    ret->commaPos = fp.commaPos;
    ret->notMarkPos = fp.notMarkPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneVarWithPatternDecl(const VarWithPatternDecl& vwpd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<VarWithPatternDecl>();
    ret->type = CloneType(vwpd.type.get(), visitor);
    ret->assignPos = vwpd.assignPos;
    ret->colonPos = vwpd.colonPos;
    ret->isVar = vwpd.isVar;
    ret->isConst = vwpd.isConst;
    ret->irrefutablePattern = ClonePattern(vwpd.irrefutablePattern.get(), visitor);
    ret->initializer = CloneExpr(vwpd.initializer.get(), visitor);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneVarDecl(const VarDecl& vd, const VisitFunc& visitor)
{
    auto ret = match(vd)([&visitor](const FuncParam& e) { return CloneFuncParam(e, visitor); },
        []() { return MakeOwned<VarDecl>(); });
    // Clone field in VarDecl.
    ret->type = CloneType(vd.type.get(), visitor);
    ret->colonPos = vd.colonPos;
    ret->initializer = CloneExpr(vd.initializer.get(), visitor);
    ret->assignPos = vd.assignPos;
    ret->isVar = vd.isVar;
    ret->isConst = vd.isConst;
    ret->isResourceVar = vd.isResourceVar;
    ret->isIdentifierCompilerAdd = vd.isIdentifierCompilerAdd;
    ret->parentPattern = vd.parentPattern;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneFuncDecl(const FuncDecl& fd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncDecl>();
    ret->leftParenPos = fd.leftParenPos;
    ret->rightParenPos = fd.rightParenPos;
    CJC_NULLPTR_CHECK(fd.funcBody);
    ret->funcBody = CloneNode(fd.funcBody.get(), visitor);
    ret->funcBody->funcDecl = ret.get(); // Reset funcDecl of funcBody to new function.
    ret->propDecl = fd.propDecl;
    ret->constructorCall = fd.constructorCall;
    ret->op = fd.op;
    ret->ownerFunc = fd.ownerFunc;
    ret->overflowStrategy = fd.overflowStrategy;
    ret->isFastNative = fd.isFastNative;
    ret->isGetter = fd.isGetter;
    ret->isSetter = fd.isSetter;
    ret->isInline = fd.isInline;
    ret->isConst = fd.isConst;
    ret->isFrozen = fd.isFrozen;
    ret->variadicArgIndex = fd.variadicArgIndex;
    ret->hasVariableLenArg = fd.hasVariableLenArg;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::ClonePrimaryCtorDecl(const PrimaryCtorDecl& pcd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<PrimaryCtorDecl>();
    CJC_NULLPTR_CHECK(pcd.funcBody);
    ret->funcBody = CloneNode(pcd.funcBody.get(), visitor);
    ret->overflowStrategy = pcd.overflowStrategy;
    ret->isFastNative = pcd.isFastNative;
    ret->isConst = pcd.isConst;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::ClonePropDecl(const PropDecl& pd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<PropDecl>();
    ret->type = CloneType(pd.type.get(), visitor);
    ret->colonPos = pd.colonPos;
    ret->leftCurlPos = pd.leftCurlPos;
    ret->rightCurlPos = pd.rightCurlPos;
    ret->isVar = pd.isVar;
    ret->isConst = pd.isConst;
    for (auto& func : pd.setters) {
        auto decl = CloneDecl(func.get(), visitor);
        auto fd = MakeOwned<FuncDecl>();
        fd.reset(As<ASTKind::FUNC_DECL>(decl.release()));
        fd->EnableAttr(Attribute::COMPILER_ADD);
        ret->setters.push_back(std::move(fd));
    }
    for (auto& func : pd.getters) {
        auto decl = CloneDecl(func.get(), visitor);
        auto fd = MakeOwned<FuncDecl>();
        fd.reset(As<ASTKind::FUNC_DECL>(decl.release()));
        fd->EnableAttr(Attribute::COMPILER_ADD);
        ret->getters.push_back(std::move(fd));
    }

    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<RefType> ASTCloner::CloneRefType(const RefType& type, const VisitFunc& visitor)
{
    auto ret = MakeOwned<RefType>();
    CopyNodeField(ret.get(), type);
    ret->ref = type.ref;
    ret->leftAnglePos = type.leftAnglePos;
    ret->typeArguments = std::vector<OwnedPtr<Type>>();
    for (auto& it : type.typeArguments) {
        ret->typeArguments.push_back(CloneType(it.get(), visitor));
    }
    ret->rightAnglePos = type.rightAnglePos;
    ret->commaPos = type.commaPos;
    ret->bitAndPos = type.bitAndPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneExtendDecl(const ExtendDecl& ed, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ExtendDecl>();
    ret->extendedType = CloneType(ed.extendedType.get(), visitor);
    for (auto& interface : ed.inheritedTypes) {
        ret->inheritedTypes.push_back(CloneType(interface.get(), visitor));
    }
    for (auto& func : ed.members) {
        ret->members.push_back(CloneDecl(func.get(), visitor));
    }
    if (ed.generic) {
        ret->generic = CloneGeneric(*ed.generic, visitor);
    }
    if (ed.bodyScope) {
        ret->bodyScope = MakeOwned<DummyBody>();
        CopyNodeScopeInfo(ret->bodyScope.get(), ed.bodyScope.get());
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneMacroExpandDecl(const MacroExpandDecl& med)
{
    auto ret = MakeOwned<MacroExpandDecl>();
    ret->invocation = CloneMacroInvocation(med.invocation);
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneStructDecl(const StructDecl& sd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<StructDecl>();
    for (auto& it : sd.inheritedTypes) {
        ret->inheritedTypes.push_back(CloneType(it.get(), visitor));
    }
    ret->body = CloneNode(sd.body.get(), visitor);
    if (sd.generic) {
        ret->generic = CloneGeneric(*sd.generic, visitor);
    }
    ret->upperBoundPos = sd.upperBoundPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneClassDecl(const ClassDecl& cd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ClassDecl>();
    for (auto& it : cd.inheritedTypes) {
        ret->inheritedTypes.push_back(CloneType(it.get(), visitor));
    }
    ret->body = CloneNode(cd.body.get(), visitor);
    if (cd.generic) {
        ret->generic = CloneGeneric(*cd.generic, visitor);
    }
    ret->upperBoundPos = cd.upperBoundPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneInterfaceDecl(const InterfaceDecl& id, const VisitFunc& visitor)
{
    auto ret = MakeOwned<InterfaceDecl>();

    for (auto& it : id.inheritedTypes) {
        ret->inheritedTypes.push_back(CloneType(it.get(), visitor));
    }
    ret->body = CloneNode(id.body.get(), visitor);

    if (id.generic) {
        ret->generic = CloneGeneric(*id.generic, visitor);
    }
    ret->upperBoundPos = id.upperBoundPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneEnumDecl(const EnumDecl& ed, const VisitFunc& visitor)
{
    auto ret = MakeOwned<EnumDecl>();
    for (auto& it : ed.inheritedTypes) {
        ret->inheritedTypes.push_back(CloneType(it.get(), visitor));
    }
    for (auto& it : ed.constructors) {
        ret->constructors.push_back(CloneDecl(it.get(), visitor));
    }
    for (auto& func : ed.members) {
        ret->members.push_back(CloneDecl(func.get(), visitor));
    }
    if (ed.generic) {
        ret->generic = CloneGeneric(*ed.generic, visitor);
    }
    ret->hasArguments = ed.hasArguments;
    if (ed.bodyScope) {
        ret->bodyScope = MakeOwned<DummyBody>();
        CopyNodeScopeInfo(ret->bodyScope.get(), ed.bodyScope.get());
    }
    ret->leftCurlPos = ed.leftCurlPos;
    ret->bitOrPosVector = ed.bitOrPosVector;
    ret->rightCurlPos = ed.rightCurlPos;
    ret->upperBoundPos = ed.upperBoundPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> ASTCloner::CloneTypeAliasDecl(const TypeAliasDecl& tad, const VisitFunc& visitor)
{
    auto ret = MakeOwned<TypeAliasDecl>();
    ret->assignPos = tad.assignPos;
    ret->type = CloneType(tad.type.get(), visitor);
    if (tad.generic != nullptr) {
        ret->generic = CloneGeneric(*tad.generic, visitor);
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<QualifiedType> ASTCloner::CloneQualifiedType(const QualifiedType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<QualifiedType>();
    ret->baseType = CloneType(node.baseType.get(), visitor);
    ret->field = node.field;
    ret->target = node.target;
    for (auto& it : node.typeArguments) {
        ret->typeArguments.push_back(CloneType(it.get(), visitor));
    }
    return ret;
}

OwnedPtr<ParenType> ASTCloner::CloneParenType(const ParenType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ParenType>();
    ret->type = CloneType(node.type.get(), visitor);
    ret->leftParenPos = node.leftParenPos;
    ret->rightParenPos = node.rightParenPos;
    return ret;
}

OwnedPtr<OptionType> ASTCloner::CloneOptionType(const OptionType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<OptionType>();
    ret->componentType = CloneType(node.componentType.get(), visitor);
    ret->questNum = node.questNum;
    ret->questVector = node.questVector;
    if (node.desugarType != nullptr) {
        ret->desugarType = Clone(node.desugarType.get(), visitor);
    }
    return ret;
}

OwnedPtr<FuncType> ASTCloner::CloneFuncType(const FuncType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncType>();
    for (auto& paramType : node.paramTypes) {
        ret->paramTypes.emplace_back(CloneType(paramType.get(), visitor));
    }
    ret->retType = CloneType(node.retType.get(), visitor);
    ret->isC = node.isC;
    ret->leftParenPos = node.leftParenPos;
    ret->rightParenPos = node.rightParenPos;
    ret->arrowPos = node.arrowPos;
    return ret;
}

OwnedPtr<TupleType> ASTCloner::CloneTupleType(const TupleType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<TupleType>();
    for (auto& it : node.fieldTypes) {
        ret->fieldTypes.push_back(CloneType(it.get(), visitor));
    }
    ret->leftParenPos = node.leftParenPos;
    ret->rightParenPos = node.rightParenPos;
    ret->commaPosVector = node.commaPosVector;
    return ret;
}

OwnedPtr<ConstantType> ASTCloner::CloneConstantType(const ConstantType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ConstantType>();
    ret->constantExpr = CloneExpr(node.constantExpr.get(), visitor);
    ret->dollarPos = node.dollarPos;
    return ret;
}

OwnedPtr<VArrayType> ASTCloner::CloneVArrayType(const VArrayType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<VArrayType>();
    ret->leftAnglePos = node.leftAnglePos;
    ret->typeArgument = CloneType(node.typeArgument.get(), visitor);
    ret->constantType = CloneType(node.constantType.get(), visitor);
    ret->rightAnglePos = node.rightAnglePos;
    return ret;
}

// Clone nodes which inherit Node directly.
OwnedPtr<Type> ASTCloner::CloneType(Ptr<Type> type, const VisitFunc& visitor)
{
    if (!type) {
        return OwnedPtr<Type>();
    }
    auto ret = match(*type)([&visitor](const RefType& e) { return OwnedPtr<Type>(CloneRefType(e, visitor)); },
        [](const PrimitiveType& e) { return OwnedPtr<Type>(MakeOwned<PrimitiveType>(e)); },
        [&visitor](const ParenType& e) { return OwnedPtr<Type>(CloneParenType(e, visitor)); },
        [&visitor](const QualifiedType& e) { return OwnedPtr<Type>(CloneQualifiedType(e, visitor)); },
        [&visitor](const OptionType& e) { return OwnedPtr<Type>(CloneOptionType(e, visitor)); },
        [&visitor](const FuncType& e) { return OwnedPtr<Type>(CloneFuncType(e, visitor)); },
        [&visitor](const TupleType& e) { return OwnedPtr<Type>(CloneTupleType(e, visitor)); },
        [&visitor](const ConstantType& e) { return OwnedPtr<Type>(CloneConstantType(e, visitor)); },
        [&visitor](const VArrayType& e) { return OwnedPtr<Type>(CloneVArrayType(e, visitor)); },
        [](const ThisType& e) { return OwnedPtr<Type>(MakeOwned<ThisType>(e)); },
        [](const InvalidType& e) { return OwnedPtr<Type>(MakeOwned<InvalidType>(e)); },
        [](const Type& e) { return OwnedPtr<Type>(MakeOwned<Type>(e)); },
        []() {
            // Invalid case.
            return MakeOwned<Type>();
        });
    CJC_ASSERT(ret && ret->astKind == type->astKind);
    ret->commaPos = type->commaPos;
    ret->bitAndPos = type->bitAndPos;

    ret->typeParameterName = type->typeParameterName;
    ret->typeParameterNameIsRawId = type->typeParameterNameIsRawId;
    ret->typePos = type->typePos;
    ret->colonPos = type->colonPos;

    CopyNodeField(ret.get(), *type);
    visitor(*type, *ret);
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<MacroExpandExpr> ASTCloner::CloneMacroExpandExpr(const MacroExpandExpr& mee, const VisitFunc& visitor)
{
    auto expr = MakeOwned<MacroExpandExpr>();
    expr->identifier = mee.identifier;
    expr->invocation = CloneMacroInvocation(mee.invocation);
    for (auto& anno : mee.annotations) {
        expr->annotations.emplace_back(CloneNode(anno.get(), visitor));
    }
    expr->modifiers.insert(mee.modifiers.begin(), mee.modifiers.end());
    return expr;
}

OwnedPtr<TokenPart> ASTCloner::CloneTokenPart(const TokenPart& tp, const VisitFunc& /* visitor */)
{
    auto expr = MakeOwned<TokenPart>();
    expr->tokens.assign(tp.tokens.begin(), tp.tokens.end());
    return expr;
}

OwnedPtr<QuoteExpr> ASTCloner::CloneQuoteExpr(const QuoteExpr& qe, const VisitFunc& visitor)
{
    auto expr = MakeOwned<QuoteExpr>();
    for (auto& i : qe.exprs) {
        expr->exprs.push_back(CloneExpr(i.get(), visitor));
    }
    expr->quotePos = qe.quotePos;
    expr->leftParenPos = qe.leftParenPos;
    expr->rightParenPos = qe.rightParenPos;
    return expr;
}

OwnedPtr<IfExpr> ASTCloner::CloneIfExpr(const IfExpr& ie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<IfExpr>();
    expr->ifPos = ie.ifPos;
    expr->leftParenPos = ie.leftParenPos;
    expr->condExpr = CloneExpr(ie.condExpr.get(), visitor);
    expr->rightParenPos = ie.rightParenPos;
    auto n = CloneExpr(ie.thenBody.get(), visitor);
    expr->thenBody.reset(As<ASTKind::BLOCK>(n.release()));
    expr->hasElse = ie.hasElse;
    expr->isElseIf = ie.isElseIf;
    expr->elsePos = ie.elsePos;
    expr->elseBody = CloneExpr(ie.elseBody.get(), visitor);
    return expr;
}

OwnedPtr<TryExpr> ASTCloner::CloneTryExpr(const TryExpr& te, const VisitFunc& visitor)
{
    auto expr = MakeOwned<TryExpr>();
    expr->tryPos = te.tryPos;
    expr->lParen = te.lParen;
    for (auto& resource : te.resourceSpec) {
        expr->resourceSpec.push_back(
            OwnedPtr<VarDecl>(As<ASTKind::VAR_DECL>(CloneDecl(resource.get(), visitor).release())));
    }
    expr->isDesugaredFromSyncBlock = te.isDesugaredFromSyncBlock;
    expr->isDesugaredFromTryWithResources = te.isDesugaredFromTryWithResources;
    expr->isIllegalResourceSpec = te.isIllegalResourceSpec;
    expr->tryBlock = CloneExpr(te.tryBlock.get(), visitor);
    for (size_t i = 0; i < te.catchPosVector.size(); i++) {
        expr->catchPosVector.push_back(te.catchPosVector[i]);
    }
    CJC_ASSERT(te.catchBlocks.size() == te.catchPatterns.size());
    for (size_t i = 0; i < te.catchBlocks.size(); i++) {
        expr->catchBlocks.push_back(CloneExpr(te.catchBlocks[i].get(), visitor));
        expr->catchPatterns.push_back(ClonePattern(te.catchPatterns[i].get(), visitor));
    }

    for (const auto& handler : te.handlers) {
        auto cloned = Handler();
        cloned.pos = handler.pos;
        cloned.commandPattern = ClonePattern(handler.commandPattern.get(), visitor);
        cloned.block = CloneExpr(handler.block.get(), visitor);
        if (handler.desugaredLambda) {
            cloned.desugaredLambda = CloneLambdaExpr(*handler.desugaredLambda, visitor);
            CopyNodeField(cloned.desugaredLambda, *handler.desugaredLambda);
        }
        expr->handlers.emplace_back(std::move(cloned));
    }
    expr->finallyPos = te.finallyPos;
    expr->finallyBlock = CloneExpr(te.finallyBlock.get(), visitor);
    expr->resourceSpecCommaPos = te.resourceSpecCommaPos;
    expr->rParen = te.rParen;
    expr->catchLParenPosVector = te.catchLParenPosVector;
    expr->catchRParenPosVector = te.catchRParenPosVector;
    if (te.tryLambda) {
        expr->tryLambda = CloneExpr(te.tryLambda.get(), visitor);
        CopyNodeField(expr->tryLambda, *te.tryLambda);
    }
    if (te.finallyLambda) {
        expr->finallyLambda = CloneExpr(te.finallyLambda.get(), visitor);
        CopyNodeField(expr->finallyLambda, *te.finallyLambda);
    }
    return expr;
}

OwnedPtr<ThrowExpr> ASTCloner::CloneThrowExpr(const ThrowExpr& te, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ThrowExpr>();
    expr->throwPos = te.throwPos;
    expr->expr = CloneExpr(te.expr.get(), visitor);
    return expr;
}

OwnedPtr<PerformExpr> ASTCloner::ClonePerformExpr(const PerformExpr& pe, const VisitFunc& visitor)
{
    auto expr = MakeOwned<PerformExpr>();
    expr->performPos = pe.performPos;
    expr->expr = CloneExpr(pe.expr.get(), visitor);
    return expr;
}

OwnedPtr<ResumeExpr> ASTCloner::CloneResumeExpr(const ResumeExpr& re, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ResumeExpr>();
    expr->resumePos = re.resumePos;
    expr->withPos = re.withPos;
    expr->withExpr = CloneExpr(re.withExpr.get(), visitor);
    expr->throwingPos = re.throwingPos;
    expr->throwingExpr = CloneExpr(re.throwingExpr.get(), visitor);
    return expr;
}

OwnedPtr<ReturnExpr> ASTCloner::CloneReturnExpr(const ReturnExpr& re, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ReturnExpr>();
    expr->returnPos = re.returnPos;
    expr->expr = CloneExpr(re.expr.get(), visitor);
    expr->refFuncBody = re.refFuncBody;
    return expr;
}

OwnedPtr<WhileExpr> ASTCloner::CloneWhileExpr(const WhileExpr& we, const VisitFunc& visitor)
{
    auto expr = MakeOwned<WhileExpr>();
    expr->whilePos = we.whilePos;
    expr->leftParenPos = we.leftParenPos;
    expr->condExpr = CloneExpr(we.condExpr.get(), visitor);
    expr->body = CloneExpr(we.body.get(), visitor);
    expr->rightParenPos = we.rightParenPos;
    return expr;
}

OwnedPtr<DoWhileExpr> ASTCloner::CloneDoWhileExpr(const DoWhileExpr& dwe, const VisitFunc& visitor)
{
    auto expr = MakeOwned<DoWhileExpr>();
    expr->doPos = dwe.doPos;
    expr->body = CloneExpr(dwe.body.get(), visitor);
    expr->whilePos = dwe.whilePos;
    expr->leftParenPos = dwe.leftParenPos;
    expr->condExpr = CloneExpr(dwe.condExpr.get(), visitor);
    expr->rightParenPos = dwe.rightParenPos;
    return expr;
}

OwnedPtr<AssignExpr> ASTCloner::CloneAssignExpr(const AssignExpr& ae, const VisitFunc& visitor)
{
    auto expr = MakeOwned<AssignExpr>();
    expr->leftValue = CloneExpr(ae.leftValue.get(), visitor);
    expr->op = ae.op;
    expr->rightExpr = CloneExpr(ae.rightExpr.get(), visitor);
    expr->isCompound = ae.isCompound;
    expr->assignPos = ae.assignPos;
    return expr;
}

OwnedPtr<IncOrDecExpr> ASTCloner::CloneIncOrDecExpr(const IncOrDecExpr& ide, const VisitFunc& visitor)
{
    auto expr = MakeOwned<IncOrDecExpr>();
    expr->op = ide.op;
    expr->operatorPos = ide.operatorPos;
    expr->expr = CloneExpr(ide.expr.get(), visitor);
    return expr;
}

OwnedPtr<UnaryExpr> ASTCloner::CloneUnaryExpr(const UnaryExpr& ue, const VisitFunc& visitor)
{
    auto expr = MakeOwned<UnaryExpr>();
    expr->op = ue.op;
    expr->expr = CloneExpr(ue.expr.get(), visitor);
    expr->operatorPos = ue.operatorPos;
    return expr;
}

OwnedPtr<BinaryExpr> ASTCloner::CloneBinaryExpr(const BinaryExpr& be, const VisitFunc& visitor)
{
    auto expr = MakeOwned<BinaryExpr>();
    expr->op = be.op;
    expr->leftExpr = CloneExpr(be.leftExpr.get(), visitor);
    expr->rightExpr = CloneExpr(be.rightExpr.get(), visitor);
    expr->operatorPos = be.operatorPos;
    return expr;
}

OwnedPtr<RangeExpr> ASTCloner::CloneRangeExpr(const RangeExpr& re, const VisitFunc& visitor)
{
    auto expr = MakeOwned<RangeExpr>();
    expr->startExpr = CloneExpr(re.startExpr.get(), visitor);
    expr->rangePos = re.rangePos;
    expr->stopExpr = CloneExpr(re.stopExpr.get(), visitor);
    expr->colonPos = re.colonPos;
    expr->stepExpr = CloneExpr(re.stepExpr.get(), visitor);
    expr->isClosed = re.isClosed;
    expr->decl = re.decl;
    return expr;
}

OwnedPtr<SubscriptExpr> ASTCloner::CloneSubscriptExpr(const SubscriptExpr& se, const VisitFunc& visitor)
{
    auto expr = MakeOwned<SubscriptExpr>();
    expr->baseExpr = CloneExpr(se.baseExpr.get(), visitor);
    expr->leftParenPos = se.leftParenPos;
    for (auto& it : se.indexExprs) {
        expr->indexExprs.push_back(CloneExpr(it.get(), visitor));
    }
    expr->rightParenPos = se.rightParenPos;
    expr->isTupleAccess = se.isTupleAccess;
    return expr;
}

OwnedPtr<MemberAccess> ASTCloner::CloneMemberAccess(const MemberAccess& ma, const VisitFunc& visitor)
{
    auto expr = MakeOwned<MemberAccess>();
    expr->baseExpr = CloneExpr(ma.baseExpr.get(), visitor);
    expr->dotPos = ma.dotPos;
    expr->field = ma.field;
    expr->target = ma.target;
    expr->targets = ma.targets;
    expr->isPattern = ma.isPattern;
    expr->isAlone = ma.isAlone;
    expr->instTys = ma.instTys;
    expr->matchedParentTy = ma.matchedParentTy;
    expr->callOrPattern = ma.callOrPattern;
    expr->foundUpperBoundMap = ma.foundUpperBoundMap;
    expr->isExposedAccess = ma.isExposedAccess;
    for (auto& it : ma.typeArguments) {
        expr->typeArguments.push_back(CloneType(it.get(), visitor));
    }
    return expr;
}

OwnedPtr<CallExpr> ASTCloner::CloneCallExpr(const CallExpr& ce, const VisitFunc& visitor)
{
    auto expr = MakeOwned<CallExpr>();
    expr->baseFunc = CloneExpr(ce.baseFunc.get(), visitor);
    expr->leftParenPos = ce.leftParenPos;
    expr->needCheckToTokens = ce.needCheckToTokens;
    std::unordered_map<Ptr<FuncArg>, Ptr<FuncArg>> cloneTable; // For replace desugarArgs
    for (auto& it : ce.args) {
        expr->args.push_back(CloneNode(it.get(), visitor));
        cloneTable[it.get()] = expr->args.back().get();
    }
    if (ce.desugarArgs.has_value()) {
        expr->defaultArgs = std::vector<OwnedPtr<FuncArg>>();
        for (auto& it : ce.defaultArgs) {
            expr->defaultArgs.push_back(CloneNode(it.get(), visitor));
            cloneTable[it.get()] = expr->defaultArgs.back().get();
        }
        expr->desugarArgs = std::vector<Ptr<FuncArg>>();
        for (auto& it : ce.desugarArgs.value()) {
            expr->desugarArgs.value().push_back(cloneTable[it]);
        }
    }
    expr->rightParenPos = ce.rightParenPos;
    expr->resolvedFunction = ce.resolvedFunction;
    expr->callKind = ce.callKind;
    expr->sugarKind = ce.sugarKind;
    if (auto ma = DynamicCast<MemberAccess*>(expr->baseFunc.get()); ma) {
        ma->callOrPattern = expr.get();
    }
    return expr;
}

OwnedPtr<ParenExpr> ASTCloner::CloneParenExpr(const ParenExpr& pe, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ParenExpr>();
    expr->leftParenPos = pe.leftParenPos;
    expr->expr = CloneExpr(pe.expr.get(), visitor);
    expr->rightParenPos = pe.rightParenPos;
    return expr;
}

OwnedPtr<LambdaExpr> ASTCloner::CloneLambdaExpr(const LambdaExpr& le, const VisitFunc& visitor)
{
    auto expr = MakeOwned<LambdaExpr>(CloneNode(le.funcBody.get(), visitor));
    if (le.TestAttr(Attribute::MOCK_SUPPORTED)) {
        expr->EnableAttr(Attribute::MOCK_SUPPORTED);
    }
    return expr;
}

OwnedPtr<LitConstExpr> ASTCloner::CloneLitConstExpr(const LitConstExpr& lce, const VisitFunc& visitor)
{
    auto expr = MakeOwned<LitConstExpr>(lce.kind, lce.stringValue);
    expr->codepoint = lce.codepoint;
    expr->delimiterNum = lce.delimiterNum;
    expr->stringKind = lce.stringKind;
    expr->rawString = lce.rawString;
    if (lce.ref) {
        expr->ref = CloneRefType(*lce.ref, visitor);
        visitor(*lce.ref, *expr->ref);
    }
    if (lce.siExpr) {
        expr->siExpr = CloneExpr(lce.siExpr.get(), visitor);
    }
    return expr;
}

OwnedPtr<InterpolationExpr> ASTCloner::CloneInterpolationExpr(
    const InterpolationExpr& ie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<InterpolationExpr>();
    expr->rawString = ie.rawString;
    expr->dollarPos = ie.dollarPos;
    expr->block = CloneExpr(ie.block.get(), visitor);
    return expr;
}

OwnedPtr<StrInterpolationExpr> ASTCloner::CloneStrInterpolationExpr(
    const StrInterpolationExpr& sie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<StrInterpolationExpr>();
    expr->rawString = sie.rawString;
    expr->strParts = sie.strParts;
    for (auto& it : sie.strPartExprs) {
        expr->strPartExprs.push_back(CloneExpr(it.get(), visitor));
    }
    return expr;
}

OwnedPtr<ArrayLit> ASTCloner::CloneArrayLit(const ArrayLit& al, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ArrayLit>();
    expr->leftSquarePos = al.leftSquarePos;
    for (auto& it : al.children) {
        expr->children.push_back(CloneExpr(it.get(), visitor));
    }
    expr->commaPosVector = al.commaPosVector;
    expr->rightSquarePos = al.rightSquarePos;
    expr->initFunc = al.initFunc;
    return expr;
}

OwnedPtr<ArrayExpr> ASTCloner::CloneArrayExpr(const ArrayExpr& ae, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ArrayExpr>();
    expr->type = CloneType(ae.type.get(), visitor);
    expr->leftParenPos = ae.leftParenPos;
    expr->args.resize(ae.args.size());
    for (size_t i = 0; i < ae.args.size(); ++i) {
        expr->args[i] = CloneNode(ae.args[i].get(), visitor);
    }
    expr->commaPosVector = ae.commaPosVector;
    expr->rightParenPos = ae.rightParenPos;
    expr->initFunc = ae.initFunc;
    expr->isValueArray = ae.isValueArray;
    return expr;
}

OwnedPtr<PointerExpr> ASTCloner::ClonePointerExpr(const PointerExpr& ptre, const VisitFunc& visitor)
{
    auto expr = MakeOwned<PointerExpr>();
    expr->type = CloneType(ptre.type.get(), visitor);
    expr->arg = CloneNode(ptre.arg.get(), visitor);
    return expr;
}

OwnedPtr<TupleLit> ASTCloner::CloneTupleLit(const TupleLit& tl, const VisitFunc& visitor)
{
    auto expr = MakeOwned<TupleLit>();
    expr->leftParenPos = tl.leftParenPos;
    for (auto& it : tl.children) {
        expr->children.push_back(CloneExpr(it.get(), visitor));
    }
    expr->commaPosVector = tl.commaPosVector;
    expr->rightParenPos = tl.rightParenPos;
    return expr;
}

OwnedPtr<RefExpr> ASTCloner::CloneRefExpr(const RefExpr& re, const VisitFunc& visitor)
{
    auto expr = CreateRefExpr(re.ref.identifier);
    expr->leftAnglePos = re.leftAnglePos;
    expr->ref.target = re.ref.target;
    expr->ref.targets = re.ref.targets;
    for (auto& it : re.typeArguments) {
        expr->typeArguments.push_back(CloneType(it.get(), visitor));
    }
    expr->rightAnglePos = re.rightAnglePos;
    expr->isSuper = re.isSuper;
    expr->isThis = re.isThis;
    expr->isAlone = re.isAlone;
    expr->instTys = re.instTys;
    expr->matchedParentTy = re.matchedParentTy;
    expr->callOrPattern = re.callOrPattern;
    return expr;
}

OwnedPtr<ForInExpr> ASTCloner::CloneForInExpr(const ForInExpr& fie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ForInExpr>();
    expr->leftParenPos = fie.leftParenPos;
    expr->rightParenPos = fie.rightParenPos;
    expr->pattern = ClonePattern(fie.pattern.get(), visitor);
    expr->inPos = fie.inPos;
    expr->inExpression = CloneExpr(fie.inExpression.get(), visitor);
    expr->wherePos = fie.wherePos;
    expr->patternGuard = CloneExpr(fie.patternGuard.get(), visitor);
    expr->patternInDesugarExpr = fie.patternInDesugarExpr;
    expr->body = CloneExpr(fie.body.get(), visitor);
    expr->forInKind = fie.forInKind;
    return expr;
}

OwnedPtr<MatchExpr> ASTCloner::CloneMatchExpr(const MatchExpr& me, const VisitFunc& visitor)
{
    auto expr = MakeOwned<MatchExpr>();
    expr->matchMode = me.matchMode;
    expr->sugarKind = me.sugarKind;
    expr->selector = CloneExpr(me.selector.get(), visitor);
    for (auto& i : me.matchCases) {
        expr->matchCases.push_back(CloneNode(i.get(), visitor));
    }
    for (auto& i : me.matchCaseOthers) {
        expr->matchCaseOthers.push_back(CloneNode(i.get(), visitor));
    }
    expr->leftParenPos = me.leftParenPos;
    expr->rightParenPos = me.rightParenPos;
    expr->leftCurlPos = me.leftCurlPos;
    expr->rightCurlPos = me.rightCurlPos;
    return expr;
}

OwnedPtr<JumpExpr> ASTCloner::CloneJumpExpr(const JumpExpr& je)
{
    auto expr = MakeOwned<JumpExpr>();
    expr->isBreak = je.isBreak;
    expr->refLoop = je.refLoop;
    return expr;
}

OwnedPtr<TypeConvExpr> ASTCloner::CloneTypeConvExpr(const TypeConvExpr& tce, const VisitFunc& visitor)
{
    auto expr = MakeOwned<TypeConvExpr>();
    expr->type = OwnedPtr<Type>(CloneType(tce.type.get(), visitor).release());
    expr->expr = CloneExpr(tce.expr.get(), visitor);
    expr->leftParenPos = tce.leftParenPos;
    expr->rightParenPos = tce.rightParenPos;
    return expr;
}

OwnedPtr<SpawnExpr> ASTCloner::CloneSpawnExpr(const SpawnExpr& se, const VisitFunc& visitor)
{
    auto expr = MakeOwned<SpawnExpr>();
    expr->task = CloneExpr(se.task.get(), visitor);
    expr->arg = CloneExpr(se.arg.get(), visitor);
    if (se.futureObj) {
        auto obj = CloneDecl(se.futureObj.get(), visitor);
        expr->futureObj = OwnedPtr<VarDecl>(As<ASTKind::VAR_DECL>(obj.release()));
    }
    expr->spawnPos = se.spawnPos;
    expr->leftParenPos = se.leftParenPos;
    expr->rightParenPos = se.rightParenPos;
    return expr;
}

OwnedPtr<SynchronizedExpr> ASTCloner::CloneSynchronizedExpr(const SynchronizedExpr& se, const VisitFunc& visitor)
{
    auto expr = MakeOwned<SynchronizedExpr>();
    expr->mutex = CloneExpr(se.mutex.get(), visitor);
    expr->body = CloneExpr(se.body.get(), visitor);
    expr->syncPos = se.syncPos;
    expr->leftParenPos = se.leftParenPos;
    expr->rightParenPos = se.rightParenPos;
    return expr;
}

OwnedPtr<InvalidExpr> ASTCloner::CloneInvalidExpr(const InvalidExpr& ie)
{
    auto expr = MakeOwned<InvalidExpr>(ie.begin);
    expr->value = ie.value;
    return expr;
}

OwnedPtr<TrailingClosureExpr> ASTCloner::CloneTrailingClosureExpr(
    const TrailingClosureExpr& tc, const VisitFunc& visitor)
{
    auto expr = MakeOwned<TrailingClosureExpr>();
    expr->leftLambda = tc.leftLambda;
    expr->rightLambda = tc.rightLambda;
    if (tc.expr) {
        expr->expr = CloneExpr(tc.expr.get(), visitor);
    }
    if (tc.lambda) {
        expr->lambda = CloneExpr(tc.lambda.get(), visitor);
    }
    return expr;
}

OwnedPtr<IsExpr> ASTCloner::CloneIsExpr(const IsExpr& ie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<IsExpr>();
    expr->leftExpr = CloneExpr(ie.leftExpr.get(), visitor);
    expr->isType = CloneType(ie.isType.get(), visitor);
    expr->isPos = ie.isPos;
    return expr;
}

OwnedPtr<AsExpr> ASTCloner::CloneAsExpr(const AsExpr& ae, const VisitFunc& visitor)
{
    auto expr = MakeOwned<AsExpr>();
    expr->leftExpr = CloneExpr(ae.leftExpr.get(), visitor);
    expr->asType = CloneType(ae.asType.get(), visitor);
    expr->asPos = ae.asPos;
    return expr;
}

OwnedPtr<OptionalExpr> ASTCloner::CloneOptionalExpr(const OptionalExpr& oe, const VisitFunc& visitor)
{
    auto expr = MakeOwned<OptionalExpr>();
    expr->baseExpr = CloneExpr(oe.baseExpr.get(), visitor);
    expr->questPos = oe.questPos;
    return expr;
}

OwnedPtr<OptionalChainExpr> ASTCloner::CloneOptionalChainExpr(const OptionalChainExpr& oce, const VisitFunc& visitor)
{
    auto expr = MakeOwned<OptionalChainExpr>();
    expr->expr = CloneExpr(oce.expr.get(), visitor);
    return expr;
}

OwnedPtr<LetPatternDestructor> ASTCloner::CloneLetPatternDestructor(
    const LetPatternDestructor& ldp, const VisitFunc& visitor)
{
    auto expr = MakeOwned<LetPatternDestructor>();
    expr->backarrowPos = ldp.backarrowPos;
    for (auto& p : ldp.patterns) {
        expr->patterns.push_back(ClonePattern(p.get(), visitor));
    }
    expr->orPos = ldp.orPos;
    expr->initializer = CloneExpr(ldp.initializer.get(), visitor);
    return expr;
}

OwnedPtr<IfAvailableExpr> ASTCloner::CloneIfAvailableExpr(const IfAvailableExpr& e, const VisitFunc& visitor)
{
    auto arg = CloneFuncArg(*e.GetArg(), visitor);
    auto lambda1 = CloneLambdaExpr(*e.GetLambda1(), visitor);
    auto lambda2 = CloneLambdaExpr(*e.GetLambda2(), visitor);
    auto expr = MakeOwned<IfAvailableExpr>(std::move(arg), std::move(lambda1), std::move(lambda2));
    return expr;
}

/**
 * NOTE: To guarantee the members of Expr is copied, the sub method should not be called outside 'CloneExpr'.
 */
template <typename ExprT> OwnedPtr<ExprT> ASTCloner::CloneExpr(Ptr<ExprT> expr, const VisitFunc& visitor)
{
    if (!expr) {
        return OwnedPtr<ExprT>();
    }
    auto clonedExpr = match(*expr)(
        // PrimitiveExpr, AdjointExpr are ignored.
        [&visitor](const IfExpr& ie) { return OwnedPtr<Expr>(CloneIfExpr(ie, visitor)); },
        [](const PrimitiveTypeExpr& pte) { return OwnedPtr<Expr>(MakeOwned<PrimitiveTypeExpr>(pte.typeKind)); },
        [&visitor](const MacroExpandExpr& mee) { return OwnedPtr<Expr>(CloneMacroExpandExpr(mee, visitor)); },
        [&visitor](const TokenPart& tp) { return OwnedPtr<Expr>(CloneTokenPart(tp, visitor)); },
        [&visitor](const QuoteExpr& qe) { return OwnedPtr<Expr>(CloneQuoteExpr(qe, visitor)); },
        [&visitor](const TryExpr& te) { return OwnedPtr<Expr>(CloneTryExpr(te, visitor)); },
        [&visitor](const ThrowExpr& te) { return OwnedPtr<Expr>(CloneThrowExpr(te, visitor)); },
        [&visitor](const PerformExpr& te) { return OwnedPtr<Expr>(ClonePerformExpr(te, visitor)); },
        [&visitor](const ResumeExpr& re) { return OwnedPtr<Expr>(CloneResumeExpr(re, visitor)); },
        [&visitor](const ReturnExpr& re) { return OwnedPtr<Expr>(CloneReturnExpr(re, visitor)); },
        [&visitor](const WhileExpr& we) { return OwnedPtr<Expr>(CloneWhileExpr(we, visitor)); },
        [&visitor](const DoWhileExpr& dwe) { return OwnedPtr<Expr>(CloneDoWhileExpr(dwe, visitor)); },
        [&visitor](const AssignExpr& ae) { return OwnedPtr<Expr>(CloneAssignExpr(ae, visitor)); },
        [&visitor](const IncOrDecExpr& ide) { return OwnedPtr<Expr>(CloneIncOrDecExpr(ide, visitor)); },
        [&visitor](const UnaryExpr& ue) { return OwnedPtr<Expr>(CloneUnaryExpr(ue, visitor)); },
        [&visitor](const BinaryExpr& be) { return OwnedPtr<Expr>(CloneBinaryExpr(be, visitor)); },
        [&visitor](const RangeExpr& re) { return OwnedPtr<Expr>(CloneRangeExpr(re, visitor)); },
        [&visitor](const SubscriptExpr& se) { return OwnedPtr<Expr>(CloneSubscriptExpr(se, visitor)); },
        [&visitor](const MemberAccess& ma) { return OwnedPtr<Expr>(CloneMemberAccess(ma, visitor)); },
        [&visitor](const CallExpr& ce) { return OwnedPtr<Expr>(CloneCallExpr(ce, visitor)); },
        [&visitor](const ParenExpr& pe) { return OwnedPtr<Expr>(CloneParenExpr(pe, visitor)); },
        [&visitor](const LambdaExpr& le) { return OwnedPtr<Expr>(CloneLambdaExpr(le, visitor)); },
        [&visitor](const LitConstExpr& lce) { return OwnedPtr<Expr>(CloneLitConstExpr(lce, visitor)); },
        [&visitor](const ArrayLit& al) { return OwnedPtr<Expr>(CloneArrayLit(al, visitor)); },
        [&visitor](const ArrayExpr& asl) { return OwnedPtr<Expr>(CloneArrayExpr(asl, visitor)); },
        [&visitor](const PointerExpr& ptre) { return OwnedPtr<Expr>(ClonePointerExpr(ptre, visitor)); },
        [&visitor](const TupleLit& tl) { return OwnedPtr<Expr>(CloneTupleLit(tl, visitor)); },
        [&visitor](const RefExpr& re) { return OwnedPtr<Expr>(CloneRefExpr(re, visitor)); },
        [&visitor](const ForInExpr& fie) { return OwnedPtr<Expr>(CloneForInExpr(fie, visitor)); },
        [&visitor](const MatchExpr& me) { return OwnedPtr<Expr>(CloneMatchExpr(me, visitor)); },
        [](const JumpExpr& je) { return OwnedPtr<Expr>(CloneJumpExpr(je)); },
        [&visitor](const TypeConvExpr& e) { return OwnedPtr<Expr>(CloneTypeConvExpr(e, visitor)); },
        [&visitor](const SpawnExpr& se) { return OwnedPtr<Expr>(CloneSpawnExpr(se, visitor)); },
        [&visitor](const SynchronizedExpr& se) { return OwnedPtr<Expr>(CloneSynchronizedExpr(se, visitor)); },
        [](const InvalidExpr& ie) { return OwnedPtr<Expr>(CloneInvalidExpr(ie)); },
        [&visitor](const Block& b) { return OwnedPtr<Expr>(CloneBlock(b, visitor)); },
        [&visitor](const InterpolationExpr& ie) { return OwnedPtr<Expr>(CloneInterpolationExpr(ie, visitor)); },
        [&visitor](const StrInterpolationExpr& sie) { return OwnedPtr<Expr>(CloneStrInterpolationExpr(sie, visitor)); },
        [&visitor](const TrailingClosureExpr& tc) { return OwnedPtr<Expr>(CloneTrailingClosureExpr(tc, visitor)); },
        [&visitor](const IsExpr& ie) { return OwnedPtr<Expr>(CloneIsExpr(ie, visitor)); },
        [&visitor](const AsExpr& ae) { return OwnedPtr<Expr>(CloneAsExpr(ae, visitor)); },
        [&visitor](const OptionalExpr& oe) { return OwnedPtr<Expr>(CloneOptionalExpr(oe, visitor)); },
        [&visitor](const OptionalChainExpr& oe) { return OwnedPtr<Expr>(CloneOptionalChainExpr(oe, visitor)); },
        [](const WildcardExpr& /* we */) { return OwnedPtr<Expr>(MakeOwned<WildcardExpr>()); },
        [&visitor](const LetPatternDestructor& ld) { return OwnedPtr<Expr>(CloneLetPatternDestructor(ld, visitor)); },
        [&visitor](const IfAvailableExpr& ie) { return OwnedPtr<Expr>(CloneIfAvailableExpr(ie, visitor)); },
        [&expr]() {
            // Invalid and ignored cases.
            auto invalidExpr = MakeOwned<InvalidExpr>(expr->begin);
            return OwnedPtr<Expr>(invalidExpr.release());
        });
    CJC_ASSERT(clonedExpr);
    if (clonedExpr->astKind != ASTKind::INVALID_EXPR) {
        CopyNodeField(clonedExpr.get(), *expr);
        // Clone field in Expr.
        clonedExpr->isConst = expr->isConst;
        clonedExpr->isBaseFunc = expr->isBaseFunc;
        clonedExpr->isInFlowExpr = expr->isInFlowExpr;
        clonedExpr->constNumValue = expr->constNumValue;
        clonedExpr->hasSemi = expr->hasSemi;
        clonedExpr->mapExpr = (expr->mapExpr == expr) ? clonedExpr.get() : expr->mapExpr;
        clonedExpr->sourceExpr = expr->sourceExpr;
        clonedExpr->overflowStrategy = expr->overflowStrategy;
        if (expr->desugarExpr && !clonedExpr->desugarExpr) {
            clonedExpr->desugarExpr = CloneExpr(expr->desugarExpr.get(), visitor);
        }
        clonedExpr->EnableAttr(Attribute::COMPILER_ADD);
        visitor(*expr, *clonedExpr);
    }
    auto result = DynamicCast<ExprT*>(clonedExpr.release());
    CJC_NULLPTR_CHECK(result);
    return OwnedPtr<ExprT>(result);
}

/**
 * NOTE: To guarantee the members of Decl is copied, the sub method should not be called outside 'CloneDecl'.
 */
OwnedPtr<Decl> ASTCloner::CloneDecl(Ptr<Decl> decl, const VisitFunc& visitor)
{
    if (!decl) {
        return OwnedPtr<Decl>();
    }
    auto ret = match(*decl)([](const GenericParamDecl& e) { return CloneGenericParamDecl(e); },
        [&visitor](const PropDecl& e) { return ClonePropDecl(e, visitor); },
        [&visitor](const VarDecl& e) { return CloneVarDecl(e, visitor); },
        [&visitor](const FuncDecl& e) { return CloneFuncDecl(e, visitor); },
        [&visitor](const StructDecl& e) { return CloneStructDecl(e, visitor); },
        [&visitor](const ClassDecl& e) { return CloneClassDecl(e, visitor); },
        [&visitor](const InterfaceDecl& e) { return CloneInterfaceDecl(e, visitor); },
        [&visitor](const EnumDecl& e) { return CloneEnumDecl(e, visitor); },
        [&visitor](const PrimaryCtorDecl& e) { return ClonePrimaryCtorDecl(e, visitor); },
        [&visitor](const ExtendDecl& e) { return CloneExtendDecl(e, visitor); },
        [](const MacroExpandDecl& e) { return CloneMacroExpandDecl(e); },
        [&visitor](const VarWithPatternDecl& e) { return CloneVarWithPatternDecl(e, visitor); },
        [&visitor](const TypeAliasDecl& e) { return CloneTypeAliasDecl(e, visitor); },
        // MainDecl and MacroDecl are ignored. Since they will be desugared before semantic typecheck.
        [](const MacroDecl& e) { return OwnedPtr<Decl>(MakeOwned<InvalidDecl>(e.begin).release()); },
        [](const MainDecl& e) { return OwnedPtr<Decl>(MakeOwned<InvalidDecl>(e.begin).release()); },
        [](const InvalidDecl& e) { return OwnedPtr<Decl>(MakeOwned<InvalidDecl>(e.begin).release()); },
        [&decl]() {
            // Invalid and ignored cases.
            auto invalidDecl = MakeOwned<InvalidDecl>(decl->begin);
            return OwnedPtr<Decl>(invalidDecl.release());
        });
    CJC_ASSERT(ret);
    CopyNodeField(ret.get(), *decl);
    // Clone field in Decl.
    ret->modifiers.insert(decl->modifiers.begin(), decl->modifiers.end());
    ret->identifier = decl->identifier;
    ret->identifierForLsp = decl->identifierForLsp;
    ret->keywordPos = decl->keywordPos;
    ret->moduleName = decl->moduleName;
    ret->fullPackageName = decl->fullPackageName;
    ret->outerDecl = decl->outerDecl;
    ret->checkFlag = decl->checkFlag;
    ret->captureIndex = decl->captureIndex;
    ret->linkage = decl->linkage;
    for (auto& anno : decl->annotations) {
        ret->annotations.emplace_back(CloneNode(anno.get(), visitor));
    }
    ret->annotationsArray = CloneNode(decl->annotationsArray.get(), visitor);
    visitor(*decl, *ret);
    return ret;
}

OwnedPtr<ConstPattern> ASTCloner::CloneConstPattern(const ConstPattern& cp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ConstPattern>();
    ret->literal = CloneExpr(cp.literal.get(), visitor);
    ret->operatorCallExpr = CloneExpr(cp.operatorCallExpr.get(), visitor);
    return ret;
}

OwnedPtr<VarPattern> ASTCloner::CloneVarPattern(const VarPattern& vp, const VisitFunc& visitor)
{
    OwnedPtr<VarPattern> ret;
    if (vp.varDecl) {
        ret = MakeOwned<VarPattern>(vp.varDecl->identifier, vp.begin);
        auto d = CloneDecl(vp.varDecl.get(), visitor);
        ret->varDecl.reset(As<ASTKind::VAR_DECL>(d.release()));
    } else {
        ret = MakeOwned<VarPattern>();
    }
    ret->desugarExpr = CloneExpr(vp.desugarExpr.get(), visitor);
    return ret;
}

OwnedPtr<TuplePattern> ASTCloner::CloneTuplePattern(const TuplePattern& tp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<TuplePattern>();
    for (auto& i : tp.patterns) {
        ret->patterns.push_back(ClonePattern(i.get(), visitor));
    }
    ret->leftBracePos = tp.leftBracePos;
    ret->rightBracePos = tp.rightBracePos;
    ret->commaPosVector = tp.commaPosVector;
    return ret;
}

OwnedPtr<TypePattern> ASTCloner::CloneTypePattern(const TypePattern& tp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<TypePattern>();
    ret->pattern = ClonePattern(tp.pattern.get(), visitor);
    ret->type = CloneType(tp.type.get(), visitor);
    ret->colonPos = tp.colonPos;

    if (tp.desugarExpr != nullptr) {
        ret->desugarExpr = CloneExpr(tp.desugarExpr.get(), visitor);
    }
    if (tp.desugarVarPattern != nullptr) {
        ret->desugarVarPattern = ASTCloner::Clone(tp.desugarVarPattern.get(), visitor);
    }

    ret->needRuntimeTypeCheck = tp.needRuntimeTypeCheck;
    ret->matchBeforeRuntime = tp.matchBeforeRuntime;
    return ret;
}

OwnedPtr<EnumPattern> ASTCloner::CloneEnumPattern(const EnumPattern& ep, const VisitFunc& visitor)
{
    auto ret = MakeOwned<EnumPattern>();
    ret->constructor = CloneExpr(ep.constructor.get(), visitor);
    for (auto& p : ep.patterns) {
        ret->patterns.emplace_back(ClonePattern(p.get(), visitor));
    }
    ret->leftParenPos = ep.leftParenPos;
    ret->rightParenPos = ep.rightParenPos;
    ret->commaPosVector = ep.commaPosVector;
    return ret;
}

OwnedPtr<ExceptTypePattern> ASTCloner::CloneExceptTypePattern(
    const ExceptTypePattern& etp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ExceptTypePattern>();
    ret->pattern = ClonePattern(etp.pattern.get(), visitor);
    for (auto& i : etp.types) {
        ret->types.push_back(CloneType(i.get(), visitor));
    }
    ret->patternPos = etp.patternPos;
    ret->colonPos = etp.colonPos;
    ret->bitOrPosVector = etp.bitOrPosVector;
    return ret;
}

OwnedPtr<CommandTypePattern> ASTCloner::CloneCommandTypePattern(
    const CommandTypePattern& ctp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<CommandTypePattern>();
    ret->pattern = ClonePattern(ctp.pattern.get(), visitor);
    for (auto& i : ctp.types) {
        ret->types.push_back(CloneType(i.get(), visitor));
    }
    ret->patternPos = ctp.patternPos;
    ret->colonPos = ctp.colonPos;
    ret->bitOrPosVector = ctp.bitOrPosVector;
    return ret;
}

OwnedPtr<VarOrEnumPattern> ASTCloner::CloneVarOrEnumPattern(
    const VarOrEnumPattern& vep, const VisitFunc& visitor)
{
    auto ret = MakeOwned<VarOrEnumPattern>(vep.identifier);
    if (vep.pattern) {
        ret->pattern = ClonePattern(vep.pattern.get(), visitor);
    }
    return ret;
}

/**
 * NOTE: To guarantee the members of Pattern is copied, the sub method should not be called outside 'ClonePattern'.
 */
OwnedPtr<Pattern> ASTCloner::ClonePattern(Ptr<Pattern> pattern, const VisitFunc& visitor)
{
    if (!pattern) {
        return OwnedPtr<Pattern>();
    }
    auto ret = match(*pattern)(
        [&visitor](const ConstPattern& e) { return OwnedPtr<Pattern>(CloneConstPattern(e, visitor)); },
        [](const WildcardPattern& e) { return OwnedPtr<Pattern>(MakeOwned<WildcardPattern>(e)); },
        [&visitor](const VarPattern& e) { return OwnedPtr<Pattern>(CloneVarPattern(e, visitor)); },
        [&visitor](const TuplePattern& e) { return OwnedPtr<Pattern>(CloneTuplePattern(e, visitor)); },
        [&visitor](const TypePattern& e) { return OwnedPtr<Pattern>(CloneTypePattern(e, visitor)); },
        [&visitor](const EnumPattern& e) { return OwnedPtr<Pattern>(CloneEnumPattern(e, visitor)); },
        [&visitor](const ExceptTypePattern& e) { return OwnedPtr<Pattern>(CloneExceptTypePattern(e, visitor)); },
        [&visitor](const CommandTypePattern& e) { return OwnedPtr<Pattern>(CloneCommandTypePattern(e, visitor)); },
        [&visitor](const VarOrEnumPattern& e) { return OwnedPtr<Pattern>(CloneVarOrEnumPattern(e, visitor)); },
        [](const InvalidPattern& e) { return OwnedPtr<Pattern>(MakeOwned<InvalidPattern>(e)); },
        []() { return OwnedPtr<Pattern>(MakeOwned<InvalidPattern>()); });
    CJC_ASSERT(ret && ret->astKind == pattern->astKind);
    CopyNodeField(ret.get(), *pattern);
    // Clone field in Pattern.
    ret->ctxExpr = pattern->ctxExpr;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    visitor(*pattern, *ret);
    return ret;
}

OwnedPtr<Block> ASTCloner::CloneBlock(const Block& block, const VisitFunc& visitor)
{
    auto ret = MakeOwned<Block>();
    CopyNodeField(ret.get(), block);
    // Clone field in Block.
    ret->unsafePos = block.unsafePos;
    ret->leftCurlPos = block.leftCurlPos;
    for (auto& it : block.body) {
        ret->body.push_back(CloneNode(it.get(), visitor));
    }
    ret->rightCurlPos = block.rightCurlPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<ClassBody> ASTCloner::CloneClassBody(const ClassBody& cb, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ClassBody>();
    CopyNodeField(ret.get(), cb);
    // Clone field in ClassBody.
    ret->leftCurlPos = cb.leftCurlPos;
    for (auto& it : cb.decls) {
        ret->decls.push_back(CloneDecl(it.get(), visitor));
    }
    ret->rightCurlPos = cb.rightCurlPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<StructBody> ASTCloner::CloneStructBody(const StructBody& sb, const VisitFunc& visitor)
{
    auto ret = MakeOwned<StructBody>();
    CopyNodeField(ret.get(), sb);
    ret->leftCurlPos = sb.leftCurlPos;
    for (auto& it : sb.decls) {
        ret->decls.push_back(CloneDecl(it.get(), visitor));
    }
    ret->rightCurlPos = sb.rightCurlPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<InterfaceBody> ASTCloner::CloneInterfaceBody(const InterfaceBody& ib, const VisitFunc& visitor)
{
    auto ret = MakeOwned<InterfaceBody>();
    CopyNodeField(ret.get(), ib);
    // Clone field in InterfaceBody.
    ret->leftCurlPos = ib.leftCurlPos;
    for (auto& it : ib.decls) {
        ret->decls.push_back(CloneDecl(it.get(), visitor));
    }
    ret->rightCurlPos = ib.rightCurlPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<GenericConstraint> ASTCloner::CloneGenericConstraint(
    const GenericConstraint& gc, const VisitFunc& visitor)
{
    auto ret = MakeOwned<GenericConstraint>();
    CopyNodeField(ret.get(), gc);
    // Clone field in GenericConstraint.
    ret->type.reset(As<ASTKind::REF_TYPE>(CloneType(gc.type.get(), visitor).release()));
    for (auto& upperBound : gc.upperBounds) {
        ret->upperBounds.push_back(CloneType(upperBound.get(), visitor));
    }
    ret->wherePos = gc.wherePos;
    ret->operatorPos = gc.operatorPos;
    ret->bitAndPos = gc.bitAndPos;
    ret->commaPos = gc.commaPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<FuncBody> ASTCloner::CloneFuncBody(const FuncBody& fb, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncBody>();
    CopyNodeField(ret.get(), fb);
    // Clone field in FuncBody.
    for (auto& it : fb.paramLists) {
        ret->paramLists.push_back(CloneNode(it.get(), visitor));
    }
    ret->doubleArrowPos = fb.doubleArrowPos;
    ret->colonPos = fb.colonPos;
    ret->retType = CloneType(fb.retType.get(), visitor);
    ret->body = CloneExpr(fb.body.get(), visitor);
    if (fb.generic) {
        ret->generic = CloneGeneric(*fb.generic, visitor);
    }
    for (auto& it : fb.capturedVars) {
        ret->capturedVars.insert(it);
    }
    ret->captureKind = fb.captureKind;
    ret->outerFunc = fb.outerFunc;
    ret->parentClassLike = fb.parentClassLike;
    ret->parentStruct = fb.parentStruct;
    ret->parentEnum = fb.parentEnum;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<FuncParamList> ASTCloner::CloneFuncParamList(const FuncParamList& fpl, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncParamList>();
    CopyNodeField(ret.get(), fpl);
    // Clone field in FuncParamList.
    ret->leftParenPos = fpl.leftParenPos;
    for (auto& it : fpl.params) {
        auto funcParam = MakeOwned<FuncParam>();
        auto d = CloneDecl(it.get(), visitor);
        funcParam.reset(As<ASTKind::FUNC_PARAM>(d.release()));
        ret->params.push_back(std::move(funcParam));
    }
    ret->variadicArgIndex = fpl.variadicArgIndex;
    ret->hasVariableLenArg = fpl.hasVariableLenArg;
    ret->rightParenPos = fpl.rightParenPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<FuncArg> ASTCloner::CloneFuncArg(const FuncArg& fa, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncArg>();
    CopyNodeField(ret.get(), fa);
    // Clone field in FuncArg.
    ret->name = fa.name;
    ret->withInout = fa.withInout;
    ret->colonPos = fa.colonPos;
    ret->expr = CloneExpr(fa.expr.get(), visitor);
    ret->commaPos = fa.commaPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Annotation> ASTCloner::CloneAnnotation(const Annotation& annotation, const VisitFunc& visitor)
{
    auto ret = MakeOwned<Annotation>(annotation.identifier, annotation.kind, annotation.begin);
    CopyNodeField(ret.get(), annotation);
    ret->kind = annotation.kind;
    ret->definedPackage = annotation.definedPackage;
    ret->identifier = annotation.identifier;
    ret->attrs = annotation.attrs;
    ret->adAnnotation = annotation.adAnnotation;
    ret->rsquarePos = annotation.rsquarePos;
    ret->lsquarePos = annotation.lsquarePos;
    for (auto& arg : annotation.args) {
        ret->args.emplace_back(CloneNode(arg.get(), visitor));
    }
    ret->condExpr = CloneExpr(annotation.condExpr.get());
    ret->baseExpr = CloneExpr(annotation.baseExpr.get());
    return ret;
}

OwnedPtr<ImportSpec> ASTCloner::CloneImportSpec(const ImportSpec& is, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ImportSpec>();
    CopyNodeField(ret.get(), is);
    if (is.modifier) {
        // Clone Modifier
        ret->modifier = MakeOwned<Modifier>(is.modifier->modifier, is.modifier->begin);
        CopyNodeField(ret->modifier.get(), *is.modifier);
        ret->modifier->isExplicit = is.modifier->isExplicit;
    }
    std::function<void(const ImportContent&, ImportContent&)> cloneContent
        = [&cloneContent](const ImportContent& src, ImportContent& dst) {
        CopyNodeField(&dst, src);
        dst.kind = src.kind;
        dst.prefixPaths = src.prefixPaths;
        dst.prefixPoses = src.prefixPoses;
        dst.prefixDotPoses = src.prefixDotPoses;
        dst.identifier = src.identifier;
        dst.asPos = src.asPos;
        dst.aliasName = src.aliasName;
        dst.leftCurlPos = src.leftCurlPos;
        if (!src.items.empty()) {
            dst.items.resize(src.items.size());
            for (size_t i = 0; i < src.items.size(); ++i) {
                cloneContent(src.items[i], dst.items[i]);
            }
        }
        dst.commaPoses = src.commaPoses;
        dst.rightCurlPos = src.rightCurlPos;
    };
    cloneContent(is.content, ret->content);
    for (auto& it : is.annotations) {
        ret->annotations.emplace_back(CloneNode(it.get(), visitor));
    }
    return ret;
}

OwnedPtr<MatchCase> ASTCloner::CloneMatchCase(const MatchCase& mc, const VisitFunc& visitor)
{
    auto ret = MakeOwned<MatchCase>();
    CopyNodeField(ret.get(), mc);
    // Clone field in MatchCase.
    for (auto& pattern : mc.patterns) {
        ret->patterns.emplace_back(ClonePattern(pattern.get(), visitor));
    }
    ret->patternGuard = CloneExpr(mc.patternGuard.get(), visitor);
    ret->exprOrDecls = CloneExpr(mc.exprOrDecls.get(), visitor);
    ret->wherePos = mc.wherePos;
    ret->arrowPos = mc.arrowPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    ret->bitOrPosVector = mc.bitOrPosVector;
    return ret;
}

OwnedPtr<MatchCaseOther> ASTCloner::CloneMatchCaseOther(const MatchCaseOther& mco, const VisitFunc& visitor)
{
    auto ret = MakeOwned<MatchCaseOther>();
    CopyNodeField(ret.get(), mco);
    // Clone field in MatchCaseOther.
    ret->matchExpr = CloneExpr(mco.matchExpr.get(), visitor);
    ret->exprOrDecls = CloneExpr(mco.exprOrDecls.get(), visitor);
    ret->arrowPos = mco.arrowPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

// Clone Node.
template <typename NodeT> OwnedPtr<NodeT> ASTCloner::CloneNode(Ptr<NodeT> node, const VisitFunc& visitor)
{
    if (!node) {
        return OwnedPtr<NodeT>();
    }
    auto clonedNode = match(*node)(
        // NOTE: Package, File, PackageSpec, ImportSpec, Modifier are ignored.
        // EnumBody struct is empty, no need to be cloned
        [&visitor](Type& type) { return OwnedPtr<Node>(CloneType(Ptr(&type), visitor)); },
        [&visitor](Expr& expr) { return OwnedPtr<Node>(CloneExpr(Ptr(&expr), visitor)); },
        [&visitor](Decl& decl) { return OwnedPtr<Node>(CloneDecl(Ptr(&decl), visitor)); },
        [&visitor](Pattern& pattern) { return OwnedPtr<Node>(ClonePattern(Ptr(&pattern), visitor)); },
        [&visitor](const ClassBody& cb) { return OwnedPtr<Node>(CloneClassBody(cb, visitor)); },
        [&visitor](const StructBody& rb) { return OwnedPtr<Node>(CloneStructBody(rb, visitor)); },
        [&visitor](const InterfaceBody& ib) { return OwnedPtr<Node>(CloneInterfaceBody(ib, visitor)); },
        [&visitor](const GenericConstraint& gc) { return OwnedPtr<Node>(CloneGenericConstraint(gc, visitor)); },
        [&visitor](const FuncBody& fb) { return OwnedPtr<Node>(CloneFuncBody(fb, visitor)); },
        [&visitor](const FuncParamList& fpl) { return OwnedPtr<Node>(CloneFuncParamList(fpl, visitor)); },
        [&visitor](const FuncArg& fa) { return OwnedPtr<Node>(CloneFuncArg(fa, visitor)); },
        [&visitor](const MatchCase& mc) { return OwnedPtr<Node>(CloneMatchCase(mc, visitor)); },
        [&visitor](const MatchCaseOther& mco) { return OwnedPtr<Node>(CloneMatchCaseOther(mco, visitor)); },
        [&visitor](const Annotation& ann) { return OwnedPtr<Node>(CloneAnnotation(ann, visitor)); },
        [](const DummyBody&) { return OwnedPtr<Node>(OwnedPtr<DummyBody>()); },
        [&visitor](const ImportSpec& is) { return OwnedPtr<Node>(CloneImportSpec(is, visitor)); },
        []() {
            // Invalid cases.
            return OwnedPtr<Node>(MakeOwned<InvalidExpr>());
        });
    CJC_ASSERT(clonedNode);
    visitor(*node, *clonedNode);
    clonedNode->EnableAttr(Attribute::COMPILER_ADD);
    auto result = DynamicCast<NodeT*>(clonedNode.release());
    return OwnedPtr<NodeT>(result);
}

OwnedPtr<Node> ASTCloner::CloneWithRearrange(Ptr<Node> node, const VisitFunc& visitor)
{
    VisitFunc collectMap = [this, visitor](Node& from, Node& target) {
        // Collect decl to decl map.
        if (auto decl = DynamicCast<Decl*>(&from); decl) {
            source2cloned[decl] = &target;
            auto& targetDecl = static_cast<Decl&>(target);
            TargetAddrMapInsert(decl->outerDecl, targetDecl.outerDecl);
            TargetAddrMapInsert(decl->genericDecl, targetDecl.genericDecl);
            // unorder_set<Ptr<Node>> users field in Decl is ignored.
        }
        if (auto expr = DynamicCast<Expr*>(&from); expr) {
            source2cloned[expr] = &target;
            auto& targetExpr = static_cast<Expr&>(target);
            TargetAddrMapInsert(expr->sourceExpr, targetExpr.sourceExpr);
            TargetAddrMapInsert(expr->mapExpr, targetExpr.mapExpr);
        }
        if (auto funcBody = DynamicCast<FuncBody*>(&from); funcBody) {
            source2cloned[funcBody] = &target;
            auto& targetFuncBody = static_cast<FuncBody&>(target);
            TargetAddrMapInsert(funcBody->funcDecl, targetFuncBody.funcDecl);
            TargetAddrMapInsert(funcBody->outerFunc, targetFuncBody.outerFunc);
            TargetAddrMapInsert(funcBody->parentClassLike, targetFuncBody.parentClassLike);
            TargetAddrMapInsert(funcBody->parentStruct, targetFuncBody.parentStruct);
            TargetAddrMapInsert(funcBody->parentEnum, targetFuncBody.parentEnum);
        }
        if (auto pattern = DynamicCast<Pattern*>(&from); pattern) {
            source2cloned[pattern] = &target;
            auto& targetPattern = static_cast<Pattern&>(target);
            TargetAddrMapInsert(pattern->ctxExpr, targetPattern.ctxExpr);
        }

        // Package and File are ignored since usually we do not need to clone them.

        // Collect target variable address to target variable address map.
        // For sub-class of Expr, there is only 1 layer under it, we just enumerate them.
        // For sub-class of Decl, there are 2 layers under it. FuncParam extends Vardecl
        // which extends Decl.
        // Macro related ASTs also are ignored

        switch (from.astKind) {
            // sub-class of Expr struct
            case ASTKind::REF_EXPR: {
                auto& reFrom = static_cast<RefExpr&>(from);
                auto& reTarget = static_cast<RefExpr&>(target);
                TargetAddrMapInsert(reFrom.ref.target, reTarget.ref.target);
                TargetAddrMapInsert(reFrom.callOrPattern, reTarget.callOrPattern);
                break;
            }
            case ASTKind::ARRAY_EXPR: {
                auto& aeFrom = static_cast<ArrayExpr&>(from);
                auto& aeTarget = static_cast<ArrayExpr&>(target);
                TargetAddrMapInsert(aeFrom.initFunc, aeTarget.initFunc);
                break;
            }
            case ASTKind::ARRAY_LIT: {
                auto& aeFrom = static_cast<ArrayLit&>(from);
                auto& aeTarget = static_cast<ArrayLit&>(target);
                TargetAddrMapInsert(aeFrom.initFunc, aeTarget.initFunc);
                break;
            }
            case ASTKind::MEMBER_ACCESS: {
                auto& maFrom = static_cast<MemberAccess&>(from);
                if (auto base = DynamicCast<RefExpr*>(maFrom.baseExpr.get()); !base || !base->isThis) {
                    break; // Do not rearrange target if current is not accessing 'this'.
                }
                auto& maTarget = static_cast<MemberAccess&>(target);
                TargetAddrMapInsert(maFrom.target, maTarget.target);
                TargetAddrMapInsert(maFrom.callOrPattern, maTarget.callOrPattern);
                for (size_t i = 0; i < maFrom.targets.size(); i++) {
                    TargetAddrMapInsert(maFrom.targets[i], maTarget.targets[i]);
                }
                // NOTE: foundUpperBoundMap is ignored
                break;
            }
            case ASTKind::FOR_IN_EXPR: {
                auto& fiFrom = static_cast<ForInExpr&>(from);
                auto& fiTarget = static_cast<ForInExpr&>(target);
                TargetAddrMapInsert(fiFrom.patternInDesugarExpr, fiTarget.patternInDesugarExpr);
                break;
            }
            case ASTKind::CALL_EXPR: {
                auto& ceFrom = static_cast<CallExpr&>(from);
                if (auto ma = DynamicCast<MemberAccess*>(ceFrom.baseFunc.get())) {
                    if (auto base = DynamicCast<RefExpr*>(ma->baseExpr.get()); !base || !base->isThis) {
                        // Do not rearrange target if current is not call of refExpr or memberAccess of 'this'.
                        break;
                    }
                }
                auto& ceTarget = static_cast<CallExpr&>(target);
                TargetAddrMapInsert(ceFrom.resolvedFunction, ceTarget.resolvedFunction);
                break;
            }
            case ASTKind::RETURN_EXPR: {
                auto& rtFrom = static_cast<ReturnExpr&>(from);
                auto& rtTarget = static_cast<ReturnExpr&>(target);
                TargetAddrMapInsert(rtFrom.refFuncBody, rtTarget.refFuncBody);
                break;
            }
            case ASTKind::JUMP_EXPR: {
                auto& jeFrom = static_cast<JumpExpr&>(from);
                auto& jeTarget = static_cast<JumpExpr&>(target);
                TargetAddrMapInsert(jeFrom.refLoop, jeTarget.refLoop);
                break;
            }
            case ASTKind::REF_TYPE: {
                auto& reFrom = static_cast<RefType&>(from);
                auto& reTarget = static_cast<RefType&>(target);
                TargetAddrMapInsert(reFrom.ref.target, reTarget.ref.target);
                break;
            }
            case ASTKind::QUALIFIED_TYPE: {
                auto& qtFrom = static_cast<QualifiedType&>(from);
                auto& qtTarget = static_cast<QualifiedType&>(target);
                TargetAddrMapInsert(qtFrom.target, qtTarget.target);
                break;
            }
            // case ASTKind::FUNC_BODY handled in if case
            case ASTKind::FUNC_DECL: {
                auto& fdFrom = static_cast<FuncDecl&>(from);
                auto& fdTarget = static_cast<FuncDecl&>(target);
                TargetAddrMapInsert(fdFrom.ownerFunc, fdTarget.ownerFunc);
                TargetAddrMapInsert(fdFrom.propDecl, fdTarget.propDecl);
                break;
            }
            case ASTKind::VAR_DECL: {
                auto& vdFrom = static_cast<VarDecl&>(from);
                auto& vdTarget = static_cast<VarDecl&>(target);
                TargetAddrMapInsert(vdFrom.parentPattern, vdTarget.parentPattern);
                break;
            }
            default:
                break;
        }
        visitor(from, target);
    };
    OwnedPtr<Node> targetNode = CloneNode(node, collectMap);
    // Rearrange pointer to node pointer's target from source node pointer to cloned node pointer.
    for (auto& [s, t] : targetAddr2targetAddr) {
        if (source2cloned.find(*s) != source2cloned.end()) {
            *t = source2cloned[*s];
        }
    }
    return targetNode;
}
} // namespace Cangjie::AST
