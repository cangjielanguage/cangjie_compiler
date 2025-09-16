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

#include "PartialInstantiation.h"

#include "ImplUtils.h"
#include "cangjie/AST/Create.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Utils/Casting.h"

namespace Cangjie {
using namespace Cangjie::AST;
using namespace Cangjie::Meta;

namespace {
auto g_optLevel = GlobalOptions::OptimizationLevel::O0;

inline bool IsCPointerFrozenMember(const Decl& decl)
{
    if (decl.fullPackageName != CORE_PACKAGE_NAME || decl.astKind != ASTKind::FUNC_DECL ||
        !decl.HasAnno(AnnotationKind::FROZEN) || !decl.outerDecl || decl.outerDecl->astKind != ASTKind::EXTEND_DECL) {
        return false;
    }

    return StaticCast<ExtendDecl>(decl.outerDecl)->extendedType->ty->IsPointer();
}
} // namespace

void SetOptLevel(const Cangjie::GlobalOptions& opts)
{
    g_optLevel = opts.optimizationLevel;
}

GlobalOptions::OptimizationLevel GetOptLevel()
{
    return g_optLevel;
}

bool IsOpenDecl(const Decl& decl)
{
    if (!decl.IsClassLikeDecl()) {
        return false;
    }
    // Note: Non-open class will mark as 'OPEN' and 'OPEN_TO_MOCK' when mock is on.
    if (decl.TestAttr(Attribute::OPEN_TO_MOCK)) {
        return false;
    }
    return decl.TestAnyAttr(AST::Attribute::ABSTRACT, AST::Attribute::OPEN) || decl.astKind == ASTKind::INTERFACE_DECL;
};

/**
 * Check whether the location where the instantiation is triggered is in the context with Open semantics.
 * Return true if expr is in a member of an open class or interface.
 */
bool IsInOpenContext(const std::vector<Ptr<AST::Decl>>& contextDecl)
{
    bool isInOpenContext = !contextDecl.empty();
    if (isInOpenContext) {
        auto toplevelDecl = contextDecl.front();
        if (toplevelDecl->IsNominalDecl()) {
            isInOpenContext = IsOpenDecl(*toplevelDecl);
        } else {
            // In global function if outer is null, and global function context can be instantated.
            auto outer = GetOuterStructDecl(*toplevelDecl);
            isInOpenContext = outer == nullptr || IsOpenDecl(*outer);
        }
    }
    return isInOpenContext;
}

bool RequireInstantiation(const Decl& decl, bool isInOpenContext)
{
    if (IsCPointerFrozenMember(decl)) {
        return true;
    }
    if (decl.astKind == ASTKind::INTERFACE_DECL) {
        return false;
    }
    if (decl.IsNominalDecl()) {
        if (g_optLevel >= GlobalOptions::OptimizationLevel::O2 && !decl.TestAttr(Attribute::IMPORTED)) {
            return true;
        }
        if (decl.TestAttr(Attribute::GENERIC)) {
            auto& members = decl.GetMemberDecls();
            return std::any_of(members.begin(), members.end(),
                [&isInOpenContext](auto& member) { return RequireInstantiation(*member, isInOpenContext); });
        }
        return false;
    }
    // If the current reference comes from an open context, that context may be inherited into a subtype, and the
    // declaration cannot be instantiated as a unique solution.
    // For example:
    //      interface I {
    //          static func foo(): Int64
    //          func call() { foo() }
    //      }
    //      class A <: I { static func foo(): Int64 {1} }
    //      class B <: I { static func foo(): Int64 {2} }
    // CallExpr `foo()` cannot pointer to any instantation version, it must be a static-invoke.
    if (IsVirtualMember(decl) || isInOpenContext) {
        return false;
    }
    if (decl.IsConst()) {
        return true;
    }
    if (decl.TestAttr(Attribute::CONTAINS_MOCK_CREATION_CALL) && decl.HasAnno(AnnotationKind::FROZEN)) {
        return true;
    }
    if (g_optLevel < GlobalOptions::OptimizationLevel::O2) {
        return false;
    }
    return !IsInDeclWithAttribute(decl, Attribute::IMPORTED) || decl.HasAnno(AnnotationKind::FROZEN);
}

void DefaultVisitFunc(const Node& /* source */, const Node& /* target */)
{
}

OwnedPtr<Generic> InstantiateGeneric(const Generic& generic, const VisitFunc& visitor)
{
    auto ret = MakeOwned<Generic>();
    for (auto& it : generic.typeParameters) {
        auto gpd = MakeOwned<GenericParamDecl>();
        auto d = PartialInstantiation::Instantiate(it.get(), visitor);
        gpd.reset(As<ASTKind::GENERIC_PARAM_DECL>(d.release()));
        ret->typeParameters.push_back(std::move(gpd));
    }
    for (auto& it : generic.genericConstraints) {
        ret->genericConstraints.push_back(PartialInstantiation::Instantiate(it.get(), visitor));
    }
    ret->leftAnglePos = generic.leftAnglePos;
    ret->rightAnglePos = generic.rightAnglePos;
    return ret;
}

MacroInvocation InstantiateMacroInvocation(const MacroInvocation& me)
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
    CopyNodeScopeInfo(ret, &e);
    ret->CopyAttrs(e.GetAttrs());
}
} // namespace

std::unordered_map<Ptr<const Decl>, Ptr<Decl>> PartialInstantiation::ins2generic = {};
std::unordered_map<Ptr<const Decl>, std::unordered_set<Ptr<Decl>>> PartialInstantiation::generic2ins = {};

// Instantiate nodes which inherit Node indirectly, should only be call by clone function which inherit Node directly.
OwnedPtr<Decl> PartialInstantiation::InstantiateGenericParamDecl(const GenericParamDecl& gpd)
{
    auto ret = MakeOwned<GenericParamDecl>();
    ret->outerDecl = gpd.outerDecl;
    ret->commaPos = gpd.commaPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<VarDecl> PartialInstantiation::InstantiateFuncParam(const FuncParam& fp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncParam>();
    ret->colonPos = fp.colonPos;
    ret->assignment = InstantiateExpr(fp.assignment.get(), visitor);
    if (fp.desugarDecl) {
        auto decl = InstantiateDecl(fp.desugarDecl.get(), visitor);
        decl->genericDecl = fp.desugarDecl.get();
        ret->desugarDecl.reset(RawStaticCast<FuncDecl*>(decl.release()));
    }
    ret->isNamedParam = fp.isNamedParam;
    ret->isMemberParam = fp.isMemberParam;
    ret->commaPos = fp.commaPos;
    ret->notMarkPos = fp.notMarkPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiateVarWithPatternDecl(
    const VarWithPatternDecl& vwpd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<VarWithPatternDecl>();
    ret->type = InstantiateType(vwpd.type.get(), visitor);
    ret->assignPos = vwpd.assignPos;
    ret->colonPos = vwpd.colonPos;
    ret->isVar = vwpd.isVar;
    ret->isConst = vwpd.isConst;
    ret->irrefutablePattern = InstantiatePattern(vwpd.irrefutablePattern.get(), visitor);
    ret->initializer = InstantiateExpr(vwpd.initializer.get(), visitor);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiateVarDecl(const VarDecl& vd, const VisitFunc& visitor)
{
    auto ret = match(vd)([&visitor](const FuncParam& e) { return InstantiateFuncParam(e, visitor); },
        []() { return MakeOwned<VarDecl>(); });
    // Instantiate field in VarDecl.
    ret->type = InstantiateType(vd.type.get(), visitor);
    ret->colonPos = vd.colonPos;
    ret->initializer = InstantiateExpr(vd.initializer.get(), visitor);
    ret->assignPos = vd.assignPos;
    ret->isVar = vd.isVar;
    ret->isConst = vd.isConst;
    ret->isResourceVar = vd.isResourceVar;
    ret->isIdentifierCompilerAdd = vd.isIdentifierCompilerAdd;
    ret->parentPattern = vd.parentPattern;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiateFuncDecl(const FuncDecl& fd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncDecl>();
    ret->leftParenPos = fd.leftParenPos;
    ret->rightParenPos = fd.rightParenPos;
    CJC_NULLPTR_CHECK(fd.funcBody);
    ret->funcBody = InstantiateNode(fd.funcBody.get(), visitor);
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

OwnedPtr<Decl> PartialInstantiation::InstantiatePrimaryCtorDecl(const PrimaryCtorDecl& pcd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<PrimaryCtorDecl>();
    CJC_NULLPTR_CHECK(pcd.funcBody);
    ret->funcBody = InstantiateNode(pcd.funcBody.get(), visitor);
    ret->overflowStrategy = pcd.overflowStrategy;
    ret->isFastNative = pcd.isFastNative;
    ret->isConst = pcd.isConst;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiatePropDecl(const PropDecl& pd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<PropDecl>();
    ret->type = InstantiateType(pd.type.get(), visitor);
    ret->colonPos = pd.colonPos;
    ret->leftCurlPos = pd.leftCurlPos;
    ret->rightCurlPos = pd.rightCurlPos;
    ret->isVar = pd.isVar;
    ret->isConst = pd.isConst;
    for (auto& func : pd.setters) {
        auto decl = InstantiateDecl(func.get(), visitor);
        auto fd = MakeOwned<FuncDecl>();
        fd.reset(As<ASTKind::FUNC_DECL>(decl.release()));
        fd->EnableAttr(Attribute::COMPILER_ADD);
        ret->setters.push_back(std::move(fd));
    }
    for (auto& func : pd.getters) {
        auto decl = InstantiateDecl(func.get(), visitor);
        auto fd = MakeOwned<FuncDecl>();
        fd.reset(As<ASTKind::FUNC_DECL>(decl.release()));
        fd->EnableAttr(Attribute::COMPILER_ADD);
        ret->getters.push_back(std::move(fd));
    }

    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<RefType> PartialInstantiation::InstantiateRefType(const RefType& type, const VisitFunc& visitor)
{
    auto ret = MakeOwned<RefType>();
    CopyNodeField(ret.get(), type);
    ret->ref = type.ref;
    ret->leftAnglePos = type.leftAnglePos;
    ret->typeArguments = std::vector<OwnedPtr<Type>>();
    for (auto& it : type.typeArguments) {
        ret->typeArguments.push_back(InstantiateType(it.get(), visitor));
    }
    ret->rightAnglePos = type.rightAnglePos;
    ret->commaPos = type.commaPos;
    ret->bitAndPos = type.bitAndPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiateExtendDecl(const ExtendDecl& ed, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ExtendDecl>();
    ret->extendedType = InstantiateType(ed.extendedType.get(), visitor);
    for (auto& interface : ed.inheritedTypes) {
        ret->inheritedTypes.push_back(InstantiateType(interface.get(), visitor));
    }
    for (auto& member : ed.members) {
        if (RequireInstantiation(*member)) {
            ret->members.push_back(InstantiateDecl(member.get(), visitor));
        }
    }
    if (ed.generic) {
        ret->generic = InstantiateGeneric(*ed.generic, visitor);
    }
    if (ed.bodyScope) {
        ret->bodyScope = MakeOwned<DummyBody>();
        CopyNodeScopeInfo(ret->bodyScope.get(), ed.bodyScope.get());
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiateMacroExpandDecl(const MacroExpandDecl& med)
{
    auto ret = MakeOwned<MacroExpandDecl>();
    ret->invocation = InstantiateMacroInvocation(med.invocation);
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiateStructDecl(const StructDecl& sd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<StructDecl>();
    for (auto& it : sd.inheritedTypes) {
        ret->inheritedTypes.push_back(InstantiateType(it.get(), visitor));
    }
    ret->body = InstantiateNode(sd.body.get(), visitor);
    if (sd.generic) {
        ret->generic = InstantiateGeneric(*sd.generic, visitor);
    }
    ret->upperBoundPos = sd.upperBoundPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiateClassDecl(const ClassDecl& cd, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ClassDecl>();
    for (auto& it : cd.inheritedTypes) {
        ret->inheritedTypes.push_back(InstantiateType(it.get(), visitor));
    }
    ret->body = InstantiateNode(cd.body.get(), visitor);
    if (cd.generic) {
        ret->generic = InstantiateGeneric(*cd.generic, visitor);
    }
    ret->upperBoundPos = cd.upperBoundPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiateInterfaceDecl(const InterfaceDecl& id, const VisitFunc& visitor)
{
    auto ret = MakeOwned<InterfaceDecl>();

    for (auto& it : id.inheritedTypes) {
        ret->inheritedTypes.push_back(InstantiateType(it.get(), visitor));
    }
    ret->body = InstantiateNode(id.body.get(), visitor);

    if (id.generic) {
        ret->generic = InstantiateGeneric(*id.generic, visitor);
    }
    ret->upperBoundPos = id.upperBoundPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Decl> PartialInstantiation::InstantiateEnumDecl(const EnumDecl& ed, const VisitFunc& visitor)
{
    auto ret = MakeOwned<EnumDecl>();
    for (auto& it : ed.inheritedTypes) {
        ret->inheritedTypes.push_back(InstantiateType(it.get(), visitor));
    }
    for (auto& it : ed.constructors) {
        ret->constructors.push_back(InstantiateDecl(it.get(), visitor));
    }
    for (auto& func : ed.members) {
        if (!RequireInstantiation(*func)) {
            continue;
        }
        ret->members.push_back(InstantiateDecl(func.get(), visitor));
    }
    if (ed.generic) {
        ret->generic = InstantiateGeneric(*ed.generic, visitor);
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

OwnedPtr<Decl> PartialInstantiation::InstantiateTypeAliasDecl(const TypeAliasDecl& tad, const VisitFunc& visitor)
{
    auto ret = MakeOwned<TypeAliasDecl>();
    ret->assignPos = tad.assignPos;
    ret->type = InstantiateType(tad.type.get(), visitor);
    if (tad.generic != nullptr) {
        ret->generic = InstantiateGeneric(*tad.generic, visitor);
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<QualifiedType> PartialInstantiation::InstantiateQualifiedType(
    const QualifiedType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<QualifiedType>();
    ret->baseType = InstantiateType(node.baseType.get(), visitor);
    ret->field = node.field;
    ret->target = node.target;
    for (auto& it : node.typeArguments) {
        ret->typeArguments.push_back(InstantiateType(it.get(), visitor));
    }
    return ret;
}

OwnedPtr<ParenType> PartialInstantiation::InstantiateParenType(const ParenType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ParenType>();
    ret->type = InstantiateType(node.type.get(), visitor);
    ret->leftParenPos = node.leftParenPos;
    ret->rightParenPos = node.rightParenPos;
    return ret;
}

OwnedPtr<OptionType> PartialInstantiation::InstantiateOptionType(const OptionType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<OptionType>();
    ret->componentType = InstantiateType(node.componentType.get(), visitor);
    ret->questNum = node.questNum;
    ret->questVector = node.questVector;
    if (node.desugarType != nullptr) {
        ret->desugarType = Instantiate(node.desugarType.get(), visitor);
    }
    return ret;
}

OwnedPtr<FuncType> PartialInstantiation::InstantiateFuncType(const FuncType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncType>();
    for (auto& paramType : node.paramTypes) {
        ret->paramTypes.emplace_back(InstantiateType(paramType.get(), visitor));
    }
    ret->retType = InstantiateType(node.retType.get(), visitor);
    ret->isC = node.isC;
    ret->leftParenPos = node.leftParenPos;
    ret->rightParenPos = node.rightParenPos;
    ret->arrowPos = node.arrowPos;
    return ret;
}

OwnedPtr<TupleType> PartialInstantiation::InstantiateTupleType(const TupleType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<TupleType>();
    for (auto& it : node.fieldTypes) {
        ret->fieldTypes.push_back(InstantiateType(it.get(), visitor));
    }
    ret->leftParenPos = node.leftParenPos;
    ret->rightParenPos = node.rightParenPos;
    ret->commaPosVector = node.commaPosVector;
    return ret;
}

OwnedPtr<ConstantType> PartialInstantiation::InstantiateConstantType(const ConstantType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ConstantType>();
    ret->constantExpr = InstantiateExpr(node.constantExpr.get(), visitor);
    ret->dollarPos = node.dollarPos;
    return ret;
}

OwnedPtr<VArrayType> PartialInstantiation::InstantiateVArrayType(const VArrayType& node, const VisitFunc& visitor)
{
    auto ret = MakeOwned<VArrayType>();
    ret->leftAnglePos = node.leftAnglePos;
    ret->typeArgument = InstantiateType(node.typeArgument.get(), visitor);
    ret->constantType = InstantiateType(node.constantType.get(), visitor);
    ret->rightAnglePos = node.rightAnglePos;
    return ret;
}

// Instantiate nodes which inherit Node directly.
OwnedPtr<Type> PartialInstantiation::InstantiateType(Ptr<Type> type, const VisitFunc& visitor)
{
    if (!type) {
        return OwnedPtr<Type>();
    }
    auto ret = match(*type)([&visitor](const RefType& e) { return OwnedPtr<Type>(InstantiateRefType(e, visitor)); },
        [](const PrimitiveType& e) { return OwnedPtr<Type>(MakeOwned<PrimitiveType>(e)); },
        [&visitor](const ParenType& e) { return OwnedPtr<Type>(InstantiateParenType(e, visitor)); },
        [&visitor](const QualifiedType& e) { return OwnedPtr<Type>(InstantiateQualifiedType(e, visitor)); },
        [&visitor](const OptionType& e) { return OwnedPtr<Type>(InstantiateOptionType(e, visitor)); },
        [&visitor](const FuncType& e) { return OwnedPtr<Type>(InstantiateFuncType(e, visitor)); },
        [&visitor](const TupleType& e) { return OwnedPtr<Type>(InstantiateTupleType(e, visitor)); },
        [&visitor](const ConstantType& e) { return OwnedPtr<Type>(InstantiateConstantType(e, visitor)); },
        [&visitor](const VArrayType& e) { return OwnedPtr<Type>(InstantiateVArrayType(e, visitor)); },
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

OwnedPtr<MacroExpandExpr> PartialInstantiation::InstantiateMacroExpandExpr(
    const MacroExpandExpr& mee, const VisitFunc& visitor)
{
    auto expr = MakeOwned<MacroExpandExpr>();
    expr->identifier = mee.identifier;
    expr->invocation = InstantiateMacroInvocation(mee.invocation);
    for (auto& anno : mee.annotations) {
        expr->annotations.emplace_back(InstantiateNode(anno.get(), visitor));
    }
    expr->modifiers.insert(mee.modifiers.begin(), mee.modifiers.end());
    return expr;
}

OwnedPtr<TokenPart> PartialInstantiation::InstantiateTokenPart(const TokenPart& tp, const VisitFunc& /* visitor */)
{
    auto expr = MakeOwned<TokenPart>();
    expr->tokens.assign(tp.tokens.begin(), tp.tokens.end());
    return expr;
}

OwnedPtr<QuoteExpr> PartialInstantiation::InstantiateQuoteExpr(const QuoteExpr& qe, const VisitFunc& visitor)
{
    auto expr = MakeOwned<QuoteExpr>();
    for (auto& i : qe.exprs) {
        expr->exprs.push_back(InstantiateExpr(i.get(), visitor));
    }
    expr->quotePos = qe.quotePos;
    expr->leftParenPos = qe.leftParenPos;
    expr->rightParenPos = qe.rightParenPos;
    return expr;
}

OwnedPtr<IfExpr> PartialInstantiation::InstantiateIfExpr(const IfExpr& ie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<IfExpr>();
    expr->ifPos = ie.ifPos;
    expr->leftParenPos = ie.leftParenPos;
    expr->condExpr = InstantiateExpr(ie.condExpr.get(), visitor);
    expr->rightParenPos = ie.rightParenPos;
    auto n = InstantiateExpr(ie.thenBody.get(), visitor);
    expr->thenBody.reset(As<ASTKind::BLOCK>(n.release()));
    expr->hasElse = ie.hasElse;
    expr->isElseIf = ie.isElseIf;
    expr->elsePos = ie.elsePos;
    expr->elseBody = InstantiateExpr(ie.elseBody.get(), visitor);
    return expr;
}

OwnedPtr<TryExpr> PartialInstantiation::InstantiateTryExpr(const TryExpr& te, const VisitFunc& visitor)
{
    auto expr = MakeOwned<TryExpr>();
    expr->tryPos = te.tryPos;
    expr->lParen = te.lParen;
    for (auto& resource : te.resourceSpec) {
        expr->resourceSpec.push_back(
            OwnedPtr<VarDecl>(As<ASTKind::VAR_DECL>(InstantiateDecl(resource.get(), visitor).release())));
    }
    expr->isDesugaredFromSyncBlock = te.isDesugaredFromSyncBlock;
    expr->isDesugaredFromTryWithResources = te.isDesugaredFromTryWithResources;
    expr->isIllegalResourceSpec = te.isIllegalResourceSpec;
    expr->tryBlock = InstantiateExpr(te.tryBlock.get(), visitor);
    for (size_t i = 0; i < te.catchPosVector.size(); i++) {
        expr->catchPosVector.push_back(te.catchPosVector[i]);
    }
    CJC_ASSERT(te.catchBlocks.size() == te.catchPatterns.size());
    for (size_t i = 0; i < te.catchBlocks.size(); i++) {
        expr->catchBlocks.push_back(InstantiateExpr(te.catchBlocks[i].get(), visitor));
        expr->catchPatterns.push_back(InstantiatePattern(te.catchPatterns[i].get(), visitor));
    }
    expr->finallyPos = te.finallyPos;
    expr->finallyBlock = InstantiateExpr(te.finallyBlock.get(), visitor);
    expr->resourceSpecCommaPos = te.resourceSpecCommaPos;
    expr->rParen = te.rParen;
    expr->catchLParenPosVector = te.catchLParenPosVector;
    expr->catchRParenPosVector = te.catchRParenPosVector;
    return expr;
}

OwnedPtr<ThrowExpr> PartialInstantiation::InstantiateThrowExpr(const ThrowExpr& te, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ThrowExpr>();
    expr->throwPos = te.throwPos;
    expr->expr = InstantiateExpr(te.expr.get(), visitor);
    return expr;
}

OwnedPtr<ReturnExpr> PartialInstantiation::InstantiateReturnExpr(const ReturnExpr& re, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ReturnExpr>();
    expr->returnPos = re.returnPos;
    expr->expr = InstantiateExpr(re.expr.get(), visitor);
    expr->refFuncBody = re.refFuncBody;
    return expr;
}

OwnedPtr<WhileExpr> PartialInstantiation::InstantiateWhileExpr(const WhileExpr& we, const VisitFunc& visitor)
{
    auto expr = MakeOwned<WhileExpr>();
    expr->whilePos = we.whilePos;
    expr->leftParenPos = we.leftParenPos;
    expr->condExpr = InstantiateExpr(we.condExpr.get(), visitor);
    expr->body = InstantiateExpr(we.body.get(), visitor);
    expr->rightParenPos = we.rightParenPos;
    return expr;
}

OwnedPtr<DoWhileExpr> PartialInstantiation::InstantiateDoWhileExpr(const DoWhileExpr& dwe, const VisitFunc& visitor)
{
    auto expr = MakeOwned<DoWhileExpr>();
    expr->doPos = dwe.doPos;
    expr->body = InstantiateExpr(dwe.body.get(), visitor);
    expr->whilePos = dwe.whilePos;
    expr->leftParenPos = dwe.leftParenPos;
    expr->condExpr = InstantiateExpr(dwe.condExpr.get(), visitor);
    expr->rightParenPos = dwe.rightParenPos;
    return expr;
}

OwnedPtr<AssignExpr> PartialInstantiation::InstantiateAssignExpr(const AssignExpr& ae, const VisitFunc& visitor)
{
    auto expr = MakeOwned<AssignExpr>();
    expr->leftValue = InstantiateExpr(ae.leftValue.get(), visitor);
    expr->op = ae.op;
    expr->rightExpr = InstantiateExpr(ae.rightExpr.get(), visitor);
    expr->isCompound = ae.isCompound;
    expr->assignPos = ae.assignPos;
    return expr;
}

OwnedPtr<IncOrDecExpr> PartialInstantiation::InstantiateIncOrDecExpr(const IncOrDecExpr& ide, const VisitFunc& visitor)
{
    auto expr = MakeOwned<IncOrDecExpr>();
    expr->op = ide.op;
    expr->operatorPos = ide.operatorPos;
    expr->expr = InstantiateExpr(ide.expr.get(), visitor);
    return expr;
}

OwnedPtr<UnaryExpr> PartialInstantiation::InstantiateUnaryExpr(const UnaryExpr& ue, const VisitFunc& visitor)
{
    auto expr = MakeOwned<UnaryExpr>();
    expr->op = ue.op;
    expr->expr = InstantiateExpr(ue.expr.get(), visitor);
    expr->operatorPos = ue.operatorPos;
    return expr;
}

OwnedPtr<BinaryExpr> PartialInstantiation::InstantiateBinaryExpr(const BinaryExpr& be, const VisitFunc& visitor)
{
    auto expr = MakeOwned<BinaryExpr>();
    expr->op = be.op;
    expr->leftExpr = InstantiateExpr(be.leftExpr.get(), visitor);
    expr->rightExpr = InstantiateExpr(be.rightExpr.get(), visitor);
    expr->operatorPos = be.operatorPos;
    return expr;
}

OwnedPtr<RangeExpr> PartialInstantiation::InstantiateRangeExpr(const RangeExpr& re, const VisitFunc& visitor)
{
    auto expr = MakeOwned<RangeExpr>();
    expr->startExpr = InstantiateExpr(re.startExpr.get(), visitor);
    expr->rangePos = re.rangePos;
    expr->stopExpr = InstantiateExpr(re.stopExpr.get(), visitor);
    expr->colonPos = re.colonPos;
    expr->stepExpr = InstantiateExpr(re.stepExpr.get(), visitor);
    expr->isClosed = re.isClosed;
    expr->decl = re.decl;
    return expr;
}

OwnedPtr<SubscriptExpr> PartialInstantiation::InstantiateSubscriptExpr(
    const SubscriptExpr& se, const VisitFunc& visitor)
{
    auto expr = MakeOwned<SubscriptExpr>();
    expr->baseExpr = InstantiateExpr(se.baseExpr.get(), visitor);
    expr->leftParenPos = se.leftParenPos;
    for (auto& it : se.indexExprs) {
        expr->indexExprs.push_back(InstantiateExpr(it.get(), visitor));
    }
    expr->rightParenPos = se.rightParenPos;
    expr->isTupleAccess = se.isTupleAccess;
    return expr;
}

OwnedPtr<MemberAccess> PartialInstantiation::InstantiateMemberAccess(const MemberAccess& ma, const VisitFunc& visitor)
{
    auto expr = MakeOwned<MemberAccess>();
    expr->baseExpr = InstantiateExpr(ma.baseExpr.get(), visitor);
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
        expr->typeArguments.push_back(InstantiateType(it.get(), visitor));
    }
    return expr;
}

OwnedPtr<CallExpr> PartialInstantiation::InstantiateCallExpr(const CallExpr& ce, const VisitFunc& visitor)
{
    auto expr = MakeOwned<CallExpr>();
    expr->baseFunc = InstantiateExpr(ce.baseFunc.get(), visitor);
    expr->leftParenPos = ce.leftParenPos;
    expr->needCheckToTokens = ce.needCheckToTokens;
    std::unordered_map<Ptr<FuncArg>, Ptr<FuncArg>> cloneTable; // For replace desugarArgs
    for (auto& it : ce.args) {
        expr->args.push_back(InstantiateNode(it.get(), visitor));
        cloneTable[it.get()] = expr->args.back().get();
    }
    if (ce.desugarArgs.has_value()) {
        expr->defaultArgs = std::vector<OwnedPtr<FuncArg>>();
        for (auto& it : ce.defaultArgs) {
            expr->defaultArgs.push_back(InstantiateNode(it.get(), visitor));
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

OwnedPtr<ParenExpr> PartialInstantiation::InstantiateParenExpr(const ParenExpr& pe, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ParenExpr>();
    expr->leftParenPos = pe.leftParenPos;
    expr->expr = InstantiateExpr(pe.expr.get(), visitor);
    expr->rightParenPos = pe.rightParenPos;
    return expr;
}

OwnedPtr<LambdaExpr> PartialInstantiation::InstantiateLambdaExpr(const LambdaExpr& le, const VisitFunc& visitor)
{
    auto expr = MakeOwned<LambdaExpr>(InstantiateNode(le.funcBody.get(), visitor));
    if (le.TestAttr(Attribute::MOCK_SUPPORTED)) {
        expr->EnableAttr(Attribute::MOCK_SUPPORTED);
    }
    return expr;
}

OwnedPtr<LitConstExpr> PartialInstantiation::InstantiateLitConstExpr(const LitConstExpr& lce, const VisitFunc& visitor)
{
    auto expr = MakeOwned<LitConstExpr>(lce.kind, lce.stringValue);
    expr->codepoint = lce.codepoint;
    expr->delimiterNum = lce.delimiterNum;
    expr->stringKind = lce.stringKind;
    expr->rawString = lce.rawString;
    if (lce.ref) {
        expr->ref = InstantiateRefType(*lce.ref, visitor);
        visitor(*lce.ref, *expr->ref);
    }
    if (lce.siExpr) {
        expr->siExpr = InstantiateExpr(lce.siExpr.get(), visitor);
    }
    return expr;
}

OwnedPtr<InterpolationExpr> PartialInstantiation::InstantiateInterpolationExpr(
    const InterpolationExpr& ie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<InterpolationExpr>();
    expr->rawString = ie.rawString;
    expr->dollarPos = ie.dollarPos;
    expr->block = InstantiateExpr(ie.block.get(), visitor);
    return expr;
}

OwnedPtr<StrInterpolationExpr> PartialInstantiation::InstantiateStrInterpolationExpr(
    const StrInterpolationExpr& sie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<StrInterpolationExpr>();
    expr->rawString = sie.rawString;
    expr->strParts = sie.strParts;
    for (auto& it : sie.strPartExprs) {
        expr->strPartExprs.push_back(InstantiateExpr(it.get(), visitor));
    }
    return expr;
}

OwnedPtr<ArrayLit> PartialInstantiation::InstantiateArrayLit(const ArrayLit& al, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ArrayLit>();
    expr->leftSquarePos = al.leftSquarePos;
    for (auto& it : al.children) {
        expr->children.push_back(InstantiateExpr(it.get(), visitor));
    }
    expr->commaPosVector = al.commaPosVector;
    expr->rightSquarePos = al.rightSquarePos;
    expr->initFunc = al.initFunc;
    return expr;
}

OwnedPtr<ArrayExpr> PartialInstantiation::InstantiateArrayExpr(const ArrayExpr& ae, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ArrayExpr>();
    expr->type = InstantiateType(ae.type.get(), visitor);
    expr->leftParenPos = ae.leftParenPos;
    expr->args.resize(ae.args.size());
    for (size_t i = 0; i < ae.args.size(); ++i) {
        expr->args[i] = InstantiateNode(ae.args[i].get(), visitor);
    }
    expr->commaPosVector = ae.commaPosVector;
    expr->rightParenPos = ae.rightParenPos;
    expr->initFunc = ae.initFunc;
    expr->isValueArray = ae.isValueArray;
    return expr;
}

OwnedPtr<PointerExpr> PartialInstantiation::InstantiatePointerExpr(const PointerExpr& ptre, const VisitFunc& visitor)
{
    auto expr = MakeOwned<PointerExpr>();
    expr->type = InstantiateType(ptre.type.get(), visitor);
    expr->arg = InstantiateNode(ptre.arg.get(), visitor);
    return expr;
}

OwnedPtr<TupleLit> PartialInstantiation::InstantiateTupleLit(const TupleLit& tl, const VisitFunc& visitor)
{
    auto expr = MakeOwned<TupleLit>();
    expr->leftParenPos = tl.leftParenPos;
    for (auto& it : tl.children) {
        expr->children.push_back(InstantiateExpr(it.get(), visitor));
    }
    expr->commaPosVector = tl.commaPosVector;
    expr->rightParenPos = tl.rightParenPos;
    return expr;
}

OwnedPtr<RefExpr> PartialInstantiation::InstantiateRefExpr(const RefExpr& re, const VisitFunc& visitor)
{
    auto expr = CreateRefExpr(re.ref.identifier);
    expr->leftAnglePos = re.leftAnglePos;
    expr->ref.target = re.ref.target;
    expr->ref.targets = re.ref.targets;
    for (auto& it : re.typeArguments) {
        expr->typeArguments.push_back(InstantiateType(it.get(), visitor));
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

OwnedPtr<ForInExpr> PartialInstantiation::InstantiateForInExpr(const ForInExpr& fie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<ForInExpr>();
    expr->leftParenPos = fie.leftParenPos;
    expr->rightParenPos = fie.rightParenPos;
    expr->pattern = InstantiatePattern(fie.pattern.get(), visitor);
    expr->inPos = fie.inPos;
    expr->inExpression = InstantiateExpr(fie.inExpression.get(), visitor);
    expr->wherePos = fie.wherePos;
    expr->patternGuard = InstantiateExpr(fie.patternGuard.get(), visitor);
    expr->patternInDesugarExpr = fie.patternInDesugarExpr;
    expr->body = InstantiateExpr(fie.body.get(), visitor);
    expr->forInKind = fie.forInKind;
    return expr;
}

OwnedPtr<MatchExpr> PartialInstantiation::InstantiateMatchExpr(const MatchExpr& me, const VisitFunc& visitor)
{
    auto expr = MakeOwned<MatchExpr>();
    expr->matchMode = me.matchMode;
    expr->sugarKind = me.sugarKind;
    expr->selector = InstantiateExpr(me.selector.get(), visitor);
    for (auto& i : me.matchCases) {
        expr->matchCases.push_back(InstantiateNode(i.get(), visitor));
    }
    for (auto& i : me.matchCaseOthers) {
        expr->matchCaseOthers.push_back(InstantiateNode(i.get(), visitor));
    }
    expr->leftParenPos = me.leftParenPos;
    expr->rightParenPos = me.rightParenPos;
    expr->leftCurlPos = me.leftCurlPos;
    expr->rightCurlPos = me.rightCurlPos;
    return expr;
}

OwnedPtr<JumpExpr> PartialInstantiation::InstantiateJumpExpr(const JumpExpr& je)
{
    auto expr = MakeOwned<JumpExpr>();
    expr->isBreak = je.isBreak;
    expr->refLoop = je.refLoop;
    return expr;
}

OwnedPtr<TypeConvExpr> PartialInstantiation::InstantiateTypeConvExpr(const TypeConvExpr& tce, const VisitFunc& visitor)
{
    auto expr = MakeOwned<TypeConvExpr>();
    expr->type = OwnedPtr<Type>(InstantiateType(tce.type.get(), visitor).release());
    expr->expr = InstantiateExpr(tce.expr.get(), visitor);
    expr->leftParenPos = tce.leftParenPos;
    expr->rightParenPos = tce.rightParenPos;
    return expr;
}

OwnedPtr<SpawnExpr> PartialInstantiation::InstantiateSpawnExpr(const SpawnExpr& se, const VisitFunc& visitor)
{
    auto expr = MakeOwned<SpawnExpr>();
    expr->task = InstantiateExpr(se.task.get(), visitor);
    expr->arg = InstantiateExpr(se.arg.get(), visitor);
    if (se.futureObj) {
        auto obj = InstantiateDecl(se.futureObj.get(), visitor);
        expr->futureObj = OwnedPtr<VarDecl>(As<ASTKind::VAR_DECL>(obj.release()));
    }
    expr->spawnPos = se.spawnPos;
    expr->leftParenPos = se.leftParenPos;
    expr->rightParenPos = se.rightParenPos;
    return expr;
}

OwnedPtr<SynchronizedExpr> PartialInstantiation::InstantiateSynchronizedExpr(
    const SynchronizedExpr& se, const VisitFunc& visitor)
{
    auto expr = MakeOwned<SynchronizedExpr>();
    expr->mutex = InstantiateExpr(se.mutex.get(), visitor);
    expr->body = InstantiateExpr(se.body.get(), visitor);
    expr->syncPos = se.syncPos;
    expr->leftParenPos = se.leftParenPos;
    expr->rightParenPos = se.rightParenPos;
    return expr;
}

OwnedPtr<InvalidExpr> PartialInstantiation::InstantiateInvalidExpr(const InvalidExpr& ie)
{
    auto expr = MakeOwned<InvalidExpr>(ie.begin);
    expr->value = ie.value;
    return expr;
}

OwnedPtr<TrailingClosureExpr> PartialInstantiation::InstantiateTrailingClosureExpr(
    const TrailingClosureExpr& tc, const VisitFunc& visitor)
{
    auto expr = MakeOwned<TrailingClosureExpr>();
    expr->leftLambda = tc.leftLambda;
    expr->rightLambda = tc.rightLambda;
    if (tc.expr) {
        expr->expr = InstantiateExpr(tc.expr.get(), visitor);
    }
    if (tc.lambda) {
        expr->lambda = InstantiateExpr(tc.lambda.get(), visitor);
    }
    return expr;
}

OwnedPtr<IsExpr> PartialInstantiation::InstantiateIsExpr(const IsExpr& ie, const VisitFunc& visitor)
{
    auto expr = MakeOwned<IsExpr>();
    expr->leftExpr = InstantiateExpr(ie.leftExpr.get(), visitor);
    expr->isType = InstantiateType(ie.isType.get(), visitor);
    expr->isPos = ie.isPos;
    return expr;
}

OwnedPtr<AsExpr> PartialInstantiation::InstantiateAsExpr(const AsExpr& ae, const VisitFunc& visitor)
{
    auto expr = MakeOwned<AsExpr>();
    expr->leftExpr = InstantiateExpr(ae.leftExpr.get(), visitor);
    expr->asType = InstantiateType(ae.asType.get(), visitor);
    expr->asPos = ae.asPos;
    return expr;
}

OwnedPtr<OptionalExpr> PartialInstantiation::InstantiateOptionalExpr(const OptionalExpr& oe, const VisitFunc& visitor)
{
    auto expr = MakeOwned<OptionalExpr>();
    expr->baseExpr = InstantiateExpr(oe.baseExpr.get(), visitor);
    expr->questPos = oe.questPos;
    return expr;
}

OwnedPtr<OptionalChainExpr> PartialInstantiation::InstantiateOptionalChainExpr(
    const OptionalChainExpr& oce, const VisitFunc& visitor)
{
    auto expr = MakeOwned<OptionalChainExpr>();
    expr->expr = InstantiateExpr(oce.expr.get(), visitor);
    return expr;
}

OwnedPtr<LetPatternDestructor> PartialInstantiation::InstantiateLetPatternDestructor(
    const LetPatternDestructor& ldp, const VisitFunc& visitor)
{
    auto expr = MakeOwned<LetPatternDestructor>();
    expr->backarrowPos = ldp.backarrowPos;
    for (auto& p : ldp.patterns) {
        expr->patterns.push_back(InstantiatePattern(p.get(), visitor));
    }
    expr->orPos = ldp.orPos;
    expr->initializer = InstantiateExpr(ldp.initializer.get(), visitor);
    return expr;
}

/**
 * NOTE: To guarantee the members of Expr is copied, the sub method should not be called outside 'InstantiateExpr'.
 */
template <typename ExprT>
OwnedPtr<ExprT> PartialInstantiation::InstantiateExpr(Ptr<ExprT> expr, const VisitFunc& visitor)
{
    if (!expr) {
        return OwnedPtr<ExprT>();
    }
    auto clonedExpr = match(*expr)(
        // PrimitiveExpr, AdjointExpr are ignored.
        [&visitor](const IfExpr& ie) { return OwnedPtr<Expr>(InstantiateIfExpr(ie, visitor)); },
        [](const PrimitiveTypeExpr& pte) { return OwnedPtr<Expr>(MakeOwned<PrimitiveTypeExpr>(pte.typeKind)); },
        [&visitor](const MacroExpandExpr& mee) { return OwnedPtr<Expr>(InstantiateMacroExpandExpr(mee, visitor)); },
        [&visitor](const TokenPart& tp) { return OwnedPtr<Expr>(InstantiateTokenPart(tp, visitor)); },
        [&visitor](const QuoteExpr& qe) { return OwnedPtr<Expr>(InstantiateQuoteExpr(qe, visitor)); },
        [&visitor](const TryExpr& te) { return OwnedPtr<Expr>(InstantiateTryExpr(te, visitor)); },
        [&visitor](const ThrowExpr& te) { return OwnedPtr<Expr>(InstantiateThrowExpr(te, visitor)); },
        [&visitor](const ReturnExpr& re) { return OwnedPtr<Expr>(InstantiateReturnExpr(re, visitor)); },
        [&visitor](const WhileExpr& we) { return OwnedPtr<Expr>(InstantiateWhileExpr(we, visitor)); },
        [&visitor](const DoWhileExpr& dwe) { return OwnedPtr<Expr>(InstantiateDoWhileExpr(dwe, visitor)); },
        [&visitor](const AssignExpr& ae) { return OwnedPtr<Expr>(InstantiateAssignExpr(ae, visitor)); },
        [&visitor](const IncOrDecExpr& ide) { return OwnedPtr<Expr>(InstantiateIncOrDecExpr(ide, visitor)); },
        [&visitor](const UnaryExpr& ue) { return OwnedPtr<Expr>(InstantiateUnaryExpr(ue, visitor)); },
        [&visitor](const BinaryExpr& be) { return OwnedPtr<Expr>(InstantiateBinaryExpr(be, visitor)); },
        [&visitor](const RangeExpr& re) { return OwnedPtr<Expr>(InstantiateRangeExpr(re, visitor)); },
        [&visitor](const SubscriptExpr& se) { return OwnedPtr<Expr>(InstantiateSubscriptExpr(se, visitor)); },
        [&visitor](const MemberAccess& ma) { return OwnedPtr<Expr>(InstantiateMemberAccess(ma, visitor)); },
        [&visitor](const CallExpr& ce) { return OwnedPtr<Expr>(InstantiateCallExpr(ce, visitor)); },
        [&visitor](const ParenExpr& pe) { return OwnedPtr<Expr>(InstantiateParenExpr(pe, visitor)); },
        [&visitor](const LambdaExpr& le) { return OwnedPtr<Expr>(InstantiateLambdaExpr(le, visitor)); },
        [&visitor](const LitConstExpr& lce) { return OwnedPtr<Expr>(InstantiateLitConstExpr(lce, visitor)); },
        [&visitor](const ArrayLit& al) { return OwnedPtr<Expr>(InstantiateArrayLit(al, visitor)); },
        [&visitor](const ArrayExpr& asl) { return OwnedPtr<Expr>(InstantiateArrayExpr(asl, visitor)); },
        [&visitor](const PointerExpr& ptre) { return OwnedPtr<Expr>(InstantiatePointerExpr(ptre, visitor)); },
        [&visitor](const TupleLit& tl) { return OwnedPtr<Expr>(InstantiateTupleLit(tl, visitor)); },
        [&visitor](const RefExpr& re) { return OwnedPtr<Expr>(InstantiateRefExpr(re, visitor)); },
        [&visitor](const ForInExpr& fie) { return OwnedPtr<Expr>(InstantiateForInExpr(fie, visitor)); },
        [&visitor](const MatchExpr& me) { return OwnedPtr<Expr>(InstantiateMatchExpr(me, visitor)); },
        [](const JumpExpr& je) { return OwnedPtr<Expr>(InstantiateJumpExpr(je)); },
        [&visitor](const TypeConvExpr& e) { return OwnedPtr<Expr>(InstantiateTypeConvExpr(e, visitor)); },
        [&visitor](const SpawnExpr& se) { return OwnedPtr<Expr>(InstantiateSpawnExpr(se, visitor)); },
        [&visitor](const SynchronizedExpr& se) { return OwnedPtr<Expr>(InstantiateSynchronizedExpr(se, visitor)); },
        [](const InvalidExpr& ie) { return OwnedPtr<Expr>(InstantiateInvalidExpr(ie)); },
        [&visitor](const Block& b) { return OwnedPtr<Expr>(InstantiateBlock(b, visitor)); },
        [&visitor](const InterpolationExpr& ie) { return OwnedPtr<Expr>(InstantiateInterpolationExpr(ie, visitor)); },
        [&visitor](
            const StrInterpolationExpr& sie) { return OwnedPtr<Expr>(InstantiateStrInterpolationExpr(sie, visitor)); },
        [&visitor](
            const TrailingClosureExpr& tc) { return OwnedPtr<Expr>(InstantiateTrailingClosureExpr(tc, visitor)); },
        [&visitor](const IsExpr& ie) { return OwnedPtr<Expr>(InstantiateIsExpr(ie, visitor)); },
        [&visitor](const AsExpr& ae) { return OwnedPtr<Expr>(InstantiateAsExpr(ae, visitor)); },
        [&visitor](const OptionalExpr& oe) { return OwnedPtr<Expr>(InstantiateOptionalExpr(oe, visitor)); },
        [&visitor](const OptionalChainExpr& oe) { return OwnedPtr<Expr>(InstantiateOptionalChainExpr(oe, visitor)); },
        [](const WildcardExpr& /* we */) { return OwnedPtr<Expr>(MakeOwned<WildcardExpr>()); },
        [&visitor](
            const LetPatternDestructor& ld) { return OwnedPtr<Expr>(InstantiateLetPatternDestructor(ld, visitor)); },
        [&expr]() {
            // Invalid and ignored cases.
            auto invalidExpr = MakeOwned<InvalidExpr>(expr->begin);
            return OwnedPtr<Expr>(invalidExpr.release());
        });
    CJC_ASSERT(clonedExpr);
    if (clonedExpr->astKind != ASTKind::INVALID_EXPR) {
        CopyNodeField(clonedExpr.get(), *expr);
        // Instantiate field in Expr.
        clonedExpr->isConst = expr->isConst;
        clonedExpr->isBaseFunc = expr->isBaseFunc;
        clonedExpr->isInFlowExpr = expr->isInFlowExpr;
        clonedExpr->constNumValue = expr->constNumValue;
        clonedExpr->hasSemi = expr->hasSemi;
        clonedExpr->mapExpr = (expr->mapExpr == expr) ? clonedExpr.get() : expr->mapExpr;
        clonedExpr->sourceExpr = expr->sourceExpr;
        clonedExpr->overflowStrategy = expr->overflowStrategy;
        if (expr->desugarExpr && !clonedExpr->desugarExpr) {
            clonedExpr->desugarExpr = InstantiateExpr(expr->desugarExpr.get(), visitor);
        }
        clonedExpr->EnableAttr(Attribute::COMPILER_ADD);
        visitor(*expr, *clonedExpr);
    }
    auto result = DynamicCast<ExprT*>(clonedExpr.release());
    CJC_NULLPTR_CHECK(result);
    return OwnedPtr<ExprT>(result);
}

/**
 * NOTE: To guarantee the members of Decl is copied, the sub method should not be called outside 'InstantiateDecl'.
 */
OwnedPtr<Decl> PartialInstantiation::InstantiateDecl(Ptr<Decl> decl, const VisitFunc& visitor)
{
    if (!decl) {
        return OwnedPtr<Decl>();
    }
    auto ret = match(*decl)([](const GenericParamDecl& e) { return InstantiateGenericParamDecl(e); },
        [&visitor](const PropDecl& e) { return InstantiatePropDecl(e, visitor); },
        [&visitor](const VarDecl& e) { return InstantiateVarDecl(e, visitor); },
        [&visitor](const FuncDecl& e) { return InstantiateFuncDecl(e, visitor); },
        [&visitor](const StructDecl& e) { return InstantiateStructDecl(e, visitor); },
        [&visitor](const ClassDecl& e) { return InstantiateClassDecl(e, visitor); },
        [&visitor](const InterfaceDecl& e) { return InstantiateInterfaceDecl(e, visitor); },
        [&visitor](const EnumDecl& e) { return InstantiateEnumDecl(e, visitor); },
        [&visitor](const PrimaryCtorDecl& e) { return InstantiatePrimaryCtorDecl(e, visitor); },
        [&visitor](const ExtendDecl& e) { return InstantiateExtendDecl(e, visitor); },
        [](const MacroExpandDecl& e) { return InstantiateMacroExpandDecl(e); },
        [&visitor](const VarWithPatternDecl& e) { return InstantiateVarWithPatternDecl(e, visitor); },
        [&visitor](const TypeAliasDecl& e) { return InstantiateTypeAliasDecl(e, visitor); },
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
    // Instantiate field in Decl.
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
        ret->annotations.emplace_back(InstantiateNode(anno.get(), visitor));
    }
    ret->annotationsArray = InstantiateNode(decl->annotationsArray.get(), visitor);
    visitor(*decl, *ret);
    return ret;
}

OwnedPtr<ConstPattern> PartialInstantiation::InstantiateConstPattern(const ConstPattern& cp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ConstPattern>();
    ret->literal = InstantiateExpr(cp.literal.get(), visitor);
    ret->operatorCallExpr = InstantiateExpr(cp.operatorCallExpr.get(), visitor);
    return ret;
}

OwnedPtr<VarPattern> PartialInstantiation::InstantiateVarPattern(const VarPattern& vp, const VisitFunc& visitor)
{
    OwnedPtr<VarPattern> ret;
    if (vp.varDecl) {
        ret = MakeOwned<VarPattern>(vp.varDecl->identifier, vp.begin);
        auto d = InstantiateDecl(vp.varDecl.get(), visitor);
        ret->varDecl.reset(As<ASTKind::VAR_DECL>(d.release()));
    } else {
        ret = MakeOwned<VarPattern>();
    }
    ret->desugarExpr = InstantiateExpr(vp.desugarExpr.get(), visitor);
    return ret;
}

OwnedPtr<TuplePattern> PartialInstantiation::InstantiateTuplePattern(const TuplePattern& tp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<TuplePattern>();
    for (auto& i : tp.patterns) {
        ret->patterns.push_back(InstantiatePattern(i.get(), visitor));
    }
    ret->leftBracePos = tp.leftBracePos;
    ret->rightBracePos = tp.rightBracePos;
    ret->commaPosVector = tp.commaPosVector;
    return ret;
}

OwnedPtr<TypePattern> PartialInstantiation::InstantiateTypePattern(const TypePattern& tp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<TypePattern>();
    ret->pattern = InstantiatePattern(tp.pattern.get(), visitor);
    ret->type = InstantiateType(tp.type.get(), visitor);
    ret->colonPos = tp.colonPos;

    if (tp.desugarExpr != nullptr) {
        ret->desugarExpr = InstantiateExpr(tp.desugarExpr.get(), visitor);
    }
    if (tp.desugarVarPattern != nullptr) {
        ret->desugarVarPattern = PartialInstantiation::Instantiate(tp.desugarVarPattern.get(), visitor);
    }

    ret->needRuntimeTypeCheck = tp.needRuntimeTypeCheck;
    ret->matchBeforeRuntime = tp.matchBeforeRuntime;
    return ret;
}

OwnedPtr<EnumPattern> PartialInstantiation::InstantiateEnumPattern(const EnumPattern& ep, const VisitFunc& visitor)
{
    auto ret = MakeOwned<EnumPattern>();
    ret->constructor = InstantiateExpr(ep.constructor.get(), visitor);
    for (auto& p : ep.patterns) {
        ret->patterns.emplace_back(InstantiatePattern(p.get(), visitor));
    }
    ret->leftParenPos = ep.leftParenPos;
    ret->rightParenPos = ep.rightParenPos;
    ret->commaPosVector = ep.commaPosVector;
    return ret;
}

OwnedPtr<ExceptTypePattern> PartialInstantiation::InstantiateExceptTypePattern(
    const ExceptTypePattern& etp, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ExceptTypePattern>();
    ret->pattern = InstantiatePattern(etp.pattern.get(), visitor);
    for (auto& i : etp.types) {
        ret->types.push_back(InstantiateType(i.get(), visitor));
    }
    ret->patternPos = etp.patternPos;
    ret->colonPos = etp.colonPos;
    ret->bitOrPosVector = etp.bitOrPosVector;
    return ret;
}

OwnedPtr<VarOrEnumPattern> PartialInstantiation::InstantiateVarOrEnumPattern(
    const VarOrEnumPattern& vep, const VisitFunc& visitor)
{
    auto ret = MakeOwned<VarOrEnumPattern>(vep.identifier);
    if (vep.pattern) {
        ret->pattern = InstantiatePattern(vep.pattern.get(), visitor);
    }
    return ret;
}

/**
 * NOTE: To guarantee the members of Pattern is copied, the sub method should not be called outside
 * 'InstantiatePattern'.
 */
OwnedPtr<Pattern> PartialInstantiation::InstantiatePattern(Ptr<Pattern> pattern, const VisitFunc& visitor)
{
    if (!pattern) {
        return OwnedPtr<Pattern>();
    }
    auto ret = match(*pattern)(
        [&visitor](const ConstPattern& e) { return OwnedPtr<Pattern>(InstantiateConstPattern(e, visitor)); },
        [](const WildcardPattern& e) { return OwnedPtr<Pattern>(MakeOwned<WildcardPattern>(e)); },
        [&visitor](const VarPattern& e) { return OwnedPtr<Pattern>(InstantiateVarPattern(e, visitor)); },
        [&visitor](const TuplePattern& e) { return OwnedPtr<Pattern>(InstantiateTuplePattern(e, visitor)); },
        [&visitor](const TypePattern& e) { return OwnedPtr<Pattern>(InstantiateTypePattern(e, visitor)); },
        [&visitor](const EnumPattern& e) { return OwnedPtr<Pattern>(InstantiateEnumPattern(e, visitor)); },
        [&visitor](const ExceptTypePattern& e) { return OwnedPtr<Pattern>(InstantiateExceptTypePattern(e, visitor)); },
        [&visitor](const VarOrEnumPattern& e) { return OwnedPtr<Pattern>(InstantiateVarOrEnumPattern(e, visitor)); },
        [](const InvalidPattern& e) { return OwnedPtr<Pattern>(MakeOwned<InvalidPattern>(e)); },
        []() { return OwnedPtr<Pattern>(MakeOwned<InvalidPattern>()); });
    CJC_ASSERT(ret && ret->astKind == pattern->astKind);
    CopyNodeField(ret.get(), *pattern);
    // Instantiate field in Pattern.
    ret->ctxExpr = pattern->ctxExpr;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    visitor(*pattern, *ret);
    return ret;
}

OwnedPtr<Block> PartialInstantiation::InstantiateBlock(const Block& block, const VisitFunc& visitor)
{
    auto ret = MakeOwned<Block>();
    CopyNodeField(ret.get(), block);
    // Instantiate field in Block.
    ret->unsafePos = block.unsafePos;
    ret->leftCurlPos = block.leftCurlPos;
    for (auto& it : block.body) {
        ret->body.push_back(InstantiateNode(it.get(), visitor));
    }
    ret->rightCurlPos = block.rightCurlPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<ClassBody> PartialInstantiation::InstantiateClassBody(const ClassBody& cb, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ClassBody>();
    CopyNodeField(ret.get(), cb);
    // Instantiate field in ClassBody.
    ret->leftCurlPos = cb.leftCurlPos;
    for (auto& it : cb.decls) {
        // not need to be instantiated:
        // 1. prop decl and primary ctor decl are desugard as function decl
        // 2. function decl need be instantiated by requirement
        // 3. static member var can't be instantiated, it should be regarded as global var decl
        // 4. static init func can't be instantiated, only call once in template
        // need to be instantiated:
        // 1. function decl which marked with @Frozen
        // 2. member var decl, maybe we need instantiated class decl's memory info
        if (it->astKind == ASTKind::PRIMARY_CTOR_DECL ||
            (it->astKind != ASTKind::VAR_DECL && !RequireInstantiation(*it)) || IsStaticVar(*it) ||
            IsStaticInitializer(*it)) {
            continue;
        }
        ret->decls.push_back(InstantiateDecl(it.get(), visitor));
    }
    ret->rightCurlPos = cb.rightCurlPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<StructBody> PartialInstantiation::InstantiateStructBody(const StructBody& sb, const VisitFunc& visitor)
{
    auto ret = MakeOwned<StructBody>();
    CopyNodeField(ret.get(), sb);
    ret->leftCurlPos = sb.leftCurlPos;
    for (auto& it : sb.decls) {
        if (it->astKind == ASTKind::PRIMARY_CTOR_DECL ||
            (it->astKind != ASTKind::VAR_DECL && !RequireInstantiation(*it)) || IsStaticVar(*it)) {
            continue;
        }
        ret->decls.push_back(InstantiateDecl(it.get(), visitor));
    }
    ret->rightCurlPos = sb.rightCurlPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<InterfaceBody> PartialInstantiation::InstantiateInterfaceBody(
    const InterfaceBody& ib, const VisitFunc& visitor)
{
    auto ret = MakeOwned<InterfaceBody>();
    CopyNodeField(ret.get(), ib);
    // Instantiate field in InterfaceBody.
    ret->leftCurlPos = ib.leftCurlPos;
    for (auto& it : ib.decls) {
        if (it->astKind == ASTKind::PROP_DECL || (it->astKind == ASTKind::FUNC_DECL && !RequireInstantiation(*it))) {
            continue;
        }
        ret->decls.push_back(InstantiateDecl(it.get(), visitor));
    }
    ret->rightCurlPos = ib.rightCurlPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<GenericConstraint> PartialInstantiation::InstantiateGenericConstraint(
    const GenericConstraint& gc, const VisitFunc& visitor)
{
    auto ret = MakeOwned<GenericConstraint>();
    CopyNodeField(ret.get(), gc);
    // Instantiate field in GenericConstraint.
    ret->type.reset(As<ASTKind::REF_TYPE>(InstantiateType(gc.type.get(), visitor).release()));
    for (auto& upperBound : gc.upperBounds) {
        ret->upperBounds.push_back(InstantiateType(upperBound.get(), visitor));
    }
    ret->wherePos = gc.wherePos;
    ret->operatorPos = gc.operatorPos;
    ret->bitAndPos = gc.bitAndPos;
    ret->commaPos = gc.commaPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<FuncBody> PartialInstantiation::InstantiateFuncBody(const FuncBody& fb, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncBody>();
    CopyNodeField(ret.get(), fb);
    // Instantiate field in FuncBody.
    for (auto& it : fb.paramLists) {
        ret->paramLists.push_back(InstantiateNode(it.get(), visitor));
    }
    ret->doubleArrowPos = fb.doubleArrowPos;
    ret->colonPos = fb.colonPos;
    ret->retType = InstantiateType(fb.retType.get(), visitor);
    ret->body = InstantiateExpr(fb.body.get(), visitor);
    if (fb.generic) {
        ret->generic = InstantiateGeneric(*fb.generic, visitor);
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

OwnedPtr<FuncParamList> PartialInstantiation::InstantiateFuncParamList(
    const FuncParamList& fpl, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncParamList>();
    CopyNodeField(ret.get(), fpl);
    // Instantiate field in FuncParamList.
    ret->leftParenPos = fpl.leftParenPos;
    for (auto& it : fpl.params) {
        auto funcParam = MakeOwned<FuncParam>();
        auto d = InstantiateDecl(it.get(), visitor);
        funcParam.reset(As<ASTKind::FUNC_PARAM>(d.release()));
        ret->params.push_back(std::move(funcParam));
    }
    ret->variadicArgIndex = fpl.variadicArgIndex;
    ret->hasVariableLenArg = fpl.hasVariableLenArg;
    ret->rightParenPos = fpl.rightParenPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<FuncArg> PartialInstantiation::InstantiateFuncArg(const FuncArg& fa, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncArg>();
    CopyNodeField(ret.get(), fa);
    // Instantiate field in FuncArg.
    ret->name = fa.name;
    ret->withInout = fa.withInout;
    ret->colonPos = fa.colonPos;
    ret->expr = InstantiateExpr(fa.expr.get(), visitor);
    ret->commaPos = fa.commaPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Annotation> PartialInstantiation::InstantiateAnnotation(const Annotation& annotation, const VisitFunc& visitor)
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
        ret->args.emplace_back(InstantiateNode(arg.get(), visitor));
    }
    ret->condExpr = InstantiateExpr(annotation.condExpr.get());
    ret->baseExpr = InstantiateExpr(annotation.baseExpr.get());
    return ret;
}

OwnedPtr<ImportSpec> PartialInstantiation::InstantiateImportSpec(const ImportSpec& is, const VisitFunc& visitor)
{
    auto ret = MakeOwned<ImportSpec>();
    CopyNodeField(ret.get(), is);
    if (is.modifier) {
        // Instantiate Modifier
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
        ret->annotations.emplace_back(InstantiateNode(it.get(), visitor));
    }
    return ret;
}

OwnedPtr<MatchCase> PartialInstantiation::InstantiateMatchCase(const MatchCase& mc, const VisitFunc& visitor)
{
    auto ret = MakeOwned<MatchCase>();
    CopyNodeField(ret.get(), mc);
    // Instantiate field in MatchCase.
    for (auto& pattern : mc.patterns) {
        ret->patterns.emplace_back(InstantiatePattern(pattern.get(), visitor));
    }
    ret->patternGuard = InstantiateExpr(mc.patternGuard.get(), visitor);
    ret->exprOrDecls = InstantiateExpr(mc.exprOrDecls.get(), visitor);
    ret->wherePos = mc.wherePos;
    ret->arrowPos = mc.arrowPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    ret->bitOrPosVector = mc.bitOrPosVector;
    return ret;
}

OwnedPtr<MatchCaseOther> PartialInstantiation::InstantiateMatchCaseOther(
    const MatchCaseOther& mco, const VisitFunc& visitor)
{
    auto ret = MakeOwned<MatchCaseOther>();
    CopyNodeField(ret.get(), mco);
    // Instantiate field in MatchCaseOther.
    ret->matchExpr = InstantiateExpr(mco.matchExpr.get(), visitor);
    ret->exprOrDecls = InstantiateExpr(mco.exprOrDecls.get(), visitor);
    ret->arrowPos = mco.arrowPos;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

// Instantiate Node.
template <typename NodeT>
OwnedPtr<NodeT> PartialInstantiation::InstantiateNode(Ptr<NodeT> node, const VisitFunc& visitor)
{
    if (!node) {
        return OwnedPtr<NodeT>();
    }
    auto clonedNode = match(*node)(
        // NOTE: Package, File, PackageSpec, ImportSpec, Modifier are ignored.
        // EnumBody struct is empty, no need to be cloned
        [&visitor](Type& type) { return OwnedPtr<Node>(InstantiateType(Ptr(&type), visitor)); },
        [&visitor](Expr& expr) { return OwnedPtr<Node>(InstantiateExpr(Ptr(&expr), visitor)); },
        [&visitor](Decl& decl) { return OwnedPtr<Node>(InstantiateDecl(Ptr(&decl), visitor)); },
        [&visitor](Pattern& pattern) { return OwnedPtr<Node>(InstantiatePattern(Ptr(&pattern), visitor)); },
        [&visitor](const ClassBody& cb) { return OwnedPtr<Node>(InstantiateClassBody(cb, visitor)); },
        [&visitor](const StructBody& rb) { return OwnedPtr<Node>(InstantiateStructBody(rb, visitor)); },
        [&visitor](const InterfaceBody& ib) { return OwnedPtr<Node>(InstantiateInterfaceBody(ib, visitor)); },
        [&visitor](const GenericConstraint& gc) { return OwnedPtr<Node>(InstantiateGenericConstraint(gc, visitor)); },
        [&visitor](const FuncBody& fb) { return OwnedPtr<Node>(InstantiateFuncBody(fb, visitor)); },
        [&visitor](const FuncParamList& fpl) { return OwnedPtr<Node>(InstantiateFuncParamList(fpl, visitor)); },
        [&visitor](const FuncArg& fa) { return OwnedPtr<Node>(InstantiateFuncArg(fa, visitor)); },
        [&visitor](const MatchCase& mc) { return OwnedPtr<Node>(InstantiateMatchCase(mc, visitor)); },
        [&visitor](const MatchCaseOther& mco) { return OwnedPtr<Node>(InstantiateMatchCaseOther(mco, visitor)); },
        [&visitor](const Annotation& ann) { return OwnedPtr<Node>(InstantiateAnnotation(ann, visitor)); },
        [](const DummyBody&) { return OwnedPtr<Node>(OwnedPtr<DummyBody>()); },
        [&visitor](const ImportSpec& is) { return OwnedPtr<Node>(InstantiateImportSpec(is, visitor)); },
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

OwnedPtr<Node> PartialInstantiation::InstantiateWithRearrange(Ptr<Node> node, const VisitFunc& visitor)
{
    VisitFunc collectMap = [this, visitor](Node& from, Node& target) {
        // Collect decl to decl map.
        if (auto decl = DynamicCast<Decl*>(&from); decl) {
            source2cloned[decl] = &target;
            ins2generic[StaticCast<Decl>(&target)] = decl;
            if (auto found = generic2ins.find(decl); found != generic2ins.end()) {
                found->second.emplace(StaticCast<Decl>(&target));
            } else {
                generic2ins[decl] = {StaticCast<Decl>(&target)};
            }
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
                /**
                 * Whether a static member function uses virtual calls depends on the called expression.
                 * If the invoked function is not in a type with `open` semantics, instantiation can be directly invoked
                 * through static type invoking.
                 * For example, `class A<T> { static func foo() {...} }`
                 *   Instantated to `class A<Int64> { static func foo() {...} }`
                 *   `A<Int64>.foo()` will pointer to `foo` in `A<Int64>`.
                 *   But if `A<T>` is modified by `open`, `foo` will point to function in `A<T>`.
                 */
                bool isCallStaticMemberByVirtual = reFrom.ref.target && reFrom.ref.target->IsFunc() &&
                    reFrom.ref.target->TestAttr(Attribute::STATIC) && reFrom.ref.target->outerDecl &&
                    IsOpenDecl(*reFrom.ref.target->outerDecl);
                if (!isCallStaticMemberByVirtual) {
                    TargetAddrMapInsert(reFrom.ref.target, reTarget.ref.target);
                }
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
    OwnedPtr<Node> targetNode = InstantiateNode(node, collectMap);
    // Rearrange pointer to node pointer's target from source node pointer to cloned node pointer.
    for (auto& [s, t] : targetAddr2targetAddr) {
        if (source2cloned.find(*s) != source2cloned.end()) {
            *t = source2cloned[*s];
        }
    }
    return targetNode;
}

Ptr<Ty> TyGeneralizer::Generalize(Ty& ty)
{
    if (typeMapping.empty()) {
        return &ty;
    }
    if (auto found = typeMapping.find(&ty); found != typeMapping.end()) {
        return found->second;
    }
    switch (ty.kind) {
        case TypeKind::TYPE_FUNC: {
            std::vector<Ptr<Ty>> paramTys;
            auto& funcTy = static_cast<FuncTy&>(ty);
            for (auto& it : funcTy.paramTys) {
                paramTys.push_back(Generalize(it));
            }
            auto retType = Generalize(funcTy.retTy);
            Ptr<Ty> ret = tyMgr.GetFunctionTy(
                paramTys, retType, {funcTy.IsCFunc(), funcTy.isClosureTy, funcTy.hasVariableLenArg});
            return ret;
        }
        case TypeKind::TYPE_TUPLE: {
            std::vector<Ptr<Ty>> typeArgs;
            std::transform(ty.typeArgs.begin(), ty.typeArgs.end(), std::back_inserter(typeArgs),
                [this](auto it) { return Generalize(it); });
            return tyMgr.GetTupleTy(typeArgs, static_cast<TupleTy&>(ty).isClosureTy);
        }
        case TypeKind::TYPE_ARRAY:
            return GetGeneralizedArrayTy(static_cast<ArrayTy&>(ty));
        case TypeKind::TYPE_POINTER:
            return GetGeneralizedPointerTy(static_cast<PointerTy&>(ty));
        case TypeKind::TYPE_STRUCT:
            return GetGeneralizedStructTy(static_cast<StructTy&>(ty));
        case TypeKind::TYPE_CLASS:
            return GetGeneralizedClassTy(static_cast<ClassTy&>(ty));
        case TypeKind::TYPE_INTERFACE:
            return GetGeneralizedInterfaceTy(static_cast<InterfaceTy&>(ty));
        case TypeKind::TYPE_ENUM:
            return GetGeneralizedEnumTy(static_cast<EnumTy&>(ty));
        case TypeKind::TYPE: {
            std::vector<Ptr<Ty>> typeArgs;
            for (auto& it : ty.typeArgs) {
                typeArgs.push_back(Generalize(it));
            }
            return tyMgr.GetTypeAliasTy(*static_cast<TypeAliasTy&>(ty).declPtr, typeArgs);
        }
        case TypeKind::TYPE_INTERSECTION:
            return GetGeneralizedSetTy(static_cast<IntersectionTy&>(ty));
        case TypeKind::TYPE_UNION:
            return GetGeneralizedSetTy(static_cast<UnionTy&>(ty));
        default:;
    }
    return &ty;
}

Ptr<Ty> TyGeneralizer::GetGeneralizedStructTy(StructTy& structTy)
{
    // If is a struct without generic parameter, no need do instantiation.
    if (!structTy.declPtr || !structTy.declPtr->generic) {
        return &structTy;
    }
    std::vector<Ptr<Ty>> typeArgs;
    // Build type arguments.
    for (auto& it : structTy.typeArgs) {
        typeArgs.push_back(Generalize(it));
    }
    auto recTy = tyMgr.GetStructTy(*structTy.declPtr, typeArgs);
    return recTy;
}

Ptr<Ty> TyGeneralizer::GetGeneralizedClassTy(ClassTy& classTy)
{
    if (!classTy.declPtr || !classTy.declPtr->generic) {
        return &classTy;
    }
    std::vector<Ptr<Ty>> typeArgs;
    for (auto& it : classTy.typeArgs) {
        typeArgs.push_back(Generalize(it));
    }
    Ptr<ClassTy> insTy = nullptr;
    if (Is<ClassThisTy>(classTy)) {
        insTy = tyMgr.GetClassThisTy(*classTy.declPtr, typeArgs);
    } else {
        insTy = tyMgr.GetClassTy(*classTy.declPtr, typeArgs);
    }
    return insTy;
}

Ptr<Ty> TyGeneralizer::GetGeneralizedInterfaceTy(InterfaceTy& interfaceTy)
{
    if (!interfaceTy.declPtr || !interfaceTy.declPtr->generic) {
        return &interfaceTy;
    }
    std::vector<Ptr<Ty>> typeArgs;
    for (auto& it : interfaceTy.typeArgs) {
        typeArgs.push_back(Generalize(it));
    }
    auto insTy = tyMgr.GetInterfaceTy(*interfaceTy.declPtr, typeArgs);
    return insTy;
}

Ptr<Ty> TyGeneralizer::GetGeneralizedEnumTy(EnumTy& enumTy)
{
    // If is an enum without generic parameter, no need to do instantiation.
    if (!enumTy.declPtr || !enumTy.declPtr->generic) {
        return &enumTy;
    }
    std::vector<Ptr<Ty>> typeArgs;
    // Build type arguments.
    for (auto& it : enumTy.typeArgs) {
        typeArgs.push_back(Generalize(it));
    }
    if (Is<RefEnumTy>(enumTy)) {
        return tyMgr.GetRefEnumTy(*enumTy.declPtr, typeArgs);
    }
    auto tmp = tyMgr.GetEnumTy(*enumTy.declPtr, typeArgs);
    tmp->hasCorrespondRefEnumTy = enumTy.hasCorrespondRefEnumTy;
    return tmp;
}

Ptr<Ty> TyGeneralizer::GetGeneralizedArrayTy(ArrayTy& arrayTy)
{
    if (arrayTy.typeArgs.empty()) {
        return &arrayTy;
    }
    auto elemTy = Generalize(arrayTy.typeArgs[0]);
    auto dims = arrayTy.dims;
    return tyMgr.GetArrayTy(elemTy, dims);
}

Ptr<Ty> TyGeneralizer::GetGeneralizedPointerTy(PointerTy& cptrTy)
{
    if (cptrTy.typeArgs.empty()) {
        return &cptrTy;
    }
    auto elemTy = Generalize(cptrTy.typeArgs[0]);
    return tyMgr.GetPointerTy(elemTy);
}

// Get instantiated ty of set type 'IntersectionTy' and 'UnionTy'.
template <typename SetTy> Ptr<Ty> TyGeneralizer::GetGeneralizedSetTy(SetTy& ty)
{
    std::set<Ptr<Ty>> tys;
    for (auto it : ty.tys) {
        tys.emplace(Generalize(it));
    }
    return tyMgr.GetTypeTy<SetTy>(tys);
}
} // namespace Cangjie
