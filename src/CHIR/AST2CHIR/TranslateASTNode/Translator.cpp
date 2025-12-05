// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

void Translator::SetSymbolTable(const AST::Node& node, Value& val, bool isLocal)
{
    CJC_ASSERT(node.IsDecl() ||
        Utils::In(node.astKind, {AST::ASTKind::BLOCK, AST::ASTKind::TUPLE_PATTERN, AST::ASTKind::ENUM_PATTERN}));
    if (isLocal) {
        localValSymbolTable.Set(node, val);
    } else {
        globalSymbolTable.Set(node, val);
    }
}

void Translator::SetTopLevel(const AST::Decl& decl)
{
    topLevelDecl = &decl;
}
bool Translator::IsTopLevel(const AST::Decl& decl) const
{
    return topLevelDecl == &decl;
}

static bool ShouldInGlobalTable(const AST::Node& node)
{
    // 1. global decl, 'NominalDecl' is stored to indicate 'this' and should be found from local table.
    if (node.TestAttr(AST::Attribute::GLOBAL) && !node.IsNominalDecl()) {
        return true;
    }

    // 2. member method or static member var
    if ((node.TestAttr(AST::Attribute::STATIC) && node.astKind == AST::ASTKind::VAR_DECL) ||
        node.astKind == AST::ASTKind::FUNC_DECL) {
        const auto& decl = StaticCast<const AST::Decl&>(node);
        if (decl.outerDecl != nullptr && decl.outerDecl->IsNominalDecl()) {
            return true;
        }
    }
    return false;
}

Ptr<Value> Translator::GetSymbolTable(const AST::Node& node) const
{
    CJC_ASSERT(node.IsDecl() ||
        Utils::In(node.astKind, {AST::ASTKind::BLOCK, AST::ASTKind::TUPLE_PATTERN, AST::ASTKind::ENUM_PATTERN}));
    // Note this is a temporary solution here, we should be sure that all local consts are in global table
    // once the universal generics are implemented
    if (ShouldInGlobalTable(node)) {
        return globalSymbolTable.Get(node);
    }
    if (node.IsDecl() && localConstVars.HasElement(StaticCast<AST::Decl>(&node))) {
        return globalSymbolTable.Get(node);
    }
    if (auto func = DynamicCast<AST::FuncDecl>(&node); func && localConstFuncs.HasElement(func)) {
        return globalSymbolTable.Get(node);
    }
    return localValSymbolTable.Get(node);
}

Ptr<Type> Translator::TranslateType(AST::Ty& ty)
{
    return chirTy.TranslateType(ty);
}

Ptr<FuncType> Translator::CreateVirtualFuncType(const AST::FuncDecl& decl)
{
    std::vector<Type*> args;
    for (size_t i = 0; i < decl.funcBody->paramLists[0]->params.size(); i++) {
        args.emplace_back(TranslateType(*decl.funcBody->paramLists[0]->params[i]->ty));
    }
    return builder.GetType<FuncType>(args, builder.GetUnitTy());
}

Ptr<CustomTypeDef> Translator::GetNominalSymbolTable(const AST::Node& node)
{
    return chirTy.GetGlobalNominalCache(StaticCast<const AST::Decl&>(node));
}

DebugLocation Translator::GetValueDebugLocationInfo(const Value& value) const
{
    DebugLocation loc;
    if (auto localVar = DynamicCast<const LocalVar*>(&value); localVar) {
        loc = localVar->GetExpr()->GetDebugLocation();
    } else {
        loc = value.GetDebugLocation();
    }
    loc.SetScopeInfo(scopeInfo);
    return loc;
}

DebugLocation Translator::TranslateLocation(const AST::Node& node) const
{
    return TranslateLocation(node.begin, node.end);
}

DebugLocation Translator::TranslateLocation(const Cangjie::Position& begin, const Cangjie::Position& end) const
{
    if (begin.GetStatus() == PositionStatus::IGNORE) {
        return TranslateFileLocation(begin.fileID);
    }
    auto loc = TranslateLocationWithoutScope(builder.GetChirContext(), begin, end);
    loc.SetScopeInfo(scopeInfo);
    return loc;
}

DebugLocation Translator::TranslateFileLocation(unsigned fileID) const
{
    return {builder.GetChirContext().GetSourceFileName(fileID), fileID, {0, 0}, {0, 0}};
}

Ptr<Value> Translator::TypeCastOrBoxIfNeeded(Value& val, Type& expectedTy, const DebugLocation& loc, bool needCheck)
{
    return CHIR::TypeCastOrBoxIfNeeded(val, expectedTy, builder, *currentBlock, loc, needCheck);
}

Ptr<Value> Translator::GetDerefedValue(Ptr<Value> val, const DebugLocation& loc)
{
    CJC_NULLPTR_CHECK(val);
    auto valType = val->GetType();
    if (valType->IsRef()) {
        valType = StaticCast<RefType*>(valType)->GetBaseType();
        // For now, raw class type should not exist. Why, spec indicate that class is reference type.
        if (valType->IsClassOrArray()) {
            return val;
        }
        auto expr = (loc.IsInvalidPos() ? CreateAndAppendExpression<Load>(valType, val, currentBlock)
                                        : CreateAndAppendExpression<Load>(loc, valType, val, currentBlock));
        return expr->GetResult();
    }
    return val;
}

Ptr<Value> Translator::GetImplicitThisParam() const
{
    auto curFunc = GetCurrentFunc();
    CJC_ASSERT(curFunc);
    return curFunc->GetParam(0);
}

void Translator::SetFuncBlockGroup(BlockGroup& group)
{
    blockGroupStack.clear();
    blockGroupStack.emplace_back(&group);
}

void Translator::SetCurrentBlock(Block& block)
{
    currentBlock = &block;
}

Ptr<Block> Translator::GetCurrentBlock() const
{
    return currentBlock;
}

Ptr<Func> Translator::CreateEmptyGVInitFunc(const std::string& mangledName, const std::string& identifier,
    const std::string& rawMangledName, const std::string& pkgName, const Linkage& linkage, const DebugLocation& loc,
    bool isConst)
{
    Func* func = TryGetFromCache<Value, Func>(GLOBAL_VALUE_PREFIX + mangledName, deserializedVals);
    BlockGroup* blockGroup = nullptr;
    if (func) {
        // found deserialized one
        blockGroup = builder.CreateBlockGroup(*func);
        func->ReplaceBody(*blockGroup);
    } else {
        auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetUnitTy());
        func = builder.CreateFunc(INVALID_LOCATION, funcTy, mangledName, identifier, rawMangledName, pkgName);
        blockGroup = builder.CreateBlockGroup(*func);
        func->SetFuncKind(FuncKind::GLOBALVAR_INIT);
        func->EnableAttr(Attribute::NO_REFLECT_INFO);
        func->EnableAttr(Attribute::NO_INLINE);
        func->EnableAttr(Attribute::COMPILER_ADD);
        func->EnableAttr(Attribute::INITIALIZER);
        if (isConst) {
            func->EnableAttr(Attribute::CONST);
        }
        func->InitBody(*blockGroup);
    }
    func->template Set<LinkTypeInfo>(Linkage(linkage));
    func->SetDebugLocation(loc);

    auto entry = builder.CreateBlock(blockGroup);
    blockGroup->SetEntryBlock(entry);
    SetFuncBlockGroup(*blockGroup);
    SetCurrentBlock(*entry);

    auto unitTyRef = builder.GetType<RefType>(builder.GetUnitTy());
    auto retVal = CreateAndAppendExpression<Allocate>(unitTyRef, builder.GetUnitTy(), entry);
    func->SetReturnValue(*retVal->GetResult());

    return func;
}

Ptr<LocalVar> Translator::CreateGetElementRefWithPath(const DebugLocation& loc, Ptr<Value> lhsBase,
    const std::vector<std::string>& path, Ptr<Block> block, const CustomType& customType)
{
    auto [memberType, isReadOnly] = GetInstMemberTypeByNameCheckingReadOnly(customType, path, builder);
    auto memberRefType = builder.GetType<RefType>(memberType);
    auto getMemberRef =
        CreateAndAppendExpression<GetElementByName>(loc, memberRefType, lhsBase, path, block)->GetResult();
    if (lhsBase->TestAttr(Attribute::READONLY) || isReadOnly) {
        getMemberRef->EnableAttr(Attribute::READONLY);
    }
    return getMemberRef;
}

Value* Translator::TranslateExprArg(const AST::Node& node, Type& targetTy, bool deref)
{
    auto value = TranslateASTNode(node, *this);
    if (targetTy.IsUnit()) {
        return CreateAndAppendConstantExpression<UnitLiteral>(builder.GetUnitTy(), *GetCurrentBlock())->GetResult();
    }
    if (node.ty->IsNothing() || targetTy.IsNothing()) {
        return CreateAndAppendConstantExpression<NullLiteral>(builder.GetNothingType(),
            *GetCurrentBlock())->GetResult();
    }
    if (!value) {
        // unit can be converted to Any, or any other interface it extends
        if (node.ty->IsUnit() && !targetTy.IsUnit()) {
            value = CreateAndAppendConstantExpression<UnitLiteral>(
                builder.GetUnitTy(), *GetCurrentBlock())->GetResult();
        } else {
            return CreateAndAppendConstantExpression<NullLiteral>(builder.GetNothingType(),
                *GetCurrentBlock())->GetResult();
        }
    }
    if (value->GetType()->IsNothing()) {
        return value;
    }
    if (deref) {
        value = GetDerefedValue(value, TranslateLocation(node));
    }
    value = TypeCastOrBoxIfNeeded(*value, targetTy, TranslateLocation(node));
    return value;
}

Value* Translator::TranslateExprArg(const AST::Node& node)
{
    auto value = TranslateASTNode(node, *this);
    if (!value) {
        if (node.ty->IsUnit()) {
            return CreateAndAppendConstantExpression<UnitLiteral>(builder.GetUnitTy(), *GetCurrentBlock())->GetResult();
        }
        return CreateAndAppendConstantExpression<NullLiteral>(builder.GetNothingType(),
            *GetCurrentBlock())->GetResult();
    }
    return value;
}

void Translator::TranslateSubExprToLoc(const AST::Node& node, Value* location)
{
    if (!location) {
        TranslateASTNode(node, *this);
        return;
    }
    const DebugLocation* debugLoc;
    if (auto l = DynamicCast<LocalVar>(location)) {
        debugLoc = &l->GetExpr()->GetDebugLocation();
    } else {
        debugLoc = &location->GetDebugLocation();
    }
    TranslateSubExprToLoc(node, location, *debugLoc);
}

void Translator::TranslateSubExprToLoc(const AST::Node& node, Value* location, const DebugLocation& debugLoc)
{
    auto value = TranslateASTNode(node, *this);
    /// \ref loc is null if it is of Unit or Nothing type. In this case, no further typecast/store is needed.
    if (!location) {
        return;
    }
    auto targetTy = StaticCast<RefType>(location->GetType())->GetBaseType();
    if (targetTy->IsNothing()) {
        return;
    }
    // unit can be converted to Any, or any other interface it extends
    if (node.ty->IsUnit() && !targetTy->IsUnit()) {
        value = CreateAndAppendConstantExpression<UnitLiteral>(
            debugLoc, builder.GetUnitTy(), *currentBlock)->GetResult();
    }
    // this if never collides with the previous if, so no need to write else if
    if (targetTy->IsUnit() || !value) {
        if (!opts.enableCompileDebug) {
            return;
        }
        // create a null/unit value if -g
        if (!value) {
            if (node.ty->IsUnit()) {
                auto unit = CreateAndAppendConstantExpression<UnitLiteral>(
                    debugLoc, builder.GetUnitTy(), *currentBlock);
                value = unit->GetResult();
            } else {
                /// either \ref node is of Nothing type, of every branch of \ref node is of Nothing type, create a
                /// Nothing value here
                auto null = CreateAndAppendConstantExpression<NullLiteral>(
                    debugLoc, builder.GetNothingType(), *currentBlock);
                null->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
                value = null->GetResult();
            }
        }
    }
    value = GetDerefedValue(value, debugLoc);
    if (value->GetType()->IsNothing()) {
        return;
    }
    value = TypeCastOrBoxIfNeeded(*value, *targetTy, debugLoc);
    CreateWrappedStore(debugLoc, value, location, GetCurrentBlock());
}

Ptr<Value> Translator::TranslateSubExprAsValue(const AST::Node& node)
{
    auto value = TranslateASTNode(node, *this);
    if (value && Is<AST::Expr>(node)) {
        value = GetDerefedValue(value, TranslateLocation(node));
    }
    return value;
}

void Translator::TranslateSubExprToDiscarded(const AST::Node& node)
{
    TranslateASTNode(node, *this);
    if (auto b = DynamicCast<AST::LitConstExpr>(&node)) {
        if (b->kind == AST::LitConstKind::UNIT) {
            auto unit = CreateAndAppendConstantExpression<UnitLiteral>(
                TranslateLocation(node), builder.GetUnitTy(), *currentBlock);
            if (b->TestAttr(AST::Attribute::COMPILER_ADD)) {
                unit->GetResult()->EnableAttr(Attribute::COMPILER_ADD);
            }
        }
    }
}
