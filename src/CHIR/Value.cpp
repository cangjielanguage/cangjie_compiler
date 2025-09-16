// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Value.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/ToStringUtils.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ICEUtil.h"
#include "cangjie/Utils/Utils.h"

#include <algorithm>

using namespace Cangjie::CHIR;

/// Value
Value::Value(Type* ty, std::string identifier, ValueKind kind)
    : ty(ty), identifier(std::move(identifier)), kind(kind)
{
}

Value::ValueKind Value::GetValueKind() const
{
    return kind;
}

bool Value::IsLocalVar() const
{
    return kind == ValueKind::KIND_LOCALVAR;
}

bool Value::IsFunc() const
{
    return kind == ValueKind::KIND_FUNC || kind == ValueKind::KIND_IMP_FUNC;
}

bool Value::IsBlock() const
{
    return kind == ValueKind::KIND_BLOCK;
}

bool Value::IsBlockGroup() const
{
    return kind == ValueKind::KIND_BLOCK_GROUP;
}

bool Value::IsParameter() const
{
    return kind == ValueKind::KIND_PARAMETER;
}

bool Value::IsLiteral() const
{
    return kind == ValueKind::KIND_LITERAL;
}

bool Value::IsGlobalVar() const
{
    return kind == ValueKind::KIND_GLOBALVAR || kind == ValueKind::KIND_IMP_VAR;
}

bool Value::IsImportedFunc() const
{
    return kind == ValueKind::KIND_IMP_FUNC;
}

bool Value::IsImportedVar() const
{
    return kind == ValueKind::KIND_IMP_VAR;
}

bool Value::IsImportedSymbol() const
{
    return kind == ValueKind::KIND_IMP_FUNC || kind == ValueKind::KIND_IMP_VAR;
}

bool Value::IsGlobalVarInCurPackage() const
{
    return kind == ValueKind::KIND_GLOBALVAR;
}

bool Value::IsFuncWithBody() const
{
    return kind == ValueKind::KIND_FUNC;
}

Type* Value::GetType() const
{
    return ty;
}

/**
 * @brief obtains the identifier of the corresponding Cangjie source code.
 */
std::string Value::GetSrcCodeIdentifier() const
{
    return "";
}

const std::string& Value::GetIdentifier() const
{
    return identifier;
}

std::string Value::GetIdentifierWithoutPrefix() const
{
    if (identifier != "") {
        return identifier.substr(1);
    }
    return identifier;
}

std::vector<Expression*> Value::GetUsers() const
{
    return users;
}

bool Value::IsCompileTimeValue() const
{
    if (kind == ValueKind::KIND_LITERAL) {
        return true;
    }

    if (kind == ValueKind::KIND_LOCALVAR) {
        if (static_cast<const LocalVar*>(this)->GetExpr()->IsConstant()) {
            return true;
        }
    }

    return TestAttr(Attribute::CONST);
}

bool Value::IsGlobal() const
{
    return kind == ValueKind::KIND_GLOBALVAR || kind == ValueKind::KIND_FUNC ||
        kind == ValueKind::KIND_IMP_VAR || kind == ValueKind::KIND_IMP_FUNC;
}

void Value::AddUserOnly(Expression* expr)
{
    CJC_NULLPTR_CHECK(expr);
    if (IsGlobal()) {
        std::unique_lock<std::mutex> lock(userMutex);
        users.push_back(expr);
    } else {
        users.push_back(expr);
    }
}

void Value::RemoveUserOnly(Expression* expr)
{
    if (IsGlobal()) {
        std::unique_lock<std::mutex> lock(userMutex);
        users.erase(std::remove(users.begin(), users.end(), expr), users.end());
    } else {
        users.erase(std::remove(users.begin(), users.end(), expr), users.end());
    }
}

void Value::Dump() const
{
    std::cout << ToString() << std::endl;
}

void Value::ReplaceWith(Value& newValue, const BlockGroup* scope)
{
    std::vector<Expression*> oldUsers{users};
    for (auto user : oldUsers) {
        if (user->GetParentBlock() == nullptr) {
            continue;
        }
        if (scope == nullptr || IsNestedBlockOf(user->GetParentBlock()->GetParentBlockGroup(), scope)) {
            user->ReplaceOperand(this, &newValue);
        }
    }
}

AttributeInfo Value::GetAttributeInfo() const
{
    return attributes;
}

bool Value::TestAttr(Attribute attr) const
{
    return attributes.TestAttr(attr);
}

void Value::AppendAttributeInfo(const AttributeInfo& info)
{
    attributes.AppendAttrs(info);
}

void Value::EnableAttr(Attribute attr)
{
    attributes.SetAttr(attr, true);
}

void Value::DisableAttr(Attribute attr)
{
    attributes.SetAttr(attr, false);
}

const AnnoInfo& Value::GetAnnoInfo() const
{
    return annoInfo;
}

void Value::SetAnnoInfo(AnnoInfo&& info)
{
    annoInfo = std::move(info);
}

void Value::ClearUsersOnly()
{
    users.clear();
}

Parameter::Parameter(Type* ty, const std::string& indexStr, Func* ownerFunc)
    : Value(ty, indexStr, ValueKind::KIND_PARAMETER), ownerFunc(ownerFunc)
{
    if (ownerFunc) {
        ownerFunc->AddParam(*this);
    }
}

Parameter::Parameter(Type* ty, const std::string& indexStr, Lambda& ownerLambda)
    : Value(ty, indexStr, ValueKind::KIND_PARAMETER), ownerLambda(&ownerLambda)
{
    ownerLambda.AddParam(*this);
}

std::string Parameter::GetSrcCodeIdentifier() const
{
    std::string ident;
    auto debugExpr = GetDebugExpr();
    if (debugExpr != nullptr) {
        ident = debugExpr->GetSrcCodeIdentifier();
    }
    return ident;
}

Func* Parameter::GetOwnerFunc() const
{
    return ownerFunc;
}

Func* Parameter::GetTopLevelFunc() const
{
    if (ownerFunc != nullptr) {
        return ownerFunc;
    }
    CJC_NULLPTR_CHECK(ownerLambda);
    return ownerLambda->GetTopLevelFunc();
}

Lambda* Parameter::GetOwnerLambda() const
{
    return ownerLambda;
}

void Parameter::SetOwnerLambda(Lambda* newParent)
{
    ownerLambda = newParent;
    ownerFunc = nullptr;
}

void Parameter::SetOwnerFunc(Func* owner)
{
    ownerFunc = owner;
    ownerLambda = nullptr;
}

Debug* Parameter::GetDebugExpr() const
{
    for (auto it = users.crbegin(); it != users.crend(); ++it) {
        if ((*it)->GetExprKind() == ExprKind::DEBUGEXPR) {
            return StaticCast<Debug*>(*it);
        }
    }
    return nullptr;
}

std::string Parameter::ToString() const
{
    std::stringstream ss;
    ss << identifier;
    ss << ": " << ty->ToString();
    return ss.str();
}

/// LocalVar
LocalVar::LocalVar(Type* ty, std::string indexStr, Expression* expr)
    : Value(ty, std::move(indexStr), ValueKind::KIND_LOCALVAR), expr(expr)
{
    if (expr) {
        expr->result = this;
    }
}

std::string LocalVar::GetSrcCodeIdentifier() const
{
    std::string ident;
    auto debugExpr = GetDebugExpr();
    if (debugExpr != nullptr) {
        ident = debugExpr->GetSrcCodeIdentifier();
    }
    return ident;
}

BlockGroup* LocalVar::GetOwnerBlockGroup() const
{
    CJC_NULLPTR_CHECK(expr);
    return expr->GetParentBlockGroup();
}

Expression* LocalVar::GetExpr() const
{
    return expr;
}

bool LocalVar::IsRetValue() const
{
    return isRetValue;
}

Func* LocalVar::GetTopLevelFunc() const
{
    CJC_NULLPTR_CHECK(expr);
    return expr->GetTopLevelFunc();
}

const DebugLocation& LocalVar::GetDebugLocation() const
{
    return expr->GetDebugLocation();
}

Debug* LocalVar::GetDebugExpr() const
{
    for (auto it = users.crbegin(); it != users.crend(); ++it) {
        if ((*it)->GetExprKind() == ExprKind::DEBUGEXPR) {
            return StaticCast<Debug*>(*it);
        }
    }
    return nullptr;
}

void LocalVar::SetRetValue()
{
    isRetValue = true;
}

std::string LocalVar::ToString() const
{
    std::stringstream ss;
    ss << identifier;
    ss << ": " << ty->ToString();
    if (expr) {
        ss << " = " << expr->ToString();
    }
    return ss.str();
}

// GlobalVar
GlobalVar::GlobalVar(Type* ty, std::string identifier, std::string srcCodeIdentifier, std::string rawMangledName,
    std::string packageName)
    : Value{ty, std::move(identifier), ValueKind::KIND_GLOBALVAR},
    GlobalVarBase{std::move(srcCodeIdentifier), std::move(rawMangledName), std::move(packageName)}
{
}

bool GlobalVar::IsLocalConst() const
{
    // lifted local const var is marked COMPILER_ADD
    // this is more like a hack.
    return TestAttr(Attribute::CONST) && TestAttr(Attribute::COMPILER_ADD);
}

std::string GlobalVar::ToString() const
{
    std::stringstream ss;
    ss << attributes.ToString() << identifier << ": " << ty->ToString();
    if (auto initVal = GetInitializer()) {
        ss << " = " << initVal->ToString();
    }
    std::stringstream comment;
    comment << ToStringAnnotationMap();
    if (annoInfo.IsAvailable()) {
        AddCommaOrNot(comment);
        comment << "annoInfo: " + annoInfo.mangledName;
    }
    if (!srcCodeIdentifier.empty()) {
        AddCommaOrNot(comment);
        comment << "srcCodeIdentifier: " + srcCodeIdentifier;
    }
    if (!rawMangledName.empty()) {
        AddCommaOrNot(comment);
        comment << "rawMangledName: " << rawMangledName;
    }
    if (comment.str() != "") {
        ss << " // " << comment.str();
    }
    return ss.str();
}

GlobalVarBase::GlobalVarBase(std::string srcCodeIdentifier, std::string rawMangledName, std::string packageName)
    : packageName(std::move(packageName)),
    srcCodeIdentifier(std::move(srcCodeIdentifier)),
    rawMangledName(std::move(rawMangledName))
{
}

Func* GlobalVarBase::GetInitFunc() const
{
    return initFunc;
}

std::string GlobalVarBase::GetSrcCodeIdentifier() const
{
    return srcCodeIdentifier;
}

/**
 * @brief only static member var has declaredParent, others return nullptr
 */
CustomTypeDef* GlobalVarBase::GetParentCustomTypeDef() const
{
    return declaredParent;
}

const std::string& GlobalVarBase::GetPackageName() const
{
    return packageName;
}

void GlobalVarBase::SetInitializer(LiteralValue& literalValue)
{
    this->initializer = &literalValue;
    this->initFunc = nullptr;
}

void GlobalVarBase::DestroySelf()
{
    if (declaredParent) {
        Utils::RemoveFromVec(declaredParent->staticVars, this);
    }
}

LiteralValue* GlobalVarBase::GetInitializer() const
{
    return this->initializer;
}

void GlobalVarBase::SetInitFunc(Func& func)
{
    CJC_ASSERT(initializer == nullptr);
    this->initFunc = &func;
}

const std::string& GlobalVarBase::GetRawMangledName() const
{
    return rawMangledName;
}

ImportedValue::ImportedValue()
{
}

std::string ImportedValue::ToString() const
{
    std::stringstream ss;
    ss << identifier;
    ss << ": " << ty->ToString();
    return ss.str();
}

void ImportedValue::DestroySelf()
{
    if (auto var = DynamicCast<ImportedVar*>(this)) {
        StaticCast<GlobalVarBase*>(var)->DestroySelf();
    } else {
        StaticCast<FuncBase*>(StaticCast<ImportedFunc*>(this))->DestroySelf();
    }
}

std::string ImportedFunc::ToString() const
{
    std::stringstream ss;
    ss << identifier;
    ss << GetGenericTypeParamsStr(genericTypeParams);
    ss << ": " << ty->ToString();
    return ss.str();
}

Block::Block(std::string identifier, BlockGroup* parentGroup)
    : Value(nullptr, std::move(identifier), ValueKind::KIND_BLOCK), parentGroup(parentGroup)
{
    if (parentGroup) {
        parentGroup->AddBlock(this);
    }
}

void Block::AppendPredecessorOnly(Block& block)
{
    predecessors.emplace_back(&block);
}

void Block::AddPredecessor(Block* block)
{
    if (Utils::NotIn(block, predecessors)) {
        AppendPredecessorOnly(*block);
        AddUserOnly(block->GetTerminator());
    }
}

void Block::RemoveExprOnly(Expression& expr)
{
    exprs.erase(std::remove(exprs.begin(), exprs.end(), &expr), exprs.end());
}

void Block::AppendExprOnly(Expression& expr)
{
    exprs.emplace_back(&expr);
}

void Block::AppendNonTerminatorExpression(Expression* expression)
{
    CJC_ASSERT(!expression->IsTerminator());
    expression->GetParentBlock()->RemoveExprOnly(*expression);
    expression->SetParent(this);
    AppendExprOnly(*expression);
}

void Block::AppendExpressions(const std::vector<Expression*>& expressions)
{
    for (auto expr : expressions) {
        AppendExpression(expr);
    }
}

void Block::AppendExpression(Expression* expression)
{
    if (expression->IsTerminator()) {
        AppendTerminator(StaticCast<Terminator*>(expression));
    } else {
        AppendNonTerminatorExpression(expression);
    }
}

std::vector<Expression*> Block::GetExpressions() const
{
    return exprs;
}

Expression* Block::GetExpressionByIdx(size_t idx) const
{
    CJC_ASSERT(idx < exprs.size());
    return exprs[idx];
}

std::vector<Expression*> Block::GetNonTerminatorExpressions() const
{
    if (exprs.empty()) {
        return {};
    } else {
        return std::vector<Expression*>{exprs.begin(), exprs.end() - 1};
    }
}

void Block::SetParentBlockGroup(BlockGroup* parent)
{
    this->parentGroup = parent;
}

BlockGroup* Block::GetParentBlockGroup() const
{
    return parentGroup;
}

void Block::AppendTerminator(Terminator* term)
{
    AppendExprOnly(*term);
    term->SetParent(this);
    // update precedessors
    for (auto suc : term->GetSuccessors()) {
        suc->AddPredecessor(this);
    }
}

Func* Block::GetTopLevelFunc() const
{
    auto blockGroup = GetParentBlockGroup();
    CJC_NULLPTR_CHECK(blockGroup);
    return blockGroup->GetTopLevelFunc();
}

Terminator* Block::GetTerminator() const
{
    if (exprs.size() == 0) {
        return nullptr;
    }
    return DynamicCast<Terminator*>(exprs.back());
}

std::vector<Block*> Block::GetSuccessors() const
{
    if (auto term = GetTerminator(); term != nullptr) {
        return term->GetSuccessors();
    }
    return {};
}

std::vector<Block*> Block::GetPredecessors() const
{
    return predecessors;
}

void Block::ClearExprsOnly()
{
    exprs.clear();
}

void Block::ClearPredecessorsOnly()
{
    predecessors.clear();
}

void Block::RemoveSelfFromBlockGroup()
{
    CJC_NULLPTR_CHECK(parentGroup);
    parentGroup->RemoveBlock(*this);
    if (parentGroup->entryBlock == this) {
        parentGroup->SetEntryBlock(nullptr);
    }
    SetParentBlockGroup(nullptr);

    ClearPredecessorsOnly();
    ClearUsersOnly();

    for (auto expr : GetExpressions()) {
        expr->RemoveSelfFromBlock();
    }
    ClearExprsOnly();
}

bool Block::IsEntry() const
{
    if (auto parent = GetParentBlockGroup(); parent != nullptr) {
        return parent->GetEntryBlock() == this;
    }
    return false;
}

void Block::InsertExprIntoHead(Expression& expr)
{
    CJC_ASSERT(!expr.IsTerminator());
    // 1. remove expr from expr's parent block
    if (expr.parent != nullptr) {
        expr.GetParentBlock()->RemoveExprOnly(expr);
    }

    // 2. insert expr to head of current block
    exprs.insert(exprs.begin(), &expr);

    // 3. change expr's parent to current block
    expr.SetParent(this);
}

void Block::SetExceptions(const std::vector<ClassType*>& ep)
{
    exceptions = ep;
}

bool Block::IsLandingPadBlock() const
{
    return exceptions.has_value();
}

std::vector<ClassType*> Block::GetExceptions() const
{
    CJC_ASSERT(exceptions.has_value());
    return exceptions.value();
}

std::string Block::ToString() const
{
    return GetBlockStr(*this);
}

void Block::RemovePredecessorOnly(Block& block)
{
    predecessors.erase(std::remove(predecessors.begin(), predecessors.end(), &block), predecessors.end());
}

void Block::RemovePredecessor(Block& block)
{
    RemovePredecessorOnly(block);
    RemoveUserOnly(block.GetTerminator());
}

void Block::MoveTo(BlockGroup& newBlockGroup)
{
    if (parentGroup != nullptr) {
        parentGroup->RemoveBlock(*this);
        if (parentGroup->entryBlock == this) {
            parentGroup->SetEntryBlock(nullptr);
        }
    }
    newBlockGroup.AddBlock(this);
}

Block* Block::Clone(CHIRBuilder& builder, BlockGroup& newGroup) const
{
    auto newBlock = builder.CreateBlock(&newGroup);
    newBlock->AppendAttributeInfo(GetAttributeInfo());
    if (exceptions.has_value()) {
        newBlock->SetExceptions(exceptions.value());
    }
    for (auto expr : exprs) {
        auto newExpr = expr->Clone(builder, *newBlock);
        newExpr->CopyAnnotationMapFrom(*expr);
    }
    return newBlock;
}

size_t Block::GetExpressionsNum() const
{
    size_t res = 0;
    for (auto expr : exprs) {
        if (expr->GetExprKind() == ExprKind::LAMBDA) {
            res += StaticCast<Lambda*>(expr)->GetBody()->GetExpressionsNum();
        }
    }
    res += exprs.size();
    return res;
}

BlockGroup::BlockGroup(std::string identifier) : Value(nullptr, std::move(identifier), ValueKind::KIND_BLOCK_GROUP)
{
}

void BlockGroup::RemoveBlock(Block& block)
{
    blocks.erase(std::remove(blocks.begin(), blocks.end(), &block), blocks.end());
}

Func* BlockGroup::GetTopLevelFunc() const
{
    if (ownerFunc != nullptr) {
        return ownerFunc;
    }
    CJC_ASSERT(users.size() == 1);
    return users[0]->GetTopLevelFunc();
}

void BlockGroup::SetOwnerFunc(Func* func)
{
    if (ownerFunc) {
        ownerFunc->RemoveBody();
    }
    if (auto lambda = DynamicCast<Lambda*>(ownerExpression)) {
        lambda->RemoveBody();
    }

    if (ownerExpression) {
        RemoveUserOnly(ownerExpression);
        ownerExpression = nullptr;
    }
    ownerFunc = func;
}

std::vector<Block*> BlockGroup::GetBlocks() const
{
    return blocks;
}

Block* BlockGroup::GetBlockByIdx(size_t idx) const
{
    CJC_ASSERT(idx < blocks.size());
    return blocks[idx];
}

Block* BlockGroup::GetEntryBlock() const
{
    return entryBlock;
}

void BlockGroup::SetEntryBlock(Block* block)
{
    entryBlock = block;
}

void BlockGroup::SetOwnerExpression(Expression& expr)
{
#ifndef NDEBUG
    // we can't move func or lambdas' body to other expression, vice versa
    if (expr.IsLambda()) {
        CJC_ASSERT(ownerExpression == nullptr || ownerExpression->IsLambda());
    } else {
        CJC_ASSERT(ownerFunc == nullptr && (ownerExpression == nullptr || !ownerExpression->IsLambda()));
    }
#endif
    if (auto lambda = DynamicCast<Lambda*>(&expr)) {
        if (ownerFunc) {
            ownerFunc->RemoveBody();
            ownerFunc = nullptr;
        } else if (auto lambdaExpr = DynamicCast<Lambda*>(ownerExpression)) {
            lambdaExpr->RemoveBody();
        }
    }
    if (ownerExpression) {
        RemoveUserOnly(ownerExpression);
    }
    ownerExpression = &expr;
    AddUserOnly(&expr);
}

Func* BlockGroup::GetOwnerFunc() const
{
    return ownerFunc;
}

Expression* BlockGroup::GetOwnerExpression() const
{
    return ownerExpression;
}

void BlockGroup::ClearBlocksOnly()
{
    blocks.clear();
}

void BlockGroup::SetOwnedFuncOnly(Func* newFunc)
{
    ownerFunc = newFunc;
}

void BlockGroup::ClearBlockGroup()
{
    for (auto block : GetBlocks()) {
        block->RemoveSelfFromBlockGroup();
    }
    ClearBlocksOnly();
    SetEntryBlock(nullptr);
    SetOwnedFuncOnly(nullptr);
    RemoveUserOnly(ownerExpression);
    ownerExpression = nullptr;
}

void BlockGroup::AddBlock(Block* block)
{
    block->SetParentBlockGroup(this);
    blocks.emplace_back(block);
}

void BlockGroup::AddBlocks(const std::vector<Block*>& newBlocks)
{
    for (auto block : newBlocks) {
        AddBlock(block);
    }
}

std::string BlockGroup::ToString() const
{
    return GetBlockGroupStr(*this);
}

void BlockGroup::CloneBlocks(CHIRBuilder& builder, BlockGroup& parent) const
{
    CJC_ASSERT(parent.GetBlocks().empty());
    CJC_ASSERT(parent.GetEntryBlock() == nullptr);
    std::unordered_map<Block*, Block*> blockMap;
    for (auto block : blocks) {
        Block* newBlock = block->Clone(builder, parent);
        if (block == entryBlock) {
            parent.SetEntryBlock(newBlock);
        }
        blockMap.emplace(block, newBlock);
    }
    for (auto block : parent.GetBlocks()) {
        auto successors = block->GetSuccessors();
        for (size_t i = 0; i < successors.size(); ++i) {
            auto it = blockMap.find(successors[i]);
            if (it != blockMap.end()) {
                auto expr = block->GetTerminator();
                CJC_NULLPTR_CHECK(expr);
                expr->ReplaceSuccessor(i, *it->second);
            }
        }
    }
}

BlockGroup* BlockGroup::Clone(CHIRBuilder& builder, Func& newFunc) const
{
    auto newGroup = builder.CreateBlockGroup(newFunc);
    newGroup->SetOwnerFunc(&newFunc);
    newGroup->AppendAttributeInfo(GetAttributeInfo());

    CloneBlocks(builder, *newGroup);
    return newGroup;
}

BlockGroup* BlockGroup::Clone(CHIRBuilder& builder, Lambda& newLambda) const
{
    auto parentFunc = newLambda.GetTopLevelFunc();
    CJC_NULLPTR_CHECK(parentFunc);
    auto newGroup = builder.CreateBlockGroup(*parentFunc);
    if (newLambda.GetBody() == nullptr) {
        newLambda.InitBody(*newGroup);
    }
    newGroup->AppendAttributeInfo(GetAttributeInfo());

    CloneBlocks(builder, *newGroup);
    return newGroup;
}

size_t BlockGroup::GetExpressionsNum() const
{
    size_t res = 0;
    for (auto block : blocks) {
        res += block->GetExpressionsNum();
    }
    return res;
}

FuncBody::FuncBody()
{
}

BlockGroup* FuncBody::GetBody() const
{
    return body;
}

Parameter* FuncBody::GetParam(size_t index) const
{
    return parameters[index];
}

const std::vector<Parameter*>& FuncBody::GetParams() const
{
    return parameters;
}

void FuncBody::SetReturnValue(LocalVar& ret)
{
    retValue = &ret;
}

/**
 * @brief get a `LocalVar` represent the returned value of this FuncBody.
 */
LocalVar* FuncBody::GetReturnValue() const
{
    return retValue;
}

void FuncBody::RemoveBody()
{
    body = nullptr;
}

void FuncBody::RemoveParams()
{
    parameters.clear();
}

void FuncBody::AddParam(Parameter& param)
{
    parameters.emplace_back(&param);
}

FuncBase::FuncBase(const std::string& srcCodeIdentifier, const std::string& rawMangledName,
    const std::string& packageName, const std::vector<GenericType*>& genericTypeParams)
    : srcCodeIdentifier(srcCodeIdentifier),
    rawMangledName(rawMangledName),
    packageName(packageName),
    genericTypeParams(genericTypeParams)
{
}

FuncKind FuncBase::GetFuncKind() const
{
    return funcKind;
}

void FuncBase::SetFuncKind(FuncKind kind)
{
    funcKind = kind;
}

const std::string& FuncBase::GetPackageName() const
{
    return packageName;
}

FuncType* FuncBase::GetFuncType() const
{
    return StaticCast<FuncType*>(GetType());
}

size_t FuncBase::GetNumOfParams() const
{
    return GetFuncType()->GetNumOfParams();
}

Type* FuncBase::GetReturnType() const
{
    return GetFuncType()->GetReturnType();
}

void FuncBase::DestroySelf()
{
    if (declaredParent) {
        Utils::RemoveFromVec(declaredParent->methods, this);
        declaredParent = nullptr;
    }
}

Type* FuncBase::GetParentCustomTypeOrExtendedType() const
{
    if (declaredParent == nullptr) {
        return nullptr;
    }
    if (auto exDef = DynamicCast<ExtendDef*>(declaredParent)) {
        return exDef->GetExtendedType();
    }
    return declaredParent->GetType();
}

CustomTypeDef* FuncBase::GetOuterDeclaredOrExtendedDef() const
{
    if (declaredParent == nullptr) {
        return nullptr;
    }
    if (auto extendDef = DynamicCast<ExtendDef*>(declaredParent); extendDef) {
        auto extendedType = extendDef->GetExtendedType();
        if (extendedType == nullptr) {
            return nullptr;
        }
        if (auto customTy = DynamicCast<const CustomType*>(extendedType); customTy) {
            return customTy->GetCustomTypeDef();
        }
        return nullptr;
    }
    return declaredParent;
}

bool FuncBase::IsMemberFunc() const
{
    return declaredParent != nullptr;
}

CustomTypeDef* FuncBase::GetParentCustomTypeDef() const
{
    return declaredParent;
}

std::string FuncBase::GetSrcCodeIdentifier() const
{
    return srcCodeIdentifier;
}

const std::string& FuncBase::GetRawMangledName() const
{
    return rawMangledName;
}

void FuncBase::SetRawMangledName(const std::string& name)
{
    rawMangledName = name;
}

bool FuncBase::IsConstructor() const
{
    return (funcKind == FuncKind::CLASS_CONSTRUCTOR || funcKind == FuncKind::STRUCT_CONSTRUCTOR ||
        funcKind == FuncKind::PRIMAL_CLASS_CONSTRUCTOR || funcKind == FuncKind::PRIMAL_STRUCT_CONSTRUCTOR) &&
        !TestAttr(Attribute::STATIC);
}

bool FuncBase::IsFinalizer() const
{
    return funcKind == FuncKind::FINALIZER;
}

bool FuncBase::IsLambda() const
{
    return funcKind == FuncKind::LAMBDA;
}

bool FuncBase::IsGVInit() const
{
    return funcKind == FuncKind::GLOBALVAR_INIT;
}

bool FuncBase::IsPrimalConstructor() const
{
    return funcKind == FuncKind::PRIMAL_CLASS_CONSTRUCTOR || funcKind == FuncKind::PRIMAL_STRUCT_CONSTRUCTOR;
}

bool FuncBase::IsCFunc() const
{
    return ty && ty->IsCFunc();
}

bool FuncBase::IsVirtualFunc() const
{
    return TestAttr(Attribute::VIRTUAL) || TestAttr(Attribute::FINAL);
}

FuncBase* FuncBase::GetGenericDecl() const
{
    return genericDecl;
}

void FuncBase::SetGenericDecl(FuncBase& decl)
{
    genericDecl = &decl;
}

bool FuncBase::IsFastNative() const
{
    return isFastNative;
}

void FuncBase::SetFastNative(bool fastNative)
{
    isFastNative = fastNative;
}

void FuncBase::SetCFFIWrapper(bool isWrapper)
{
    isCFFIWrapper = isWrapper;
}

bool FuncBase::IsCFFIWrapper() const
{
    return isCFFIWrapper;
}

const std::vector<GenericType*>& FuncBase::GetGenericTypeParams() const
{
    return genericTypeParams;
}

void FuncBase::SetParamDftValHostFunc(FuncBase& hostFunc)
{
    paramDftValHostFunc = &hostFunc;
}

FuncBase* FuncBase::GetParamDftValHostFunc() const
{
    return paramDftValHostFunc;
}

// Func
bool FuncBase::IsClassMethod() const
{
    if (auto outerDef = GetOuterDeclaredOrExtendedDef()) {
        return outerDef->IsClass();
    }
    return false;
}

bool FuncBase::IsStructMethod() const
{
    if (auto outerDef = GetOuterDeclaredOrExtendedDef()) {
        return outerDef->IsStruct();
    }
    return false;
}

bool FuncBase::IsEnumMethod() const
{
    if (auto outerDef = GetOuterDeclaredOrExtendedDef()) {
        return outerDef->IsEnum();
    }
    return false;
}

bool FuncBase::IsInExtend() const
{
    if (declaredParent == nullptr) {
        return false;
    }
    return declaredParent->GetCustomKind() == CustomDefKind::TYPE_EXTEND;
}

bool FuncBase::IsInGenericContext() const
{
    if (TestAttr(Attribute::GENERIC)) {
        return true;
    }
    return declaredParent != nullptr && declaredParent->TestAttr(Attribute::GENERIC);
}

void FuncBase::SetOriginalLambdaInfo(const FuncSigInfo& info)
{
    CJC_ASSERT(funcKind == FuncKind::LAMBDA);
    originalLambdaInfo = info;
}

FuncType* FuncBase::GetOriginalLambdaType() const
{
    return funcKind == LAMBDA ? originalLambdaInfo.funcType : GetFuncType();
}

std::vector<GenericType*> FuncBase::GetOriginalGenericTypeParams() const
{
    return funcKind == LAMBDA ? originalLambdaInfo.genericTypeParams : GetGenericTypeParams();
}

Func::Func(Type* ty, const std::string& identifier, const std::string& srcCodeIdentifier,
    const std::string& rawMangledName, const std::string& packageName,
    const std::vector<GenericType*>& genericTypeParams)
    : Value(ty, identifier, ValueKind::KIND_FUNC),
      FuncBase{srcCodeIdentifier, rawMangledName, packageName, genericTypeParams}
{
}

uint64_t Func::GenerateLocalId()
{
    return localId++;
}

void Func::DestroyFuncBody()
{
    for (auto b : body.GetBody()->GetBlocks()) {
        for (auto e : b->GetExpressions()) {
            e->RemoveSelfFromBlock();
        }
    }
    RemoveBody();
}

BlockGroup* Func::GetBody() const
{
    return body.GetBody();
}

void Func::RemoveBody()
{
    body.RemoveBody();
}

void Func::RemoveParams()
{
    body.RemoveParams();
}

void Func::InitBody(BlockGroup& newBody)
{
    CJC_ASSERT(body.body == nullptr);
    body.body = &newBody;
    if (newBody.GetOwnerFunc() != this) {
        newBody.SetOwnerFunc(this);
    }
}

void Func::InheritIDFromFunc(const Func& func)
{
    blockId = func.blockId;
    localId = func.localId;
    blockGroupId = func.blockGroupId;
}

void Func::AddParam(Parameter& param)
{
    body.AddParam(param);
    param.SetOwnerFunc(this);
}

Parameter* Func::GetParam(size_t index) const
{
    return body.GetParam(index);
}

const std::vector<Parameter*>& Func::GetParams() const
{
    return body.GetParams();
}

bool Func::HasReturnValue() const
{
    return body.GetReturnValue() != nullptr;
}

void Func::SetReturnValue(LocalVar& ret)
{
    ret.SetRetValue();
    body.SetReturnValue(ret);
}

LocalVar* Func::GetReturnValue() const
{
    return body.GetReturnValue();
}

void Func::SetParentRawMangledName(const std::string& name)
{
    parentName = name;
}

const std::string& Func::GetParentRawMangledName() const
{
    return parentName;
}

uint64_t Func::GenerateBlockId()
{
    return blockId++;
}

uint64_t Func::GenerateBlockGroupId()
{
    return blockGroupId++;
}

Block* Func::GetEntryBlock() const
{
    return GetBody()->GetEntryBlock();
}

std::string Func::ToString() const
{
    return GetFuncStr(*this);
}

const DebugLocation& Func::GetPropLocation() const
{
    return propLoc;
}

void Func::SetPropLocation(const DebugLocation& loc)
{
    propLoc = loc;
}

size_t Func::GetExpressionsNum() const
{
    return GetBody()->GetExpressionsNum();
}

void Func::DestroySelf()
{
    DestroyFuncBody();
    FuncBase::DestroySelf();
}

ImportedFunc::ImportedFunc(
    Type* ty, const std::string& identifier, const std::string& srcCodeIdentifier, const std::string& rawMangledName,
    const std::string& packageName, const std::vector<GenericType*>& genericTypeParams)
    : Value{ty, identifier, KIND_IMP_FUNC},
    ImportedValue{},
    FuncBase{srcCodeIdentifier, rawMangledName, packageName, genericTypeParams}
{
}

const std::vector<AbstractMethodParam>& ImportedFunc::GetParamInfo() const
{
    return paramInfo;
}

void ImportedFunc::SetParamInfo(std::vector<AbstractMethodParam>&& params)
{
    paramInfo = std::move(params);
}

const std::string& ImportedFunc::GetSourcePackageName() const
{
    return packageName;
}

ImportedVar::ImportedVar(Type* ty, std::string identifier, std::string srcCodeIdentifier, std::string rawMangledName,
    std::string packageName)
    : Value{ty, std::move(identifier), ValueKind::KIND_IMP_VAR}, ImportedValue{},
    GlobalVarBase{std::move(srcCodeIdentifier), std::move(rawMangledName), std::move(packageName)}
{
}

const std::string& ImportedVar::GetSourcePackageName() const
{
    return packageName;
}

namespace Cangjie::CHIR {

std::string AbstractMethodParam::ToString()
{
    return paramName + ": " + type->ToString();
}
} // namespace Cangjie::CHIR
