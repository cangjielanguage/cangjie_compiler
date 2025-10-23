// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_VALUE_H
#define CANGJIE_CHIR_VALUE_H

#include "cangjie/CHIR/AnnoInfo.h"
#include "cangjie/CHIR/AttributeInfo.h"
#include "cangjie/CHIR/Base.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/Utils/SafePointer.h"

#include <algorithm>
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
class Func;
class FuncBody;
class BlockGroup;
class Terminator;
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
    friend class Terminator;
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
        KIND_IMP_FUNC,
        KIND_IMP_VAR,
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

    bool IsGlobalVarInCurPackage() const;

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

    virtual std::string GetSrcCodeIdentifier() const;
    const std::string& GetIdentifier() const;
    std::string GetIdentifierWithoutPrefix() const;

    std::vector<Expression*> GetUsers() const;

    // we replace `this` with `newValue` in `scope`, when `scope` is nullptr, we replace nodes in package scope
    void ReplaceWith(Value& newValue, const BlockGroup* scope = nullptr);

    virtual std::string ToString() const = 0;
    void Dump() const;

    bool IsCompileTimeValue() const;

    // ===--------------------------------------------------------------------===//
    // Attribute
    // ===--------------------------------------------------------------------===//
    AttributeInfo GetAttributeInfo() const;
    void AppendAttributeInfo(const AttributeInfo& info);
    void DisableAttr(Attribute attr);
    void EnableAttr(Attribute attr);
    bool TestAttr(Attribute attr) const;

    // ===--------------------------------------------------------------------===//
    // Annotation
    // ===--------------------------------------------------------------------===//
    const AnnoInfo& GetAnnoInfo() const;
    void SetAnnoInfo(AnnoInfo&& info);
    
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
    AttributeInfo attributes;       // variable attribute
    std::vector<Expression*> users; // variable users
    std::mutex userMutex;           // mutex for AddUserOnly and RemoveUserOnly
    AnnoInfo annoInfo;              // annoInfo, used in struct/class/enum member func

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
    
    std::string ToString() const override;

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    Func* GetOwnerFunc() const;
    void SetOwnerFunc(Func* owner);

    Lambda* GetOwnerLambda() const;
    void SetOwnerLambda(Lambda* newParent);

    Func* GetTopLevelFunc() const;

    // ===--------------------------------------------------------------------===//
    // Debug Expression
    // ===--------------------------------------------------------------------===//
    // if parameter do not have debug, return nullptr
    Debug* GetDebugExpr() const;

private:
    Func* ownerFunc = nullptr;
    Lambda* ownerLambda = nullptr;

private:
    explicit Parameter(Type* ty, const std::string& indexStr, Func* ownerFunc);
    explicit Parameter(Type* ty, const std::string& indexStr, Lambda& ownerLambda);
    ~Parameter() override = default;
};

class LocalVar : public Value {
    friend class CHIRBuilder;
    friend class Expression;
    friend class Func;
    friend class Lambda;
    friend class CHIRDeserializer;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Expression* GetExpr() const;

    std::string GetSrcCodeIdentifier() const override;

    std::string ToString() const override;

    const DebugLocation& GetDebugLocation() const override;

    bool IsRetValue() const;

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    BlockGroup* GetOwnerBlockGroup() const;

    Func* GetTopLevelFunc() const;

    // ===--------------------------------------------------------------------===//
    // Debug Expression
    // ===--------------------------------------------------------------------===//
    // if localVar do not have debug, return nullptr
    Debug* GetDebugExpr() const;

private:
    explicit LocalVar(Type* ty, std::string indexStr, Expression* expr);
    ~LocalVar() override = default;

    void SetRetValue();

private:
    Expression* expr;        // The owner of this result value.
    bool isRetValue = false; // If func return value or not
};

/**
 * this class represent imported/non-imported global var and static member var in Cangjie's source code.
 */
class GlobalVarBase : public virtual Value {
    friend class CHIRBuilder;
    friend class CustomTypeDef;
    friend class ImportedVar;
    friend class GlobalVar;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    std::string GetSrcCodeIdentifier() const override;

    const std::string& GetPackageName() const;

    const std::string& GetRawMangledName() const;

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    /**
     * @brief only static member var has declaredParent, others return nullptr
     */
    CustomTypeDef* GetParentCustomTypeDef() const;

    void DestroySelf();

protected:
    std::string packageName; // package where this globalVar defined by user
    std::string srcCodeIdentifier;                // the name of global variable
    std::string rawMangledName;                   // rawMangledName, generated by Parser, used by Incremental Compile
    CustomTypeDef* declaredParent = nullptr; // e.g. class A { static var x = 1 }
                                                  // `A` is declaredParent of `x`

private:
    explicit GlobalVarBase(std::string srcCodeIdentifier, std::string rawMangledName, std::string packageName);
    ~GlobalVarBase() override = default;
};

class GlobalVar : public GlobalVarBase {
    friend class CHIRBuilder;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    LiteralValue* GetInitializer() const;
    void SetInitializer(LiteralValue& literalValue);
    
    Func* GetInitFunc() const;
    void SetInitFunc(Func& func);

    std::string ToString() const override;

    bool IsLocalConst() const;

protected:
    LiteralValue* initializer = nullptr;          // the initializer must be a LiteralValue if exist
    Func* initFunc{};                             // init function of this globalVar, null if initializer is not null

private:
    explicit GlobalVar(Type* ty, std::string identifier, std::string srcCodeIdentifier, std::string rawMangledName,
        std::string packageName);
    ~GlobalVar() override = default;
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

struct AbstractMethodParam {
    std::string paramName;
    Type* type = nullptr;
    AnnoInfo annoInfo;

    std::string ToString();
};

struct AbstractMethodInfo {
    std::string methodName;                      // abstract method name
    Type* methodTy = nullptr;                    // abstract method type
    std::vector<AbstractMethodParam> paramInfos; // abstract method parameters
    AttributeInfo attributeInfo;                 // abstract method attribute
    AnnoInfo annoInfo;                           // abstract method annoInfo
    std::vector<GenericType*> methodGenericTypeParams; // store `T` of `func foo<T>()`
    bool hasBody;                                // abstract method in interface may have func body
    ClassDef* parent = nullptr;
    AbstractMethodInfo(const std::string& methodName, const std::string& mangledName, Type* methodTy,
        const std::vector<AbstractMethodParam>& paramInfos, const AttributeInfo& attrInfo, const AnnoInfo& annoInfo,
        const std::vector<GenericType*>& methodGenericTypeParams, bool hasBody, ClassDef* parent)
        : methodName(methodName),
          methodTy(methodTy),
          paramInfos(paramInfos),
          attributeInfo(attrInfo),
          annoInfo(annoInfo),
          methodGenericTypeParams(methodGenericTypeParams),
          hasBody(hasBody),
          parent(parent),
          mangledName(mangledName)
          
    {
    }
    bool TestAttr(Attribute attr) const
    {
        return attributeInfo.TestAttr(attr);
    }

    // This function will append ".0" to the original mangledName from AST to avoid backend issues.
    std::string GetMangledName() const
    {
        return mangledName + ".0";
    }

    std::string GetASTMangledName() const
    {
        return mangledName;
    }

private:
    std::string mangledName;            // abstract method mangled name
};

class ImportedValue : public virtual Value {
    friend class CHIRBuilder;
    friend class ImportedVar;
    friend class ImportedFunc;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    std::string ToString() const override;

    virtual const std::string& GetSourcePackageName() const = 0;

    // ===--------------------------------------------------------------------===//
    // Modify Self
    // ===--------------------------------------------------------------------===//
    virtual void DestroySelf();

private:
    explicit ImportedValue();
    ~ImportedValue() override = default;
};

class Block : public Value {
    friend class CHIRContext;
    friend class CHIRBuilder;
    friend class Expression;
    friend class Terminator;
    friend class BlockGroup;
    friend class CHIRDeserializer;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    std::string ToString() const override;

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
    Terminator* GetTerminator() const;

    void InsertExprIntoHead(Expression& expr);

    // ===--------------------------------------------------------------------===//
    // Parent
    // ===--------------------------------------------------------------------===//
    BlockGroup* GetParentBlockGroup() const;
    void SetParentBlockGroup(BlockGroup* parent);
    
    Func* GetTopLevelFunc() const;

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

    void AppendNonTerminatorExpression(Expression* expression);
    void AppendTerminator(Terminator* term);

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
    friend class Lambda;
    friend class ForIn;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    size_t GetExpressionsNum() const;
    std::string ToString() const override;

    // ===--------------------------------------------------------------------===//
    // Block
    // ===--------------------------------------------------------------------===//
    void AddBlock(Block* block);
    void AddBlocks(const std::vector<Block*>& newBlocks);
    Block* GetBlockByIdx(size_t idx) const;
    std::vector<Block*> GetBlocks() const;

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
    Func* GetTopLevelFunc() const;

    Func* GetOwnerFunc() const;
    void SetOwnerFunc(Func* func);

    Expression* GetOwnerExpression() const;

    BlockGroup* Clone(CHIRBuilder& builder, Func& newFunc) const;
    BlockGroup* Clone(CHIRBuilder& builder, Lambda& newLambda) const;
private:
    explicit BlockGroup(std::string identifier);
    ~BlockGroup() override = default;

    void ClearBlockGroup();

    void ClearBlocksOnly();

    void SetOwnedFuncOnly(Func* newFunc);

    void SetOwnerExpression(Expression& expr);

    void RemoveBlock(Block& block);

    void CloneBlocks(CHIRBuilder& builder, BlockGroup& parent) const;

private:
    Block* entryBlock = nullptr; // block group entryBlock
    std::vector<Block*> blocks;
    Func* ownerFunc = nullptr;
    /**< can only be Lambda/ForIn/If/Loop */
    Expression* ownerExpression = nullptr;
};

/**
 * @brief FuncBody class is an interlayer.
 *
 * FuncBody is used to keep parameters and body of function and lambda
 */
class FuncBody {
    friend class Func;
    friend class Lambda;
private:
    BlockGroup* GetBody() const;
    void RemoveBody();
    void RemoveParams();

    void AddParam(Parameter& param);
    Parameter* GetParam(size_t index) const;
    const std::vector<Parameter*>& GetParams() const;

    LocalVar* GetReturnValue() const;
    void SetReturnValue(LocalVar& ret);

    explicit FuncBody();

private:
    BlockGroup* body = nullptr;
    std::vector<Parameter*> parameters;
    LocalVar* retValue = nullptr;
};

class FuncBase : public virtual Value {
    friend class TypeConverterForCC;
    friend class ValueTypeConverter;
    friend class CustomTypeDef;
    friend class ImportedFunc;
    friend class VirtualMethodInfo;
    friend class Func;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    bool IsConstructor() const;
    bool IsFinalizer() const;
    bool IsGVInit() const;
    bool IsLambda() const;
    bool IsPrimalConstructor() const;
    bool IsInstanceVarInit() const;
    bool IsStaticInit() const;
    FuncKind GetFuncKind() const;
    void SetFuncKind(FuncKind kind);

    const std::string& GetPackageName() const;

    std::string GetSrcCodeIdentifier() const override;

    const std::string& GetRawMangledName() const;
    void SetRawMangledName(const std::string& name);

    FuncBase* GetGenericDecl() const;
    void SetGenericDecl(FuncBase& decl);

    bool IsFastNative() const;
    void SetFastNative(bool fastNative);

    bool IsCFFIWrapper() const;
    void SetCFFIWrapper(bool isWrapper);

    FuncBase* GetParamDftValHostFunc() const;
    void SetParamDftValHostFunc(FuncBase& hostFunc);
    void ClearParamDftValHostFunc();

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
    // Parent
    // ===--------------------------------------------------------------------===//
    CustomTypeDef* GetParentCustomTypeDef() const;

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

    // ===--------------------------------------------------------------------===//
    // Modify Self
    // ===--------------------------------------------------------------------===//
    virtual void DestroySelf();

protected:
    std::string srcCodeIdentifier; // origin name
    std::string rawMangledName;
    std::string packageName;
    CustomTypeDef* declaredParent{nullptr};       // e.g. class A { func foo() {} }
                                                  //      `class A` is declaredParent of `foo`
                                                  // e.g. extend A { func goo() {} }
                                                  //      `extend A` is declaredParent of `goo`
    FuncBase* genericDecl = nullptr;              // original generic decl from which current instantiated decl derives
    FuncKind funcKind = FuncKind::DEFAULT;
    bool isFastNative{};        // is this func is annotated with @FastNative
    bool isCFFIWrapper = false; // when generated by CFFI wrapper.
    /** 1. when lambda is lifted to global func, its generic type params and func type may be changed,
     *  but cjdb need original type to show
     *  e.g. func foo<T1>() {
     *      func goo<T2>() {} // this lambda will be lifted to global func
     *  }
     *  after being lifted, global func `goo` is as follow:
     *  func goo<T1, T2>(env: class&) {}
     */
    FuncSigInfo originalLambdaInfo;
    std::vector<GenericType*> genericTypeParams;
    /** func foo(a !: Bool = true) {}
     *  param `a` is desugared by sema to global function named `a.0`
     *  we name `foo` is host function of `a.0`, so `paramDftValHostFunc` of `a.0` is `foo`
     */
    FuncBase* paramDftValHostFunc = nullptr;

private:
    FuncBase(const std::string& srcCodeIdentifier, const std::string& rawMangledName,
        const std::string& packageName, const std::vector<GenericType*>& genericTypeParams);
};

/*
 * @brief Func class in CHIR.
 *
 * Func is an expression with a block group.
 */
class Func : public FuncBase {
    friend class BlockGroup;
    friend class CHIRBuilder;
    friend class ValueTypeConverter;
    friend class CHIRSerializer;
    friend class CHIRDeserializer;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    std::string ToString() const override;

    uint64_t GenerateBlockId();
    uint64_t GenerateBlockGroupId();
    uint64_t GenerateLocalId();
    void InheritIDFromFunc(const Func& func);

    const DebugLocation& GetPropLocation() const;
    void SetPropLocation(const DebugLocation& loc);

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
    // Modify Self
    // ===--------------------------------------------------------------------===//
    void DestroySelf() override;

    // ===--------------------------------------------------------------------===//
    // Incremental Compile
    // ===--------------------------------------------------------------------===//
    /**
     * Gets the RawMangledName of parent if parent is ExtendDecl. Returns null otherwise. This function is only used in
     * collection of CHIR optimisation in incremental compilation and should not be used elsewhere.
     */
    const std::string& GetParentRawMangledName() const;
    /**
     * Sets the RawMangledName if parent is ExtendDecl. This function is only used in collection of CHIR optimisation
     * in incremental compilation and should not be used elsewhere.
     */
    void SetParentRawMangledName(const std::string& name);

private:
    void DestroyFuncBody();
    void RemoveBody();
    void RemoveParams();

    void SetLocalId(uint64_t id);
    void SetBlockId(uint64_t id);
    void SetBlockGroupId(uint64_t id);

private:
    explicit Func(Type* ty, const std::string& identifier, const std::string& srcCodeIdentifier,
        const std::string& rawMangledName, const std::string& packageName,
        const std::vector<GenericType*>& genericTypeParams = {});

    ~Func() override = default;

    FuncBody body;
    std::string parentName;    // a hack, if parent is extend decl, we store its raw mangled name, only extend decl
    DebugLocation propLoc;     // when this is setter or getter, it has prop location, needed by cjlint
    uint64_t localId = 0;      // the local Id in expression
    uint64_t blockId = 0;      // the id in block
    uint64_t blockGroupId = 0; // the id in block group
};

class ImportedFunc : public ImportedValue, public FuncBase {
    friend class CHIRBuilder;
    friend class ValueTypeConverter;

public:
    const std::vector<AbstractMethodParam>& GetParamInfo() const;
    void SetParamInfo(std::vector<AbstractMethodParam>&& params);

    const std::string& GetSourcePackageName() const override;

    ~ImportedFunc() override = default;

    std::string ToString() const override;

private:
    ImportedFunc(Type* ty, const std::string& identifier, const std::string& srcCodeIdentifier,
        const std::string& rawMangledName, const std::string& packageName,
        const std::vector<GenericType*>& genericTypeParams = {});

private:
    // In incremental compilation, non-recompiled funcs will be represented as an `importedValue`,
    // in this case, we need to record more information
    // Note: some of this is overlapped with other meta-datas, and we should have this not only for
    // incremental compilation, but all cases
    std::vector<AbstractMethodParam> paramInfo; // abstract method parameters
};

class ImportedVar : public ImportedValue, public GlobalVarBase {
    friend class CHIRBuilder;

public:
    const std::string& GetSourcePackageName() const override;

private:
    explicit ImportedVar(Type* ty, std::string identifier, std::string srcCodeIdentifier, std::string rawMangledName,
        std::string packageName);
    ~ImportedVar() override = default;
};
} // namespace Cangjie::CHIR

#endif
