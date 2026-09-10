// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/Utils/ToStringUtils.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/Utils.h"
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
    return kind == ValueKind::KIND_FUNC;
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
    return kind == ValueKind::KIND_GLOBALVAR;
}

bool Value::IsImportedFunc() const
{
    return kind == ValueKind::KIND_FUNC && TestAttr(Attribute::IMPORTED);
}

bool Value::IsImportedVar() const
{
    return kind == ValueKind::KIND_GLOBALVAR && TestAttr(Attribute::IMPORTED);
}

bool Value::IsImportedSymbol() const
{
    return IsImportedFunc() || IsImportedVar();
}

bool Value::IsGlobalVarWithInitializer() const
{
    return kind == ValueKind::KIND_GLOBALVAR && StaticCast<const GlobalVar*>(this)->GetInitializerValue() != nullptr;
}

bool Value::IsFuncWithBody() const
{
    return kind == ValueKind::KIND_FUNC && StaticCast<const Function*>(this)->GetBody() != nullptr;
}

Type* Value::GetType() const
{
    return ty;
}

void Value::SetType(Type& newType)
{
    ty = &newType;
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
        if (Is<Constant>(static_cast<const LocalVar*>(this)->GetExpr())) {
            return true;
        }
    }

    return TestAttr(Attribute::CONST);
}

bool Value::IsGlobal() const
{
    return kind == ValueKind::KIND_GLOBALVAR || kind == ValueKind::KIND_FUNC;
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
    std::cout << ToString(0) << std::endl;
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

void Value::ClearUsersOnly()
{
    users.clear();
}

BlockGroup* Value::GetFuncOrLambdaBody() const
{
    if (IsParameter()) {
        auto param = StaticCast<const Parameter*>(this);
        if (auto func = param->GetOwnerFunc()) {
            return func->GetBody();
        } else if (auto lambda = param->GetOwnerLambda()) {
            return lambda->GetBody();
        } else {
            CJC_ABORT();
        }
    } else if (IsLocalVar()) {
        return StaticCast<const LocalVar*>(this)->GetExpr()->GetParentBlock()->GetFuncOrLambdaBody();
    } else if (IsBlock()) {
        return StaticCast<const Block*>(this)->GetParentBlockGroup()->GetFuncOrLambdaBody();
    } else if (IsBlockGroup()) {
        auto bg = StaticCast<const BlockGroup*>(this);
        if (auto func = bg->GetOwnerFunc()) {
            return func->GetBody();
        }
        CJC_ASSERT(users.size() == 1);
        return users[0]->GetFuncOrLambdaBody();
    } else if (IsFunc()) {
        return StaticCast<const Function*>(this)->GetBody();
    }
    return nullptr;
}

Parameter::Parameter(Type* ty, const std::string& id, Function* ownerFunc)
    : Value(ty, id, ValueKind::KIND_PARAMETER), ownerFunc(ownerFunc)
{
    if (ownerFunc && !ownerFunc->TestAttr(Attribute::PREVIOUSLY_DESERIALIZED)) {
        ownerFunc->AddParam(*this);
    }
}

Parameter::Parameter(Type* ty, const std::string& id, Lambda& ownerLambda)
    : Value(ty, id, ValueKind::KIND_PARAMETER), ownerLambda(&ownerLambda)
{
    ownerLambda.AddParam(*this);
}

std::string Parameter::GetSrcCodeIdentifier() const
{
    return srcCodeIdentifier;
}

void Parameter::SetSrcCodeIdentifier(const std::string& newName)
{
    srcCodeIdentifier = newName;
}

Function* Parameter::GetOwnerFunc() const
{
    return ownerFunc;
}

Function* Parameter::GetTopLevelFunc() const
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

void Parameter::SetOwnerFunc(Function* owner)
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

const AnnoInfo& Parameter::GetAnnoInfo() const
{
    return annoInfo;
}

void Parameter::SetAnnoInfo(AnnoInfo&& info)
{
    annoInfo = std::move(info);
}

std::string Parameter::ToString(size_t indent) const
{
    /** @CustomAnnotation
     *  [attr1] [attr2] [...] %x[name]: type // comment
     */
    std::stringstream ss;
    ss << AddNewLineOrNot(annoInfo.ToString(indent));
    ss << IndentToString(indent) << attributes.ToString();
    ss << identifier;
    if (!srcCodeIdentifier.empty()) {
        ss << "[" << srcCodeIdentifier << "]";
    }
    ss << ": " << ty->ToString();
    ss << CommentToString(BaseCommentToString());
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
    return srcCodeIdentifier;
}

void LocalVar::SetSrcCodeIdentifier(const std::string& newName)
{
    srcCodeIdentifier = newName;
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

Function* LocalVar::GetTopLevelFunc() const
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

void LocalVar::SetRetValue(bool flag)
{
    this->isRetValue = flag;
}

std::string LocalVar::ToString(size_t indent) const
{
    CJC_NULLPTR_CHECK(expr);
    return expr->ToString(indent);
}

// GlobalVar
GlobalVar::GlobalVar(Type* ty, const std::string& identifier, const std::string& srcCodeIdentifier,
    const std::string& rawMangledName, const std::string& packageName)
    : GlobalValue(ValueKind::KIND_GLOBALVAR, ty, identifier, srcCodeIdentifier, rawMangledName, packageName)
{
}

bool GlobalVar::IsSrcCodeImported() const
{
    return TestAttr(Attribute::IMPORTED) && initializer != nullptr;
}

bool GlobalVar::IsLocalConst() const
{
    // lifted local const var is marked COMPILER_ADD
    // this is more like a hack.
    return TestAttr(Attribute::CONST) && TestAttr(Attribute::COMPILER_ADD);
}

void GlobalVar::DestroySelf()
{
    if (declaredParent) {
        Utils::RemoveFromVec(declaredParent->staticVars, this);
    }
}

void GlobalVar::DestroyInitializer()
{
    if (auto initFunc = GetInitFunc()) {
        for (auto user : initFunc->GetUsers()) {
            user->RemoveSelfFromBlock();
        }
        initFunc->DestroySelf();
    }
    initializer = nullptr;
}

std::string GlobalVar::ToString(size_t indent) const
{
    /** @CustomAnnotation
     *  [attr1] [attr2] [...] @mangledName: type = literal init value(or init function)
     */
    std::stringstream ss;
    ss << AddNewLineOrNot(annoInfo.ToString(indent));
    ss << IndentToString(indent) << attributes.ToString() << identifier << ": " << ty->ToString();
    if (auto initVal = GetInitializer()) {
        ss << " = " << initVal->ToString(0);
    } else if (auto initFunc = GetInitFunc()) {
        ss << " = " << initFunc->GetIdentifier() << "()";
    }
    ss << CommentToString(GlobalValueCommentToString());
    return ss.str();
}

Value* GlobalVar::GetInitializerValue() const
{
    return initializer;
}

Function* GlobalVar::GetInitFunc() const
{
    return DynamicCast<Function*>(initializer);
}

void GlobalVar::SetInitializer(LiteralValue& literalValue)
{
    this->initializer = &literalValue;
}

LiteralValue* GlobalVar::GetInitializer() const
{
    return DynamicCast<LiteralValue*>(initializer);
}

void GlobalVar::SetInitFunc(Function& func)
{
    this->initializer = &func;
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
    CJC_ASSERT(expr.GetParentBlockGroup() == GetParentBlockGroup());
    exprs.emplace_back(&expr);
}

void Block::AppendExpressions(const std::vector<Expression*>& expressions)
{
    for (auto expr : expressions) {
        AppendExpression(expr);
    }
}

void Block::AppendExpression(Expression* expression)
{
    CJC_ASSERT(expression->GetParentBlockGroup() == GetParentBlockGroup());
    expression->GetParentBlock()->RemoveExprOnly(*expression);
    expression->SetParent(this);
    AppendExprOnly(*expression);
    // update predecessors if this expression has successor blocks
    for (auto suc : expression->GetSuccessors()) {
        suc->AddPredecessor(this);
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

Function* Block::GetTopLevelFunc() const
{
    auto blockGroup = GetParentBlockGroup();
    CJC_NULLPTR_CHECK(blockGroup);
    return blockGroup->GetTopLevelFunc();
}

BlockGroup* Block::GetFuncOrLambdaBody() const
{
    auto blockGroup = GetParentBlockGroup();
    CJC_NULLPTR_CHECK(blockGroup);
    return blockGroup->GetFuncOrLambdaBody();
}

Expression* Block::GetTerminator() const
{
    if (exprs.size() == 0) {
        return nullptr;
    }
    auto lastExpr = exprs.back();
    if (lastExpr->IsTerminator()) {
        return lastExpr;
    }
    return nullptr;
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
    CJC_ASSERT(expr.GetParentBlockGroup() == GetParentBlockGroup());
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

std::string Block::ToString(size_t indent) const
{
    std::stringstream ss;
    ss << IndentToString(indent) << attributes.ToString();
    ss << "Block " << identifier << ": ";
    std::vector<std::string> comments;
    if (!predecessors.empty()) {
        comments.emplace_back("predecessors: " + ValueIdVecToString("[", predecessors, "]"));
    }
    if (auto baseComment = BaseCommentToString(); !baseComment.empty()) {
        comments.emplace_back(baseComment);
    }
    if (IsLandingPadBlock()) {
        auto exceptionStr = TypeVecToString("[", GetExceptions(), "]");
        if (exceptionStr.empty()) {
            comments.emplace_back("exceptions: [All]");
        } else {
            comments.emplace_back(exceptionStr);
        }
    }
    ss << CommentToString(comments);
    for (auto expr : exprs) {
        ss << std::endl << expr->ToString(indent + 1);
    }
    return ss.str();
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
        newExpr->CopyBaseInfoFrom(*expr);
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

Function* BlockGroup::GetTopLevelFunc() const
{
    if (ownerFunc != nullptr) {
        return ownerFunc;
    }
    CJC_ASSERT(users.size() == 1);
    return users[0]->GetTopLevelFunc();
}

BlockGroup* BlockGroup::GetFuncOrLambdaBody() const
{
    if (ownerFunc != nullptr) {
        return ownerFunc->GetBody();
    }
    CJC_ASSERT(users.size() == 1);
    return users[0]->GetFuncOrLambdaBody();
}

void BlockGroup::SetOwnerFunc(Function* func)
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

std::vector<Block*> BlockGroup::GetAllBlocks() const
{
    auto res = blocks;
    for (auto block : blocks) {
        for (auto e : block->GetExpressions()) {
            if (e->GetExprKind() == ExprKind::LAMBDA) {
                auto lambdaBlocks = StaticCast<Lambda*>(e)->GetBody()->GetAllBlocks();
                res.insert(res.end(), lambdaBlocks.begin(), lambdaBlocks.end());
            }
        }
    }
    return res;
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
    if (ownerFunc) {
        ownerFunc->RemoveBody();
        ownerFunc = nullptr;
    }
    if (ownerExpression) {
        RemoveUserOnly(ownerExpression);
        auto& subGroups = ownerExpression->blockGroups;
        subGroups.erase(
            std::remove(subGroups.begin(), subGroups.end(), this), subGroups.end());
        ownerExpression = nullptr;
    }
    ownerExpression = &expr;
    AddUserOnly(&expr);
}

Function* BlockGroup::GetOwnerFunc() const
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

void BlockGroup::SetOwnedFuncOnly(Function* newFunc)
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

std::string BlockGroup::ToString(size_t indent) const
{
    std::stringstream ss;
    ss << IndentToString(indent) << "{ Block Group: " << identifier;
    ss << CommentToString(BaseCommentToString());
    ss << std::endl;
    if (!blocks.empty()) {
        auto cmp = [](const Ptr<const Block> b1, const Ptr<const Block> b2) {
            return b1->GetIdentifier() < b2->GetIdentifier();
        };
        auto blockSet = Utils::VecToSortedSet<decltype(cmp)>(blocks, cmp);
        CJC_NULLPTR_CHECK(entryBlock);
        auto sortedBlock = TopologicalSort(entryBlock);
        for (auto block : sortedBlock) {
            ss << block->ToString(indent) << std::endl;
            blockSet.erase(block);
        }

        // print orphan block
        for (auto block : blockSet) {
            ss << block->ToString(indent) << std::endl;
        }
    }
    ss << IndentToString(indent) << "}";
    return ss.str();
}

void BlockGroup::CloneBlocks(CHIRBuilder& builder, BlockGroup& parent) const
{
    CJC_ASSERT(parent.GetBlocks().empty());
    CJC_ASSERT(parent.GetEntryBlock() == nullptr);
    for (auto block : blocks) {
        Block* newBlock = block->Clone(builder, parent);
        if (block == entryBlock) {
            parent.SetEntryBlock(newBlock);
        }
    }
}

BlockGroup* BlockGroup::Clone(CHIRBuilder& builder, Function& newFunc) const
{
    auto newGroup = builder.CreateBlockGroup(newFunc);
    newGroup->SetOwnerFunc(&newFunc);
    newGroup->AppendAttributeInfo(GetAttributeInfo());

    CloneBlocks(builder, *newGroup);
    return newGroup;
}

BlockGroup* BlockGroup::Clone(CHIRBuilder& builder, Expression& newExpression) const
{
    auto newGroup = builder.CreateBlockGroup(*newExpression.GetTopLevelFunc());
    newGroup->AppendAttributeInfo(GetAttributeInfo());
    if (auto lambda = DynamicCast<Lambda*>(&newExpression)) {
        lambda->InitBody(*newGroup);
    } else if (auto exclave = DynamicCast<Exclave*>(&newExpression)) {
        exclave->InitBody(*newGroup);
    } else {
        CJC_ABORT();
    }
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

Function::Function(Type* ty, const std::string& identifier, const std::string& srcCodeIdentifier,
    const std::string& rawMangledName, const std::string& packageName,
    const std::vector<GenericType*>& genericTypeParams)
    : GlobalValue(ValueKind::KIND_FUNC, ty, identifier, srcCodeIdentifier, rawMangledName, packageName),
    genericTypeParams(genericTypeParams)
{
}

FuncKind Function::GetFuncKind() const
{
    return funcKind;
}

void Function::SetFuncKind(FuncKind kind)
{
    funcKind = kind;
}

FuncType* Function::GetFuncType() const
{
    return StaticCast<FuncType*>(GetType());
}

/**
 * @brief Replace the return value of this function and update both the function type and body.
 *
 * This method performs a complete replacement of the function's return value:
 * 1. Updates the function type's return type (via base class implementation).
 * 2. Clears the old return value's ret flag if it exists.
 * 3. Updates the function body's return value:
 *    - If `newRet` is nullptr, clears the return value (function returns Void).
 *    - Otherwise, sets `newRet` as the new return value.
 *
 * This is typically used during optimization passes, such as when converting
 * Unit return types to Void (see OptFuncRetType::Unit2Void).
 *
 * @param newRet The new return value LocalVar. If nullptr, the function will return Void
 *               and the body's return value will be cleared. Must be a RefType if not nullptr.
 * @param builder The CHIRBuilder used to create or get the updated function type.
 */
void Function::ReplaceReturnValue(LocalVar* newRet, CHIRBuilder& builder)
{
    auto curFuncType = GetFuncType();
    if (newRet == nullptr) {
        // Change return type to Void
        ty = builder.GetType<FuncType>(curFuncType->GetParamTypes(), builder.GetVoidTy());
    } else {
        // Extract base type from RefType and set it as the new return type
        CJC_ASSERT(newRet->GetType()->IsRef());
        auto retType = StaticCast<RefType*>(newRet->GetType())->GetBaseType();
        ty = builder.GetType<FuncType>(curFuncType->GetParamTypes(), retType);
    }
    // Clear the old return value's ret flag if it exists
    if (retValue != nullptr) {
        retValue->SetRetValue(false);
    }

    retValue = newRet;
    if (retValue != nullptr) {
        CJC_NULLPTR_CHECK(body);
        retValue->SetRetValue(true);
    }
}

size_t Function::GetNumOfParams() const
{
    return parameters.size();
}

Type* Function::GetReturnType() const
{
    return GetFuncType()->GetReturnType();
}

void Function::DestroySelf()
{
    if (declaredParent) {
        Utils::RemoveFromVec(declaredParent->methods, this);
        declaredParent = nullptr;
    }
    DestroyFuncBody();
}

Type* Function::GetParentCustomTypeOrExtendedType() const
{
    if (declaredParent == nullptr) {
        return nullptr;
    }
    return declaredParent->GetType();
}

CustomTypeDef* Function::GetOuterDeclaredOrExtendedDef() const
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

bool Function::IsMemberFunc() const
{
    return declaredParent != nullptr;
}

bool Function::IsConstructor() const
{
    return (funcKind == FuncKind::CLASS_CONSTRUCTOR || funcKind == FuncKind::STRUCT_CONSTRUCTOR ||
        funcKind == FuncKind::PRIMAL_CLASS_CONSTRUCTOR || funcKind == FuncKind::PRIMAL_STRUCT_CONSTRUCTOR) &&
        !TestAttr(Attribute::STATIC);
}

bool Function::IsFinalizer() const
{
    return funcKind == FuncKind::FINALIZER;
}

bool Function::IsLambda() const
{
    return funcKind == FuncKind::LAMBDA;
}

bool Function::IsGVInit() const
{
    return funcKind == FuncKind::GLOBALVAR_INIT;
}

bool Function::IsStaticInit() const
{
    return (funcKind == FuncKind::CLASS_CONSTRUCTOR || funcKind == FuncKind::STRUCT_CONSTRUCTOR) &&
        srcCodeIdentifier == "static.init";
}

bool Function::IsInstanceVarInit() const
{
    return funcKind == FuncKind::INSTANCEVAR_INIT;
}

bool Function::IsCFunc() const
{
    return ty && ty->IsCFunc();
}

bool Function::IsVirtualFunc() const
{
    return TestAttr(Attribute::VIRTUAL) || TestAttr(Attribute::FINAL);
}

bool Function::IsPureAbstract() const
{
    return TestAttr(Attribute::ABSTRACT) && body == nullptr;
}

bool Function::IsSrcCodeImported() const
{
    return TestAttr(Attribute::IMPORTED) && body != nullptr;
}

Function* Function::GetGenericDecl() const
{
    return genericDecl;
}

void Function::SetGenericDecl(Function& decl)
{
    genericDecl = &decl;
}

bool Function::IsFastNative() const
{
    return isFastNative;
}

void Function::SetFastNative(bool fastNative)
{
    isFastNative = fastNative;
}

void Function::SetCFFIWrapper(bool isWrapper)
{
    isCFFIWrapper = isWrapper;
}

bool Function::IsCFFIWrapper() const
{
    return isCFFIWrapper;
}

const std::vector<GenericType*>& Function::GetGenericTypeParams() const
{
    return genericTypeParams;
}

void Function::SetParamDftValHostFunc(Function& hostFunc)
{
    paramDftValHostFunc = &hostFunc;
}

Function* Function::GetParamDftValHostFunc() const
{
    return paramDftValHostFunc;
}

void Function::ClearParamDftValHostFunc()
{
    paramDftValHostFunc = nullptr;
}

// Function
bool Function::IsClassMethod() const
{
    if (auto outerDef = GetOuterDeclaredOrExtendedDef()) {
        return outerDef->IsClass();
    }
    return false;
}

bool Function::IsStructMethod() const
{
    if (auto outerDef = GetOuterDeclaredOrExtendedDef()) {
        return outerDef->IsStruct();
    }
    return false;
}

bool Function::IsEnumMethod() const
{
    if (auto outerDef = GetOuterDeclaredOrExtendedDef()) {
        return outerDef->IsEnum();
    }
    return false;
}

bool Function::IsInExtend() const
{
    if (declaredParent == nullptr) {
        return false;
    }
    return declaredParent->GetCustomKind() == CustomDefKind::TYPE_EXTEND;
}

bool Function::IsInGenericContext() const
{
    if (TestAttr(Attribute::GENERIC)) {
        return true;
    }
    return declaredParent != nullptr && declaredParent->TestAttr(Attribute::GENERIC);
}

void Function::SetOriginalLambdaInfo(const FuncSigInfo& info)
{
    CJC_ASSERT(funcKind == FuncKind::LAMBDA);
    originalLambdaInfo = info;
}

FuncType* Function::GetOriginalLambdaType() const
{
    return funcKind == LAMBDA ? originalLambdaInfo.funcType : GetFuncType();
}

std::vector<GenericType*> Function::GetOriginalGenericTypeParams() const
{
    return funcKind == LAMBDA ? originalLambdaInfo.genericTypeParams : GetGenericTypeParams();
}

uint64_t Function::GenerateLocalId()
{
    return localId++;
}

void Function::SetLocalId(uint64_t id)
{
    localId = id;
}

void Function::SetBlockId(uint64_t id)
{
    blockId = id;
}

void Function::SetBlockGroupId(uint64_t id)
{
    blockGroupId = id;
}

void Function::DestroyFuncBody()
{
    retValue = nullptr;
    if (body == nullptr) {
        return;
    }
    for (auto b : body->GetBlocks()) {
        for (auto e : b->GetExpressions()) {
            e->RemoveSelfFromBlock();
        }
    }
    body = nullptr;
}

BlockGroup* Function::GetBody() const
{
    return body;
}

void Function::RemoveBody()
{
    body = nullptr;
}

void Function::RemoveParams()
{
    parameters.clear();
}

void Function::InitBody(BlockGroup& newBody)
{
    CJC_ASSERT(body == nullptr);
    body = &newBody;
    if (newBody.GetOwnerFunc() != this) {
        newBody.SetOwnerFunc(this);
    }
}

void Function::ReplaceBody(BlockGroup& newBody)
{
    DestroyFuncBody();
    InitBody(newBody);
}

void Function::InheritIDFromFunc(const Function& func)
{
    blockId = func.blockId;
    localId = func.localId;
    blockGroupId = func.blockGroupId;
}

void Function::AddParam(Parameter& param)
{
    parameters.emplace_back(&param);
    param.SetOwnerFunc(this);
}

Parameter* Function::GetParam(size_t index) const
{
    CJC_ASSERT(index < parameters.size());
    return parameters[index];
}

const std::vector<Parameter*>& Function::GetParams() const
{
    return parameters;
}

bool Function::HasReturnValue() const
{
    return retValue != nullptr;
}

void Function::SetReturnValue(LocalVar& ret)
{
    CJC_NULLPTR_CHECK(body);
    ret.SetRetValue(true);
    retValue = &ret;
}

LocalVar* Function::GetReturnValue() const
{
    return retValue;
}

uint64_t Function::GenerateBlockId()
{
    return blockId++;
}

uint64_t Function::GenerateBlockGroupId()
{
    return blockGroupId++;
}

Block* Function::GetEntryBlock() const
{
    return body->GetEntryBlock();
}

std::string Function::ToString(size_t indent) const
{
    std::stringstream ss;
    ss << AddNewLineOrNot(annoInfo.ToString(indent));
    ss << IndentToString(indent) << attributes.ToString();
    if (isFastNative) {
        ss << "[fastNative] ";
    }
    if (isCFFIWrapper) {
        ss << "[CFFIWrapper] ";
    }
    ss << "Func " << identifier << TypeVecToString("<", genericTypeParams, ">") << "(";
    for (auto param : parameters) {
        ss << std::endl << param->ToString(indent + 1);
    }
    if (!parameters.empty()) {
        ss << std::endl;
        ss << IndentToString(indent);
    }
    ss << "): " << GetReturnType()->ToString();
    std::vector<std::string> comments;
    if (auto globalValComment = GlobalValueCommentToString(); !globalValComment.empty()) {
        comments.emplace_back(globalValComment);
    }
    if (auto gStr = GetGenericTypeConstaintsStr(genericTypeParams); !gStr.empty()) {
        comments.emplace_back(gStr);
    }
    if (funcKind != FuncKind::DEFAULT) {
        comments.emplace_back("kind: " + FUNCKIND_TO_STRING.at(funcKind));
    }
    if (genericDecl != nullptr) {
        comments.emplace_back("genericDecl: " + genericDecl->GetIdentifier());
    }
    if (paramDftValHostFunc != nullptr) {
        comments.emplace_back("paramDftValHostFunc: " + paramDftValHostFunc->GetIdentifier());
    }
    if (funcKind == FuncKind::LAMBDA) {
        comments.emplace_back("originalLambdaInfo: " + originalLambdaInfo.ToString());
    }
    if (!propLoc.IsInvalidPos()) {
        comments.emplace_back("propLoc: " + propLoc.ToString());
    }
    ss << CommentToString(comments);
    if (body != nullptr) {
        ss << std::endl << body->ToString(indent);
    }
    return ss.str();
}

const DebugLocation& Function::GetPropLocation() const
{
    return propLoc;
}

void Function::SetPropLocation(const DebugLocation& loc)
{
    propLoc = loc;
}

size_t Function::GetExpressionsNum() const
{
    if (body == nullptr) {
        return 0;
    }
    return body->GetExpressionsNum();
}

GlobalValue::GlobalValue(ValueKind kind, Type* ty, const std::string& identifier,
    const std::string& srcCodeIdentifier, const std::string& rawMangledName, const std::string& packageName)
    : Value(ty, identifier, kind),
    srcCodeIdentifier(srcCodeIdentifier), rawMangledName(rawMangledName), packageName(packageName)
{
}

const std::string& GlobalValue::GetPackageName() const
{
    return packageName;
}

std::string GlobalValue::GetSrcCodeIdentifier() const
{
    return srcCodeIdentifier;
}

const std::string& GlobalValue::GetRawMangledName() const
{
    return rawMangledName;
}

void GlobalValue::SetRawMangledName(const std::string& name)
{
    rawMangledName = name;
}

const std::set<std::string>& GlobalValue::GetFeatures() const
{
    return features;
}

void GlobalValue::SetFeatures(const std::set<std::string>& newFeatures)
{
    features = newFeatures;
}

const AnnoInfo& GlobalValue::GetAnnoInfo() const
{
    return annoInfo;
}

void GlobalValue::SetAnnoInfo(AnnoInfo&& info)
{
    annoInfo = std::move(info);
}

CustomTypeDef* GlobalValue::GetParentCustomTypeDef() const
{
    return declaredParent;
}

std::string GlobalValue::GlobalValueCommentToString() const
{
    std::vector<std::string> result;
    result.emplace_back(BaseCommentToString());
    if (!srcCodeIdentifier.empty()) {
        result.emplace_back("srcCodeIdentifier: " + srcCodeIdentifier);
    }
    if (!rawMangledName.empty()) {
        result.emplace_back("rawMangledName: " + rawMangledName);
    }
    if (!packageName.empty()) {
        result.emplace_back("packageName: " + packageName);
    }
    if (declaredParent != nullptr) {
        result.emplace_back("declaredParent: " + declaredParent->GetIdentifier());
    }
    if (!features.empty()) {
        std::vector<std::string> vec(features.begin(), features.end());
        result.emplace_back("features: {" + StringJoin(vec, ", ") + "}");
    }
    return StringJoin(result, ", ");
}