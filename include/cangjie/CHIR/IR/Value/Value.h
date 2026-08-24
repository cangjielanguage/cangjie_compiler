// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_VALUE_H
#define CANGJIE_CHIR_VALUE_H

#include "cangjie/CHIR/IR/AnnoInfo.h"
#include "cangjie/CHIR/IR/Base.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/UserDefinedType.h"
#include "cangjie/Utils/SafePointer.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_set>
#include <vector>

namespace Cangjie::CHIR {
class Allocate;
class Expression;
class Constant;
class Function;
class BlockGroup;
class CustomTypeDef;
class FuncType;
class Debug;
class Lambda;
class CHIRBuilder;
class ClassDef;
class LiteralValue;

class Value : public Base {
    friend class CHIRContext;
    friend class CHIRBuilder;
    friend class Expression;
    friend class ValueTypeConverter;
    template <typename> friend class ValueTypeFunctor;
    friend class TypeConverterForCC;
    friend class CustomDefTypeConverter;
    friend class CHIRDeserializer;
    friend class CHIRSerializer;

public:
    enum ValueKind : uint8_t {
        KIND_LITERAL,
        KIND_GLOBALVAR,
        KIND_PARAMETER,
        KIND_LOCALVAR,
        KIND_FUNC,
        KIND_BLOCK,
        KIND_BLOCK_GROUP
    };

    // ===--------------------------------------------------------------------===//
    // Base Infomation
    // ===--------------------------------------------------------------------===//
    bool IsBlock() const;

    bool IsBlockGroup() const;

    // including:
    // 1. imported function
    // 2. function in current package
    bool IsFunc() const;

    // including:
    // 1. func declared in current package
    // 2. func declared in imported pacakge but instantiated in current package
    // 3. func declared in imported pacakge with `const`
    // 4. func declared in imported pacakge with @Frozen and compiled with O2
    bool IsFuncWithBody() const;

    // including:
    // 1. func and global var in current package
    // 2. imported func and imported global var
    bool IsGlobal() const;

    // including:
    // 1. imported global var
    // 2. global var in current package
    bool IsGlobalVar() const;

    bool IsGlobalVarWithInitializer() const;

    bool IsImportedFunc() const;
    
    // including:
    // 1. imported function
    // 2. imported global var
    bool IsImportedSymbol() const;

    bool IsImportedVar() const;

    bool IsLiteral() const;

    bool IsLocalVar() const;

    // including:
    // 1. parameter in function
    // 2. parameter in lambda
    bool IsParameter() const;

    Type* GetType() const;
    void SetType(Type& newType);

    virtual std::string GetSrcCodeIdentifier() const;
    const std::string& GetIdentifier() const;
    std::string GetIdentifierWithoutPrefix() const;

    std::vector<Expression*> GetUsers() const;

    // we replace `this` with `newValue` in `scope`, when `scope` is nullptr, we replace nodes in package scope
    void ReplaceWith(Value& newValue, const BlockGroup* scope = nullptr);

    virtual std::string ToString(size_t indent) const = 0;
    void Dump() const;

    bool IsCompileTimeValue() const;

    /**
     * @brief Retrieves the function or lambda body which this value belongs to.
     *
     * For a parameter/local-var/block/block-group/function, walks up to the owning
     * function or lambda and returns its body. Returns nullptr for values that do
     * not belong to a function or lambda body.
     *
     * @return The function or lambda body, or nullptr.
     */
    BlockGroup* GetFuncOrLambdaBody() const;

protected:
    explicit Value(Type* ty, std::string identifier, ValueKind kind);
    virtual ~Value() = default;

    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;

    // ===--------------------------------------------------------------------===//
    // User
    // ===--------------------------------------------------------------------===//
    void AddUserOnly(Expression* expr);
    void ClearUsersOnly();
    void RemoveUserOnly(Expression* expr);

protected:
    Type* ty;                       // variable type
    std::string identifier;         // variable identifier
    std::vector<Expression*> users; // variable users
    std::mutex userMutex;           // mutex for AddUserOnly and RemoveUserOnly

private:
    ValueKind GetValueKind() const;

    ValueKind kind;                 // value kind
};

class Parameter : public Value {
    friend class CHIRBuilder;
    friend class FuncBody;
    friend class Expression;
    friend class Lambda;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    std::string GetSrcCodeIdentifier() const override;
    void SetSrcCodeIdentifier(const std::string& newName);

    std::string ToString(size_t indent) const override;

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    Function* GetOwnerFunc() const;
    void SetOwnerFunc(Function* owner);

    Lambda* GetOwnerLambda() const;
    void SetOwnerLambda(Lambda* newParent);

    Function* GetTopLevelFunc() const;

    // ===--------------------------------------------------------------------===//
    // Annotation
    // ===--------------------------------------------------------------------===//
    const AnnoInfo& GetAnnoInfo() const;
    void SetAnnoInfo(AnnoInfo&& info);

    // ===--------------------------------------------------------------------===//
    // Debug Expression
    // ===--------------------------------------------------------------------===//
    // if parameter do not have debug, return nullptr
    Debug* GetDebugExpr() const;

private:
    Function* ownerFunc = nullptr;
    Lambda* ownerLambda = nullptr;
    std::string srcCodeIdentifier;
    AnnoInfo annoInfo;

private:
    explicit Parameter(Type* ty, const std::string& id, Function* ownerFunc);
    explicit Parameter(Type* ty, const std::string& id, Lambda& ownerLambda);
    ~Parameter() override = default;
};

class LocalVar : public Value {
    friend class CHIRBuilder;
    friend class Expression;
    friend class Function;
    friend class Lambda;
    friend class CHIRDeserializer;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Expression* GetExpr() const;

    std::string GetSrcCodeIdentifier() const override;
    void SetSrcCodeIdentifier(const std::string& newName);

    std::string ToString(size_t indent) const override;

    const DebugLocation& GetDebugLocation() const override;

    bool IsRetValue() const;

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    BlockGroup* GetOwnerBlockGroup() const;

    Function* GetTopLevelFunc() const;

    // ===--------------------------------------------------------------------===//
    // Debug Expression
    // ===--------------------------------------------------------------------===//
    // if localVar do not have debug, return nullptr
    Debug* GetDebugExpr() const;

private:
    explicit LocalVar(Type* ty, std::string indexStr, Expression* expr);
    ~LocalVar() override = default;

    void SetRetValue(bool flag);

private:
    Expression* expr;        // The owner of this result value.
    bool isRetValue = false; // If func return value or not
    std::string srcCodeIdentifier;
};

enum FuncKind : uint8_t {
    DEFAULT, // Default funcKind
    GETTER,
    SETTER,
    LAMBDA, // cced lambda
    CLASS_CONSTRUCTOR,
    PRIMAL_CLASS_CONSTRUCTOR,
    STRUCT_CONSTRUCTOR,
    PRIMAL_STRUCT_CONSTRUCTOR,
    GLOBALVAR_INIT,
    FINALIZER,
    MAIN_ENTRY,
    ANNOFACTORY_FUNC,
    MACRO_FUNC,
    DEFAULT_PARAMETER_FUNC,
    INSTANCEVAR_INIT, /**< These functions are related to initialization of class/struct's instance variables */
    FUNCKIND_END
};

const std::unordered_map<FuncKind, std::string> FUNCKIND_TO_STRING{{FuncKind::DEFAULT, "default"},
    {FuncKind::GETTER, "getter"}, {FuncKind::SETTER, "setter"}, {FuncKind::LAMBDA, "lambda"},
    {FuncKind::CLASS_CONSTRUCTOR, "classConstructor"}, {FuncKind::PRIMAL_CLASS_CONSTRUCTOR, "classPrimalConstructor"},
    {FuncKind::STRUCT_CONSTRUCTOR, "structConstructor"},
    {FuncKind::PRIMAL_STRUCT_CONSTRUCTOR, "structPrimalConstructor"}, {FuncKind::GLOBALVAR_INIT, "globalVarInit"},
    {FuncKind::FINALIZER, "finalizer"}, {FuncKind::MAIN_ENTRY, "mainEntry"},
    {FuncKind::ANNOFACTORY_FUNC, "annoFactory"}, {FuncKind::MACRO_FUNC, "macro"},
    {FuncKind::DEFAULT_PARAMETER_FUNC, "defaultParameter"}, {FuncKind::INSTANCEVAR_INIT, "memberVariablesInit"}};

class Block : public Value {
    friend class CHIRContext;
    friend class CHIRBuilder;
    friend class Expression;
    friend class BlockGroup;
    friend class CHIRDeserializer;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent) const override;

    std::vector<Block*> GetSuccessors() const;
    std::vector<Block*> GetPredecessors() const;

    std::vector<ClassType*> GetExceptions() const;
    void SetExceptions(const std::vector<ClassType*>& ep);
    bool IsLandingPadBlock() const;

    bool IsEntry() const;

    // ===--------------------------------------------------------------------===//
    // Expressions
    // ===--------------------------------------------------------------------===//
    void AppendExpressions(const std::vector<Expression*>& expressions);
    void AppendExpression(Expression* expression);
    Expression* GetExpressionByIdx(size_t idx) const;
    std::vector<Expression*> GetExpressions() const;
    size_t GetExpressionsNum() const;
    std::vector<Expression*> GetNonTerminatorExpressions() const;
    Expression* GetTerminator() const;

    void InsertExprIntoHead(Expression& expr);

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    BlockGroup* GetParentBlockGroup() const;
    void SetParentBlockGroup(BlockGroup* parent);
    
    Function* GetTopLevelFunc() const;

    /**
     * @brief Retrieves the function or lambda body which this block belongs to.
     *
     * @return The function or lambda body.
     */
    BlockGroup* GetFuncOrLambdaBody() const;

    // ===--------------------------------------------------------------------===//
    // Modify Self
    // ===--------------------------------------------------------------------===//
    void MoveTo(BlockGroup& newBlockGroup);
    void RemoveSelfFromBlockGroup();

    Block* Clone(CHIRBuilder& builder, BlockGroup& newGroup) const;

private:
    explicit Block(std::string identifier, BlockGroup* parentGroup);
    ~Block() override = default;
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;

    void RemoveExprOnly(Expression& expr);
    void AddPredecessor(Block* block);

    void RemovePredecessor(Block& block);

    void AppendExprOnly(Expression& expr);

    void ClearExprsOnly();

    void AppendPredecessorOnly(Block& block);
    void RemovePredecessorOnly(Block& block);
    void ClearPredecessorsOnly();

private:
    BlockGroup* parentGroup;          // block parent block group
    std::vector<Expression*> exprs;   // block expressions
    std::vector<Block*> predecessors; // predecessors
    /**
     * @brief the exceptions info
     *   nullopt for general block
     *   empty vector for landingpad block catch all exceptions
     *   non-empty vector for landingpad block catch specific exceptions
     */
    std::optional<std::vector<ClassType*>> exceptions{std::nullopt};
};

/**
 * Consist of a group of Blocks, included by Func、Lambda、For、Loop、If
 */
class BlockGroup : public Value {
    friend class CHIRContext;
    friend class CHIRBuilder;
    friend class Block;
    friend class Exclave;
    friend class Lambda;
    friend class ForIn;
    friend class Expression;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    size_t GetExpressionsNum() const;
    std::string ToString(size_t indent) const override;

    // ===--------------------------------------------------------------------===//
    // Block
    // ===--------------------------------------------------------------------===//
    void AddBlock(Block* block);
    void AddBlocks(const std::vector<Block*>& newBlocks);
    Block* GetBlockByIdx(size_t idx) const;
    std::vector<Block*> GetBlocks() const;
    std::vector<Block*> GetAllBlocks() const;
    Block* GetEntryBlock() const;
    void SetEntryBlock(Block* block);

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    /**
     * @brief Get the func where this blockGroup located
     *
     * if this blockGroup belongs to If/Loop/ForIn/Lambda,
     * the method will also continue looking up until ownerFunc is not empty.
     */
    Function* GetTopLevelFunc() const;
    /**
     * @brief Retrieves the function or lambda body which this block group belongs to.
     *
     * @return The function or lambda body.
     */
    BlockGroup* GetFuncOrLambdaBody() const;

    Function* GetOwnerFunc() const;
    void SetOwnerFunc(Function* func);

    Expression* GetOwnerExpression() const;

    BlockGroup* Clone(CHIRBuilder& builder, Function& newFunc) const;
    BlockGroup* Clone(CHIRBuilder& builder, Expression& newExpression) const;
private:
    explicit BlockGroup(std::string identifier);
    ~BlockGroup() override = default;

    void ClearBlockGroup();

    void ClearBlocksOnly();

    void SetOwnedFuncOnly(Function* newFunc);

    void SetOwnerExpression(Expression& expr);

    void RemoveBlock(Block& block);

    void CloneBlocks(CHIRBuilder& builder, BlockGroup& parent) const;

private:
    Block* entryBlock = nullptr; // block group entryBlock
    std::vector<Block*> blocks;
    Function* ownerFunc = nullptr;
    /**< can only be Lambda/ForIn/If/Loop */
    Expression* ownerExpression = nullptr;
};

class GlobalValue : public Value {
    friend class CustomTypeDef;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    const std::string& GetPackageName() const;
    std::string GetSrcCodeIdentifier() const override;

    const std::string& GetRawMangledName() const;
    void SetRawMangledName(const std::string& name);

    const std::set<std::string>& GetFeatures() const;
    void SetFeatures(const std::set<std::string>& newFeatures);

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    CustomTypeDef* GetParentCustomTypeDef() const;

    // ===--------------------------------------------------------------------===//
    // Attribute
    // ===--------------------------------------------------------------------===//
    virtual bool IsSrcCodeImported() const = 0;

    // ===--------------------------------------------------------------------===//
    // Annotation
    // ===--------------------------------------------------------------------===//
    const AnnoInfo& GetAnnoInfo() const;
    void SetAnnoInfo(AnnoInfo&& info);

    // ===--------------------------------------------------------------------===//
    // Modify Self
    // ===--------------------------------------------------------------------===//
    virtual void DestroySelf() = 0;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    std::string GlobalValueCommentToString() const;

protected:
    explicit GlobalValue(ValueKind kind, Type* ty, const std::string& identifier,
        const std::string& srcCodeIdentifier, const std::string& rawMangledName, const std::string& packageName);

private:
    GlobalValue(const GlobalValue&) = delete;
    GlobalValue& operator=(const GlobalValue&) = delete;

protected:
    std::string srcCodeIdentifier;
    std::string rawMangledName;
    std::string packageName;
    CustomTypeDef* declaredParent{nullptr};
    std::set<std::string> features;
    AnnoInfo annoInfo;
};

class Function : public GlobalValue {
    friend class CHIRContext;
    friend class CHIRBuilder;
    friend class CHIRDeserializer;
    friend class CHIRSerializer;
    friend class TypeConverterForCC;
    friend class ValueTypeConverter;
    friend class BlockGroup;
    friend class VirtualMethodInfo;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    std::string ToString(size_t indent) const override;

    bool IsConstructor() const;
    bool IsFinalizer() const;
    bool IsGVInit() const;
    bool IsLambda() const;
    bool IsPrimalConstructor() const;
    bool IsInstanceVarInit() const;
    bool IsStaticInit() const;
    FuncKind GetFuncKind() const;
    void SetFuncKind(FuncKind kind);

    Function* GetGenericDecl() const;
    void SetGenericDecl(Function& decl);

    bool IsFastNative() const;
    void SetFastNative(bool fastNative);

    bool IsCFFIWrapper() const;
    void SetCFFIWrapper(bool isWrapper);

    Function* GetParamDftValHostFunc() const;
    void SetParamDftValHostFunc(Function& hostFunc);
    void ClearParamDftValHostFunc();

    uint64_t GenerateBlockId();
    uint64_t GenerateBlockGroupId();
    uint64_t GenerateLocalId();
    void InheritIDFromFunc(const Function& func);

    const DebugLocation& GetPropLocation() const;
    void SetPropLocation(const DebugLocation& loc);

    // ===--------------------------------------------------------------------===//
    // Signature Infomation
    // ===--------------------------------------------------------------------===//
    void SetOriginalLambdaInfo(const FuncSigInfo& info);
    FuncType* GetOriginalLambdaType() const;
    std::vector<GenericType*> GetOriginalGenericTypeParams() const;

    size_t GetNumOfParams() const;
    FuncType* GetFuncType() const;
    Type* GetReturnType() const;

    const std::vector<GenericType*>& GetGenericTypeParams() const;

    // ===--------------------------------------------------------------------===//
    // Func Body
    // ===--------------------------------------------------------------------===//
    BlockGroup* GetBody() const;
    void InitBody(BlockGroup& newBody);
    void ReplaceBody(BlockGroup& newBody);

    Block* GetEntryBlock() const;

    void AddParam(Parameter& param);
    Parameter* GetParam(size_t index) const;
    const std::vector<Parameter*>& GetParams() const;

    /**
     * @brief get a `LocalVar` represent the returned value of this Func.
     */
    LocalVar* GetReturnValue() const;
    bool HasReturnValue() const;
    void SetReturnValue(LocalVar& ret);

    size_t GetExpressionsNum() const;

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    Type* GetParentCustomTypeOrExtendedType() const;

    CustomTypeDef* GetOuterDeclaredOrExtendedDef() const;

    /**
     * @brief a function in class, struct, enum or extend
     */
    bool IsMemberFunc() const;

    /**
     * @brief a method declared in class, including in extend class
     */
    bool IsClassMethod() const;

    /**
     * @brief a method declared in struct, including in extend struct
     */
    bool IsStructMethod() const;

    /**
     * @brief a method declared in enum, including in extend enum
     */
    bool IsEnumMethod() const;

    /**
     * @brief a method defined within `Extend` scope
     */
    bool IsInExtend() const;
    // ===--------------------------------------------------------------------===//
    // Attribute
    // ===--------------------------------------------------------------------===//
    /**
     * @brief there are two cases:
     * 1. func is generic, that means `TestAttr(Attribute::GENERIC)` is true
     * 2. func has parent CustomTypeDef, and its parent is generic
     */
    bool IsInGenericContext() const;

    bool IsCFunc() const;

    bool IsVirtualFunc() const;

    bool IsPureAbstract() const;

    // including:
    // 1. declared in imported package, is marked with @Frozen and compiled with O2
    // 2. declared in imported package, is marked with `const`
    // 3. a generic function from imported package, is instantiated in current package
    bool IsSrcCodeImported() const override;

    // ===--------------------------------------------------------------------===//
    // Modify Self
    // ===--------------------------------------------------------------------===//
    void DestroySelf() override;

    void DestroyFuncBody();
    /**
     * @brief Replace the return value of this function and update the function type accordingly.
     *
     * This method updates the function's return type based on the new return value:
     * - If `newRet` is nullptr, the function's return type is changed to Void.
     * - If `newRet` is not nullptr, the function's return type is set to the base type
     *   extracted from `newRet`'s RefType.
     *
     * Note: This is a base class implementation that only updates the function type.
     * The derived class `Function` overrides this to also update the function body's return value.
     *
     * @param newRet The new return value LocalVar. If nullptr, the function will return Void.
     *               Must be a RefType if not nullptr.
     * @param builder The CHIRBuilder used to create or get the updated function type.
     *
     * @see Function::ReplaceReturnValue for the full implementation that also updates the body.
     */
    virtual void ReplaceReturnValue(LocalVar* newRet, CHIRBuilder& builder);

private:
    explicit Function(Type* ty, const std::string& identifier, const std::string& srcCodeIdentifier,
        const std::string& rawMangledName, const std::string& packageName,
        const std::vector<GenericType*>& genericTypeParams = {});
    ~Function() override = default;
    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;

    void RemoveBody();
    void RemoveParams();

    void SetLocalId(uint64_t id);
    void SetBlockId(uint64_t id);
    void SetBlockGroupId(uint64_t id);

private:
    std::vector<Parameter*> parameters;
    std::vector<GenericType*> genericTypeParams;
    BlockGroup* body{nullptr};
    LocalVar* retValue{nullptr};
    uint64_t localId{0};
    uint64_t blockId{0};
    uint64_t blockGroupId{0};
    Function* genericDecl{nullptr};
    FuncKind funcKind{FuncKind::DEFAULT};
    bool isFastNative{false};
    bool isCFFIWrapper{false};
    FuncSigInfo originalLambdaInfo;
    Function* paramDftValHostFunc{nullptr};
    DebugLocation propLoc;
};

class GlobalVar : public GlobalValue {
    friend class CHIRContext;
    friend class CHIRBuilder;
    friend class CHIRDeserializer;
    friend class CHIRSerializer;
    friend class TypeConverterForCC;
    friend class ValueTypeConverter;
public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Value* GetInitializerValue() const;
    
    LiteralValue* GetInitializer() const;
    void SetInitializer(LiteralValue& literalValue);

    Function* GetInitFunc() const;
    void SetInitFunc(Function& func);

    std::string ToString(size_t indent) const override;

    // ===--------------------------------------------------------------------===//
    // Attribute
    // ===--------------------------------------------------------------------===//
    bool IsSrcCodeImported() const override;

    bool IsLocalConst() const;

    // ===--------------------------------------------------------------------===//
    // Modify Self
    // ===--------------------------------------------------------------------===//
    void DestroySelf() override;

    void DestroyInitializer();

private:
    explicit GlobalVar(Type* ty, const std::string& identifier, const std::string& srcCodeIdentifier,
        const std::string& rawMangledName, const std::string& packageName);
    ~GlobalVar() override = default;
    GlobalVar(const GlobalVar&) = delete;
    GlobalVar& operator=(const GlobalVar&) = delete;

private:
    Value* initializer{nullptr};
};
} // namespace Cangjie::CHIR

#endif
