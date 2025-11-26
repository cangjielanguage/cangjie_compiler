// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares Translator
 */

#ifndef CANGJIE_CHIR_TRANSLATOR_H
#define CANGJIE_CHIR_TRANSLATOR_H

#include "cangjie/AST/NodeX.h"
#include "cangjie/CHIR/AST2CHIR/AST2CHIRNodeMap.h"
#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/ExceptionTypeMapping.h"
#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/CHIRType.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Sema/GenericInstantiationManager.h"
#include "cangjie/Utils/SafePointer.h"

namespace Cangjie::CHIR {

class Translator {
public:
    Translator(CHIRBuilder& builder, CHIRType& chirTy, const Cangjie::GlobalOptions& opts,
        const GenericInstantiationManager* gim, AST2CHIRNodeMap<Value>& globalSymbolTable,
        const ElementList<Ptr<const AST::Decl>>& localConstVars,
        const ElementList<Ptr<const AST::FuncDecl>>& localConstFuncs, const IncreKind& kind,
        std::vector<std::pair<const AST::Decl*, Func*>>& annoFactories,
        std::unordered_map<Block*, Terminator*>& maybeUnreachable,
        bool computeAnnotations,
        std::vector<CHIR::Func*>& initFuncForAnnoFactory)
        : builder(builder),
          chirTy(chirTy),
          globalSymbolTable(globalSymbolTable),
          localConstVars(localConstVars),
          localConstFuncs(localConstFuncs),
          opts(opts),
          gim(gim),
          increKind(kind),
          annoFactoryFuncs(annoFactories),
          maybeUnreachable(maybeUnreachable),
          isComputingAnnos{computeAnnotations},
          initFuncsForAnnoFactory{initFuncForAnnoFactory}
    {
    }

    /**
     * @brief Translates an AST node into a Value.
     *
     * @param node The AST node to be translated.
     * @param trans The translator instance used for translation.
     * @return A pointer to the translated Value.
     */
    static Ptr<Value> TranslateASTNode(const AST::Node& node, Translator& trans)
    {
        auto base = &node;
        auto backBlock = trans.currentBlock;
        auto nodePtr = GetDesugaredExpr(node);
        if (auto expr = DynamicCast<AST::Expr*>(nodePtr); expr && expr->mapExpr != nullptr) {
            base = expr->mapExpr;
        }
        if (auto res = trans.exprValueTable.TryGet(*base)) {
            return res;
        }
        Ptr<Value> res = nullptr;
        switch (nodePtr->astKind) {
#define ASTKIND(KIND, VALUE, NODE, SIZE)                                                                               \
    case AST::ASTKind::KIND: {                                                                                         \
        res = trans.Visit(*AST::StaticAs<AST::ASTKind::KIND>(nodePtr));                                                \
        break;                                                                                                         \
    }
#include "cangjie/AST/ASTKind.inc"
#undef ASTKIND
            default:
                res = trans.Visit(*nodePtr);
        }

        /* There are two cases need add `Goto` when translate AST::Block
            case1: if the Block node is a desugar node, then add `Goto`.
            example code：
                                                                                                 |     |   desugar block
            `print("${a.a.b}\n")` => desugar to  `print({var tmp1 = Stringbuilder(); tmp1.append({a.a.b})})`
                                                        |                                                | desugar block
            case2: unsafe block.
            example code:
            var a = unsafe{}
        */
        if (auto subBlock = DynamicCast<AST::Block>(nodePtr)) {
            if (nodePtr != &node || nodePtr->TestAttr(AST::Attribute::UNSAFE)) {
                trans.CreateAndAppendTerminator<GoTo>(trans.GetBlockByAST(*subBlock), backBlock);
            }
        }
        return res;
    }

    struct InstCalleeInfo {
        Type* instParentCustomTy{nullptr};
        Type* thisType{nullptr};
        std::vector<Type*> instParamTys;
        Type* instRetTy{nullptr};
    };

    struct InstInvokeCalleeInfo {
        std::string srcCodeIdentifier;
        FuncType* instFuncType{nullptr};
        FuncType* originalFuncType{nullptr}; // not ()->Unit, include this type
        ClassType* instParentCustomTy{nullptr};
        ClassType* originalParentCustomTy{nullptr};
        std::vector<Type*> instantiatedTypeArgs;
        Type* thisType{nullptr};
        size_t offset{0};
    };
    // === static helper functions ==
    /**
     * @brief Retrieves the constructor ID of an enum.
     *
     * @param target The enum declaration.
     * @return The constructor ID of the enum.
     */
    static uint64_t GetEnumCtorId(const AST::Decl& target);
    
    /**
     * @brief Retrieves the pattern ID of an enum pattern.
     *
     * @param enumPattern The enum pattern.
     * @return The pattern ID of the enum.
     */
    static uint64_t GetEnumPatternID(const AST::EnumPattern& enumPattern);

    /**
     * @brief Translates a type from AST to CHIR.
     *
     * @param ty The type in AST format.
     * @return The translated type.
     */
    Ptr<Type> TranslateType(AST::Ty& ty);
    
    /**
     * @brief Retrieves the debug location information of a value.
     *
     * @param value The value to get the debug location for.
     * @return The debug location information.
     */
    DebugLocation GetValueDebugLocationInfo(const Value& value) const;
    
    /**
     * @brief Translates a code location from Cangjie positions to a debug location.
     *
     * @param begin The beginning position of the code.
     * @param end The ending position of the code.
     * @return The translated debug location.
     */
    DebugLocation TranslateLocation(const Cangjie::Position& begin, const Cangjie::Position& end) const;
    
    /**
     * @brief Translates a file location to a debug location.
     *
     * @param fileID The ID of the file.
     * @return The translated file location.
     */
    DebugLocation TranslateFileLocation(unsigned fileID) const;
    
    /**
     * @brief Translates a node location to a debug location.
     *
     * @param node The AST node.
     * @return The translated node location.
     */
    DebugLocation TranslateLocation(const AST::Node& node) const;
    
    /**
     * @brief Retrieves the operator location for a given expression.
     *
     * @tparam T The type of the expression.
     * @param expr The expression to get the operator location for.
     * @return The operator location.
     */
    template <typename T> DebugLocation GetOperatorLoc(const T& expr)
    {
        const std::string& opStr = TOKENS[static_cast<int>(expr.op)];
        Cangjie::Position begin;
        if constexpr (std::is_same_v<T, AST::AssignExpr>) {
            begin = expr.assignPos.IsZero() ? expr.begin : expr.assignPos;
        } else {
            begin = expr.operatorPos.IsZero() ? expr.begin : expr.operatorPos;
        }
        auto end = begin + Cangjie::Position(0, 0, static_cast<int>(opStr.size()));
        return TranslateLocation(begin, end);
    }

    /**
     * @brief Sets the generic function map.
     *
     * @param funcMap A map from generic function declarations to their specialized instances.
     */
    void SetGenericFuncMap(const std::unordered_map<const AST::FuncDecl*, std::vector<AST::FuncDecl*>>& funcMap)
    {
        genericFuncMap = funcMap;
    }
    
    // ===--------------------------------------------------------------------===//
    // Expression API
    // ===--------------------------------------------------------------------===//
    
    /**
     * @brief Creates and appends an expression of type TExpr.
     *
     * @tparam TExpr The type of the expression to create.
     * @param resultTy The result type of the expression.
     * @param args The arguments for the expression constructor.
     * @return A pointer to the created expression.
     */
    template <typename TExpr, typename... Args> TExpr* CreateAndAppendExpression(Type* resultTy, Args&&... args)
    {
        return Cangjie::CHIR::CreateAndAppendExpression<TExpr>(builder, resultTy, args...);
    }

    /**
     * @brief Creates and appends an expression with a specified debug location.
     *
     * @tparam TExpr The type of the expression to create.
     * @param loc The debug location for the expression.
     * @param resultTy The result type of the expression.
     * @param args The arguments for the expression constructor.
     * @return A pointer to the created expression.
     */
    template <typename TExpr, typename... Args>
    TExpr* CreateAndAppendExpression(const DebugLocation& loc, Type* resultTy, Args&&... args)
    {
        return Cangjie::CHIR::CreateAndAppendExpression<TExpr>(builder, loc, resultTy, args...);
    }
    
    /**
     * @brief Creates and appends an expression with specified debug locations for warnings and expression.
     *
     * @tparam TExpr The type of the expression to create.
     * @param locForWarning The debug location for potential warnings.
     * @param loc The debug location for the expression.
     * @param resultTy The result type of the expression.
     * @param args The arguments for the expression constructor.
     * @return A pointer to the created expression.
     */
    template <typename TExpr, typename... Args>
    TExpr* CreateAndAppendExpression(
        const DebugLocation& locForWarning, const DebugLocation& loc, Type* resultTy, Args&&... args)
    {
        return Cangjie::CHIR::CreateAndAppendExpression<TExpr>(builder, locForWarning, loc, resultTy, args...);
    }
    
    /**
     * @brief Creates and appends a constant expression.
     *
     * @tparam TLitVal The type of the literal value.
     * @param resultTy The result type of the expression.
     * @param parentBlock The block to which the expression will be appended.
     * @param args The arguments for the expression constructor.
     * @return A pointer to the created constant expression.
     */
    template <typename TLitVal, typename... Args>
    Constant* CreateAndAppendConstantExpression(Type* resultTy, Block& parentBlock, Args&&... args)
    {
        Constant* expr = builder.CreateConstantExpression<TLitVal>(resultTy, &parentBlock, args...);
        parentBlock.AppendExpression(expr);
        return expr;
    }
    
    /**
     * @brief Creates and appends a constant expression with a specified debug location.
     *
     * @tparam TLitVal The type of the literal value.
     * @param loc The debug location for the expression.
     * @param resultTy The result type of the expression.
     * @param parentBlock The block to which the expression will be appended.
     * @param args The arguments for the expression constructor.
     * @return A pointer to the created constant expression.
     */
    template <typename TLitVal, typename... Args>
    Constant* CreateAndAppendConstantExpression(
        const DebugLocation& loc, Type* resultTy, Block& parentBlock, Args&&... args)
    {
        auto expr = builder.CreateConstantExpression<TLitVal>(loc, resultTy, &parentBlock, args...);
        parentBlock.AppendExpression(expr);
        return expr;
    }
    
    /**
     * @brief Creates and appends a terminator.
     *
     * @tparam TExpr The type of the terminator to create.
     * @param args The arguments for the terminator constructor.
     * @return A pointer to the created terminator.
     */
    template <typename TExpr, typename... Args> TExpr* CreateAndAppendTerminator(Args&&... args)
    {
        return Cangjie::CHIR::CreateAndAppendTerminator<TExpr>(builder, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Creates and appends a terminator with a specified debug location.
     *
     * @tparam TExpr The type of the terminator to create.
     * @param loc The debug location for the terminator.
     * @param args The arguments for the terminator constructor.
     * @return A pointer to the created terminator.
     */
    template <typename TExpr, typename... Args>
    TExpr* CreateAndAppendTerminator(const DebugLocation& loc, Args&&... args)
    {
        return Cangjie::CHIR::CreateAndAppendTerminator<TExpr>(builder, loc, std::forward<Args>(args)...);
    }
    
    /**
     * @brief Retrieves the current block.
     *
     * @return A pointer to the current block.
     */
    Ptr<Block> GetCurrentBlock() const;
    
    /**
     * @brief Sets the current block.
     *
     * @param block The block to set as the current block.
     */
    void SetCurrentBlock(Block& block);
    
    /**
     * @brief Creates an empty global variable initialization function.
     *
     * @param mangledName The mangled name of the function.
     * @param identifier The identifier of the function.
     * @param rawMangledName The raw mangled name of the function.
     * @param pkgName The package name.
     * @param linkage The linkage type of the function.
     * @param loc The debug location for the function.
     * @param isConst The global var is const or not.
     * @return A pointer to the created function.
     */
    Ptr<Func> CreateEmptyGVInitFunc(const std::string& mangledName, const std::string& identifier,
        const std::string& rawMangledName, const std::string& pkgName, const Linkage& linkage,
        const DebugLocation& loc, bool isConst);
    
    /**
     * @brief Returns a dereferenced value if necessary.
     *
     * @param val The value to dereference.
     * @param loc The debug location for the operation.
     * @return A pointer to the dereferenced value.
     */
    Ptr<Value> GetDerefedValue(Ptr<Value> val, const DebugLocation& loc = INVALID_LOCATION);
    
    /**
     * @brief Flattens a variable declaration with a pattern.
     *
     * @param pattern The pattern used in the declaration.
     * @param target The target value to flatten.
     * @param isLocalPattern Indicates if the pattern is local.
     */
    void FlattenVarWithPatternDecl(const AST::Pattern& pattern, const Ptr<Value>& target, bool isLocalPattern);
    
    /**
     * @brief Translates a literal constant expression to a CHIR constant.
     *
     * @param expr The literal constant expression.
     * @param realTy The real type of the constant.
     * @param block The block to which the constant will be added.
     * @return A pointer to the translated constant.
     */
    Ptr<Constant> TranslateLitConstant(const AST::LitConstExpr& expr, AST::Ty& realTy, Ptr<Block> block);
    
    /**
     * @brief Translates a literal constant expression to a CHIR literal value.
     *
     * @param expr The literal constant expression.
     * @param realTy The real type of the constant.
     * @return A pointer to the translated literal value.
     */
    Ptr<LiteralValue> TranslateLitConstant(const AST::LitConstExpr& expr, AST::Ty& realTy);
    
    /**
     * @brief Creates annotation factory functions for a declaration.
     *
     * @param decl The declaration to create annotation factory functions for.
     * @param parent The custom type definition.
     * @return The annotation information.
     */
    AnnoInfo CreateAnnoFactoryFuncSig(const AST::Decl& decl, CustomTypeDef* parent);
    
    /**
     * @brief Translates the body of an annotation factory function.
     *
     * @param decl The declaration.
     * @param func The function to translate the body for.
     */
    void TranslateAnnoFactoryFuncBody(const AST::Decl& decl, Func& func);
    void TranslateAnnotationsArrayBody(const AST::Decl& decl, Func& func);
    std::vector<GlobalVar*> TranslateAnnotationsArraySig(const AST::ArrayLit& annos, const Func& func);
    GlobalVar* TranslateCustomAnnoInstanceSig(const AST::Expr& expr, const Func& func, size_t i);

    /**
     * @brief Creates annotation information for a function parameter.
     *
     * @param astParam The AST function parameter.
     * @param chirParam The CHIR parameter.
     * @param parent The custom type definition.
     */
    void CreateParamAnnotationInfo(const AST::FuncParam& astParam, Parameter& chirParam, CustomTypeDef& parent);

    /**
     * @brief Creates annotation information for a declaration.
     *
     * @tparam TValue The type of the value to create annotation info for.
     * @param decl The declaration.
     * @param value The value to create annotation info for.
     * @param parent The custom type definition. Can be null.
     */
    template <typename TValue>
    void CreateAnnotationInfo(const AST::Decl& decl, TValue& value, CustomTypeDef* parent)
    {
        value.SetAnnoInfo(CreateAnnoFactoryFuncSig(decl, parent));
    }

    /**
     * @brief Creates annotation factory functions for a function declaration.
     *
     * @param funcDecl The function declaration.
     * @param parent The custom type definition. Can be null.
     */
    void CreateAnnoFactoryFuncsForFuncDecl(const AST::FuncDecl& funcDecl, CustomTypeDef* parent);
    
    /**
     * @brief Merges the virtual table for a class definition.
     *
     * @param bd The CHIR builder.
     * @param classDef The class definition.
     */
    static void MergeVTableForClassDef(CHIRBuilder& bd, ClassDef& classDef);
    
    /**
     * @brief Wraps a mutable function.
     *
     * @param builder The CHIR builder.
     * @param customTypeDef The custom type definition.
     */
    static void WrapMutFunc(CHIRBuilder& builder, CustomTypeDef& customTypeDef);
    
    /**
     * @brief Creates a wrapper for a mutable function.
     *
     * @param builder The CHIR builder.
     * @param rawFunc The raw function to wrap.
     * @param curDef The current custom type definition.
     * @param srcClassTy The source class type.
     */
    static void CreateMutFuncWrapper(
        CHIRBuilder& builder, FuncBase* rawFunc, CustomTypeDef& curDef, ClassType& srcClassTy);
    
    /**
     * @brief Retrieves a wrapper function from a member access.
     *
     * @param thisType The type of 'this'.
     * @param funcName The name of the function.
     * @param instFuncType The instance function type.
     * @param isStatic Indicates if the function is static.
     * @param funcInstTypeArgs The function instance type arguments.
     * @return A pointer to the wrapper function.
     */
    Value* GetWrapperFuncFromMemberAccess(Type& thisType, const std::string funcName,
        FuncType& instFuncType, bool isStatic, std::vector<Type*>& funcInstTypeArgs);
    
    void SetCompileTimeValue(bool val)
    {
        isCompileTimeValue = val;
    }
    
    /**
     * @brief Retrieves the desugared expression from a node.
     *
     * @param node The node to desugar.
     * @return A pointer to the desugared expression.
     */
    static const Ptr<const AST::Node> GetDesugaredExpr(const AST::Node& node)
    {
        auto expr = DynamicCast<AST::Expr*>(&node);
        auto nodePtr = &node;
        while (expr != nullptr && expr->desugarExpr != nullptr) {
            nodePtr = expr->desugarExpr.get();
            expr = DynamicCast<AST::Expr*>(nodePtr);
        }
        return nodePtr;
    }

    /**
     * From raw func, this type and its parent type to get an specified mut wrapper func
     * Interface I<T> {
     *   mut func foo(x: T): T {}
     * }
     * struct S1 <: I<Int64> {
     * }
     *
     * We want to know foo's wrapper func from S1, we need to know
     * 1. rawFunc: here is foo
     * 2. subTy, which custom type wrapper func should in: here is S1
     * 3. parentTy, which custom type wrapper func derive from: here is I<Int64>
     * set these three items as key
     */
    class WrapperFuncContainer {
    public:
        void Emplace(Value* rawFunc, CustomTypeDef* subDef, Type* parentTy, FuncBase* wrapperFunc);
        FuncBase* GetWrapperFunc(Value* rawFunc, CustomTypeDef* subDef, Type* parentTy) const;
    private:
        bool TypeEqual(const Type& ty1, const Type& ty2) const;
        std::map<std::pair<Value*, CustomTypeDef*>, std::unordered_map<Type*, FuncBase*>> wrapperFuncs;
    };
    Translator Copy() const;
    /// Set the top level decl this Translator object is visiting. These apis are used to make sure local const funcs
    /// are translated iff they are being visited as top level decls.
    void SetTopLevel(const AST::Decl& decl);
    bool IsTopLevel(const AST::Decl& decl) const;

    /// common translation functions
    /// Translate AST expression \p node as the argument of another CHIR expression
    Value* TranslateExprArg(const AST::Node& node);
    /// T* can be implicitly converted to bool, delete this overload to avoid Type* converted to bool
    template <class T>
    Value* TranslateExprArg(const AST::Node& node, T*) = delete;
    /// Translate AST expression \p node as the argument of another CHIR expression, and convert it to target CHIR
    /// type \p targetTy.
    Value* TranslateExprArg(const AST::Node& node, Type& targetTy, bool deref = true);
    /// Translate AST expression \p node as the return value of another CHIR expression, and store it to the target
    /// location \p debugLoc if \ref loc is not null and the result value is not of Unit or Nothing type.
    /// \param debugLoc If present, use it as the debug location of the generated typecast/store, if any. If absent, use
    /// the location of \param location as the debug location.
    /// This function is typically used in
    /// translate a subblock of another expression as the return value
    ///     if (cond) { bl1 } else { bl2 }
    ///     auto condValue = TranslateExprArg(cond, *GetBoolTy());
    ///     auto tb = CreateBlock(), fb = CreateBlock();
    ///     auto loc = ifExpr->GetType()->IsUnitOrNothing() ? null : Allocate(RefType{ifExpr->GetType()});
    ///     (switch to tb) TranslateSubExprAsValue(bl1, loc);
    ///     (switch to fb) TranslateSubExprAsValue(bl2, loc);
    void TranslateSubExprToLoc(const AST::Node& node, Value* location);
    void TranslateSubExprToLoc(const AST::Node& node, Value* location, const DebugLocation& debugLoc);
    /// Translate a node and leave the value of it as the return value of another expression. This is typically used in
    /// translation of the last expression of an AST block.
    Ptr<Value> TranslateSubExprAsValue(const AST::Node& node);
    /// Translate AST expression \p node here (in \ref currentBlock), and discard the return value if any. This function
    /// is typically used when
    /// 1. translating the sub expressions excluding the last (because the last is used as the
    ///     return value of the block) of a AST block.
    /// 2. translating the initialiser of a discarded pattern
    void TranslateSubExprToDiscarded(const AST::Node& node);

    void CollectValueAnnotation(const AST::Decl& decl);
    void CollectTypeAnnotation(const AST::InheritableDecl& decl, const CustomTypeDef& cl);

private:
    friend class GlobalVarInitializer;
    // Used for add compileTimeVal tag for compile-time evaluation.
    bool isCompileTimeValue = false;
    CHIRBuilder& builder;
    CHIRType& chirTy;
    AST2CHIRNodeMap<Value> localValSymbolTable;
    AST2CHIRNodeMap<Value>& globalSymbolTable;
    const ElementList<Ptr<const AST::Decl>>& localConstVars;
    const ElementList<Ptr<const AST::FuncDecl>>& localConstFuncs;
    const AST::Decl* topLevelDecl{nullptr};
    /** Used for store side effect expressions and other expression which does not have associated valid ast. */
    AST2CHIRNodeMap<Value> exprValueTable;
    // Since property's getter and setter will share same annotation function, we need to cache the function name.
    std::unordered_map<Ptr<const AST::Decl>, std::string> annotationFuncMap;
    static std::unordered_map<std::string, Ptr<Func>> jAnnoFuncMap;
    std::vector<Ptr<BlockGroup>> blockGroupStack;
    Ptr<Block> currentBlock;
    Ptr<Value> delayExitSignal;
    // this map is used for check if jump expr is inside try catch scope.
    std::unordered_map<Ptr<const AST::Expr>, Ptr<CHIR::DebugLocation>> forInExprAST2DebugLocMap;
    // this map is used for set return value for forIn expr, gc-pool issue:Cangjie-manifest/issues/2442
    std::unordered_map<Ptr<Value>, Ptr<Value>> forInExprReturnMap;
    size_t lambdaWrapperIndex{0};

    const Cangjie::GlobalOptions& opts;
    const GenericInstantiationManager* gim;
    // Map for blocks inside looping control flow, it has conditionBlock, falseBlock.
    AST2CHIRNodeMap<std::pair<Ptr<Block>, Ptr<Block>>> terminatorSymbolTable;
    //                 generic func,         instantiated func
    std::unordered_map<const AST::FuncDecl*, std::vector<AST::FuncDecl*>> genericFuncMap;

    static WrapperFuncContainer mutWrapperMap;

    enum class ControlType : uint8_t {
        BREAK = 0,
        CONTINUE,
        RETURN,
        NORMAL,
        MAX_TYPES,
    };
    /**
     * Store the tryExprs outside current expression (Only for expressions inside tryBody).
     * Used for collecting all reachable try-catch exceptions for catch block.
     */
    std::vector<Ptr<const AST::TryExpr>> tryBodyContext;
    std::stack<Ptr<Block>> tryCatchContext;
    struct FinallyControlVal {
        Ptr<Block> finallyBlock;
        /**
         * Has fixed size of DEFAULT_CASE, and will indexed by ControlType before 'DEFAULT_CASE'
         * the pair of blocks is {the block before control flow, control flow's target block}.
         */
        std::vector<std::vector<std::pair<Ptr<Block>, Ptr<Block>>>> controlFlowBlocks;
        FinallyControlVal(Ptr<Block> finally)
            : finallyBlock(finally), controlFlowBlocks(static_cast<uint8_t>(ControlType::MAX_TYPES))
        {
        }
    };
    /**
     * Used for store finally entry block of current try expression.
     * Try-finally will inside control flow will be generated like:
     *   while () {
     *      controlVal: UInt8
     *      try {
     *           break  ==> disconnect blocks,
     *   store [the block where break happens, the block generated break] at the index of 'break'
     *           xxxx
     *           continue ==> disconnect blocks,
     *   store [the block where continue happens, the block generated continue] at the index of 'continue'
     *           ...
     *           return ==> disconnect blocks,
     *   store [the block where return happens, the block generated return] at the index of 'return'
     *       } finally.normal { finally body; goto endBlock}
     *       } finally.throw { finally body; raise exception }
     *       } finally.break { finally body; goto stored break block } -> may generate multiple times
     *       } finally.continue { finally body; goto stored break block }  -> may generate multiple times
     *       } finally.return { finally body; goto return }  -> may generate multiple times
     *   }
     */
    std::stack<FinallyControlVal> finallyContext;

    // Record nesting scope info, it is used for 1. debug info; 2. compare scope of try-finally and control flow expr.
    std::vector<int> scopeInfo{0};
    const IncreKind& increKind;
    std::vector<std::pair<const AST::Decl*, Func*>>& annoFactoryFuncs;
    std::unordered_map<Block*, Terminator*>& maybeUnreachable;
    bool isComputingAnnos{};
    std::vector<CHIR::Func*>& initFuncsForAnnoFactory;

    class ScopeContext {
    public:
        explicit ScopeContext(Translator& trans) : scopeInfo(trans.scopeInfo)
        {
            scopeInfo.emplace_back(-1);
        }
        ~ScopeContext()
        {
            scopeInfo.pop_back();
        }

        void ScopePlus()
        {
            ++scopeInfo.back();
        }

    private:
        std::vector<int>& scopeInfo;
    };

private:
    void SetSymbolTable(const AST::Node& node, Value& val, bool isLocal = true);
    Ptr<Value> GetSymbolTable(const AST::Node& node) const;
    Ptr<CustomTypeDef> GetNominalSymbolTable(const AST::Node& node);
    Ptr<Value> TypeCastOrBoxIfNeeded(Value& val, Type& expectedTy, const DebugLocation& loc, bool needCheck = true);
    Ptr<Block> GetBlockByAST(const AST::Block& block)
    {
        auto val = GetSymbolTable(block);
        CJC_ASSERT(val->IsBlock());
        return StaticCast<Block*>(val.get());
    }
    Block* CreateBlock()
    {
        return builder.CreateBlock(blockGroupStack.back());
    }
    void SetFuncBlockGroup(BlockGroup& group);
    bool OverloadableExprMayThrowException(const AST::OverloadableExpr& expr, const Type& leftValTy) const;
    void SetRawMangledNameForIncrementalCompile(const AST::FuncDecl& astFunc, Func& chirFunc) const;

    /** @brief Wrapped expression creation to handle both normal context and try-catch context.*/
    template <typename TExpr, typename... Args> Expression* TryCreate(Block* parent, Args&&... args)
    {
        if (tryCatchContext.empty()) {
            auto expr = CreateAndAppendExpression<CHIRNodeNormalT<TExpr>>(std::forward<Args>(args)..., parent);
            return expr;
        }
        auto errBB = tryCatchContext.top();
        auto sucBB = CreateBlock();
        auto ret =
            CreateAndAppendExpression<CHIRNodeExceptionT<TExpr>>(std::forward<Args>(args)..., sucBB, errBB, parent);
        currentBlock = sucBB;
        return ret;
    }

    /**
     * @brief Wrapped expression creation to handle both normal context and try-catch context.
     * For IntOp, typecast with overflow strategy.
     */
    template <typename TExpr, typename... Args>
    Expression* TryCreateWithOV(Block* parent, bool mayThrowE, OverflowStrategy ofs, Args&&... args)
    {
        if (tryCatchContext.empty() || !mayThrowE) {
            return CreateAndAppendExpression<CHIRNodeNormalT<TExpr>>(std::forward<Args>(args)..., ofs, parent);
        }
        auto errBB = tryCatchContext.top();
        auto sucBB = CreateBlock();
        auto ret =
            CreateAndAppendExpression<CHIRNodeExceptionT<TExpr>>(std::forward<Args>(args)..., sucBB, errBB, parent);
        currentBlock = sucBB;
        return ret;
    }

    std::vector<Type*> GetFuncInstArgs(const AST::CallExpr& expr);

    Expression* GenerateFuncCall(Value& callee, const FuncType* instantiedFuncTy,
        const std::vector<Type*> calleeInstTypeArgs, Type* thisTy,
        const std::vector<Value*>& args, DebugLocation loc);

    Expression* GenerateDynmaicDispatchFuncCall(const InstInvokeCalleeInfo& funcInfo, const std::vector<Value*>& args,
        Value* thisObj = nullptr, Value* thisRTTI = nullptr, DebugLocation loc = INVALID_LOCATION);

    InstInvokeCalleeInfo CreateVirFuncInvokeInfo(InstCalleeInfo& strInstFuncType,
        const std::vector<Ptr<Type>>& funcInstArgs, const AST::FuncDecl& resolvedFunction);

    CHIR::Type* GetExactParentType(Type& fuzzyParentType, const AST::FuncDecl& resolvedFunction, FuncType& funcType,
        std::vector<Type*>& funcInstTypeArgs, bool checkAbstractMethod, [[maybe_unused]] bool report = true);
    std::vector<VTableSearchRes> GetFuncIndexInVTable(
        Type& root, const FuncCallType& funcCallType, bool isStatic, [[maybe_unused]] bool report = true);

    // translate var decl
    Ptr<Value> TranslateLeftValueOfVarDecl(const AST::VarDecl& decl, bool rValueIsEmpty, bool isLocalVar);
    void StoreRValueToLValue(const AST::VarDecl& decl, Value& rval, Ptr<Value>& lval);
    Value* TranslateStaticAccessAsLambda(const AST::RefExpr& ref);
    void HandleVarWithVarPattern(const AST::VarPattern& pattern, const Ptr<Value>& initNode, bool isLocalPattern);
    void HandleVarWithTupleAndEnumPattern(const AST::Pattern& pattern,
        const std::vector<OwnedPtr<AST::Pattern>>& subPatterns, const Ptr<Value>& initNode, bool isLocalPattern);

    // translate classLike decl
    void TranslateClassLikeDecl(ClassDef& classDef, const AST::ClassLikeDecl& decl);
    /*
    ** Create a Function type for virtual function.
    */
    Ptr<FuncType> CreateVirtualFuncType(const AST::FuncDecl& decl);
    void AddMemberVarDecl(CustomTypeDef& def, const AST::VarDecl& decl);
    void TranslateClassLikeMemberFuncDecl(ClassDef& classDef, const AST::FuncDecl& decl);
    void AddMemberPropDecl(CustomTypeDef& def, const AST::PropDecl& decl);
    void TranslateAbstractMethod(ClassDef& classDef, const AST::FuncDecl& decl, bool hasBody);

    Ptr<Value> Visit(const AST::ArrayExpr& array);
    Ptr<Value> Visit(const AST::ArrayLit& array);
    Ptr<Value> Visit(const AST::AssignExpr& assign);
    Ptr<Value> Visit(const AST::BinaryExpr& binaryExpr);
    Ptr<Value> Visit(const AST::Block& b);
    Ptr<Value> Visit(const AST::CallExpr& callExpr);
    Ptr<Value> Visit(const AST::ClassDecl& decl);
    Ptr<Value> Visit(const AST::DoWhileExpr& doWhileExpr);
    Ptr<Value> Visit(const AST::EnumDecl& decl);
    Ptr<Value> Visit(const AST::ExtendDecl& decl);
    Ptr<Value> Visit(const AST::FuncArg& arg);
    Ptr<Value> Visit(const AST::FuncBody& funcBody);
    Ptr<Value> Visit(const AST::FuncDecl& func);
    Ptr<Value> Visit(const AST::IfExpr& ifExpr);
    Ptr<Value> Visit(const AST::InterfaceDecl& decl);
    Ptr<Value> Visit(const AST::JumpExpr& jumpExpr);
    Ptr<Value> Visit(const AST::LambdaExpr& lambdaExpr);
    Ptr<Value> Visit(const AST::ForInExpr& forInExpr);
    Ptr<Value> Visit(const AST::LitConstExpr& expr);
    Ptr<Value> Visit(const AST::MatchExpr& matchExpr);
    Ptr<Value> Visit(const AST::MemberAccess& member);
    Ptr<Value> Visit(const AST::ParenExpr& expr);
    Ptr<Value> Visit(const AST::PointerExpr& expr);
    Ptr<Value> Visit(const AST::RefExpr& refExpr);
    Ptr<Value> Visit(const AST::ReturnExpr& expr);
    Ptr<Value> Visit(const AST::SpawnExpr& spawnExpr);
    Ptr<Value> Visit(const AST::StructDecl& decl);
    Ptr<Value> Visit(const AST::SubscriptExpr& subscriptExpr);
    Ptr<Value> Visit(const AST::ThrowExpr& throwExpr);
    Ptr<Value> Visit(const AST::TryExpr& tryExpr);
    Ptr<Value> Visit(const AST::TupleLit& tuple);
    Ptr<Value> Visit(const AST::TypeConvExpr& typeConvExpr);
    Ptr<Value> Visit(const AST::UnaryExpr& unaryExpr);
    Ptr<Value> Visit(const AST::VarDecl& decl);
    Ptr<Value> Visit(const AST::VarWithPatternDecl& patternDecl);
    Ptr<Value> Visit(const AST::WhileExpr& whileExpr);
    Ptr<Value> Visit(const AST::Node& node) const
    {
        if (isComputingAnnos && Is<AST::IfAvailableExpr>(node)) {
            return nullptr;
        }
        CJC_ASSERT(false && "Should not reach here!");
        return nullptr;
    }

    // ===--------------------------------------------------------------------===//
    // Helper data structure for left-value translation
    // ===--------------------------------------------------------------------===//

    struct LeftValueInfo {
        Value* base;
        std::vector<uint64_t> path;

        LeftValueInfo(Value* base_, const std::vector<uint64_t>& path_) : base(base_), path(path_)
        {
        }
    };

    // ===--------------------------------------------------------------------===//
    // Helper function for RefExpr translation
    // ===--------------------------------------------------------------------===//
    LeftValueInfo TranslateThisOrSuperRefAsLeftValue(const AST::RefExpr& refExpr);
    // this should be an API in utils
    uint64_t GetVarMemberIndex(const AST::VarDecl& varDecl);
    LeftValueInfo TranslateClassMemberVarRefAsLeftValue(const AST::RefExpr& refExpr);
    LeftValueInfo TranslateStructMemberVarRefAsLeftValue(const AST::RefExpr& refExpr);
    LeftValueInfo TranslateEnumMemberVarRef(const AST::RefExpr& refExpr);
    LeftValueInfo TranslateVarRefAsLeftValue(const AST::RefExpr& refExpr);
    Value* TranslateThisOrSuperRef(const AST::RefExpr& refExpr);
    Value* TranslateVarRef(const AST::RefExpr& refExpr);
    Value* TranslateFuncRefInstArgs(const AST::RefExpr& refExpr, Value& originalFunc);
    Value* TranslateMemberFuncRef(const AST::RefExpr& refExpr);
    Value* TranslateFuncRef(const AST::RefExpr& refExpr);

    // ===--------------------------------------------------------------------===//
    // Helper function for AssignExpr translation
    // ===--------------------------------------------------------------------===//
    Value* TranslateVArrayAssign(const AST::AssignExpr& assign);
    Value* TranslateLeftValueInfo(const LeftValueInfo& lhs, const DebugLocation& loc);
    Value* TranslateCompoundAssign(const AST::AssignExpr& assign);
    Value* TranslateTrivialAssign(const AST::AssignExpr& assign);

    Func* GetCurrentFunc() const
    {
        CJC_ASSERT(!blockGroupStack.empty());
        return blockGroupStack.front()->GetOwnerFunc();
    }

    struct BindingConfig {
        bool hasInitial{false};
        bool createDebug{true};
        bool setSymbol{true};
    };
    // Translate function.
    void BindingFuncParam(const AST::FuncParamList& paramList, const BlockGroup& funcBody,
        const BindingConfig& cfg = {false, true, true});
    Ptr<Block> TranslateFuncBody(const AST::FuncBody& funcBody);
    Ptr<Value> TranslateInstanceMemberFunc(const AST::Decl& parent, const AST::FuncBody& funcBody);
    Ptr<Value> TranslateConstructorFunc(const AST::Decl& parent, const AST::FuncBody& funcBody);
    Ptr<Value> TranslateNestedFunc(const AST::FuncDecl& func);
    Translator SetupContextForLambda(const AST::Block& body);
    /** NOTE: This method must be called with new translator. */
    Ptr<Value> TranslateLambdaBody(Ptr<Lambda> lambda, const AST::FuncBody& funcBody, const BindingConfig& config);
    friend class TranslateCondCtrlExpr;
    friend class TranslateWhileExpr;

    // ========Methods used for translating ForInExpr=========
    Ptr<Value> GetOuterMostExpr();
    void InitializeDelayExitSignal(const DebugLocation& loc);
    Ptr<Value> InitializeCondVar(const DebugLocation& loc);
    ForIn* InitForInExprSkeleton(const AST::ForInExpr& forInExpr, Ptr<Value>& inductiveVar, Ptr<Value>& condVar);
    Ptr<Value> GenerateForInRetValLocation(const DebugLocation& loc);
    void UpdateDelayExitSignalInForInEnd(const ForIn& forIn);
    int64_t CalculateDelayExitLevelForBreak();
    int64_t CalculateDelayExitLevelForContinue();
    int64_t CalculateDelayExitLevelForReturn();
    void UpdateDelayExitSignal(int64_t level);
    Ptr<Value> GetOuterBlockGroupReturnValLocation();
    void TranslateForInCondControlFlow(Ptr<Value>& condVar);
    void TranslateForInBodyBlockGroup(const AST::ForInExpr& forInExpr);
    friend class TranslateForInExpr;

    // ========Methods used for translating ForInExpr with Range kind========
    Ptr<Value> TranslateForInRange(const AST::ForInExpr& forInExpr);
    void TranslateForInRangeLatchBlockGroup(const AST::Node& node);
    std::tuple<Block*, Block*, Value*> DelayExitIsNot0(const DebugLocation& loc);

    // ========Methods used for translating ForInExpr with String kind=======
    Ptr<Value> TranslateForInString(const AST::ForInExpr& forInExpr);
    void TranslateForInStringLatchBlockGroup(Ptr<Value>& inductiveVar);

    // ========Methods used for translating ForInExpr with Iter kind==========
    Ptr<Value> TranslateForInIter(const AST::ForInExpr& forInExpr);
    /// Make a Option::None value of option type \param optionType.
    Ptr<Value> MakeNone(Type& optionType, const DebugLocation& loc);
    Ptr<Value> TranslateForInIterCondition(Ptr<Value>& iterNextLocation, Ptr<AST::Ty>& astTy);
    void TranslateForInIterPattern(const AST::ForInExpr& forInExpr, Ptr<Value>& iterNextLocation);
    void TranslateForInIterLatchBlockGroup(const AST::MatchExpr& matchExpr, Ptr<Value>& iterNextLocation);
    // ========End methods used for translating ForInExpr=========

    // Short circuit helper function.
    Ptr<Value> TransShortCircuitAnd(
        const Ptr<Value> leftValue, const AST::Expr& rightExpr, const DebugLocation& loc, bool isImplicitAdd = false);
    Ptr<Value> TransShortCircuitOr(
        const Ptr<Value> leftValue, const AST::Expr& rightExpr, const DebugLocation& loc, bool isImplicitAdd = false);

    Ptr<Value> ProcessBinaryExpr(const AST::BinaryExpr& binaryExpr);
    // ====================call expr===============
    // ==================== global func or instance member func call===============
    Ptr<Value> TranslateIntrinsicCall(const AST::CallExpr& expr);
    Ptr<Value> TranslateCStringCtorCall(const AST::CallExpr& expr);
    Ptr<Value> TranslateForeignFuncCall(const AST::CallExpr& expr);

    Value* GenerateLoadIfNeccessary(Value& arg, bool isThis, bool isMut, bool isInOut, const DebugLocation& loc);
    bool IsOverflowOpCall(const AST::FuncDecl& func);
    Value* TranslateNonStaticMemberFuncCall(const AST::CallExpr& expr);
    Value* TranslateStaticMemberFuncCall(const AST::CallExpr& expr);
    Value* CreateGetRTTIWrapper(Value* value, Block* bl, const DebugLocation& loc);
    static bool IsInsideCFunc(const Block& bl);
    Value* TranslateMemberFuncCall(const AST::CallExpr& expr);
    Value* TranslateTrivialFuncCall(const AST::CallExpr& expr);

    Ptr<Value> TranslateNothingTypeCall(const AST::CallExpr& expr);
    Ptr<Value> TranslateGlobalOrInstanceMemberFuncCall(const AST::CallExpr& expr);
    Ptr<Value> ProcessCallExpr(const AST::CallExpr& expr);
    Ptr<AST::Expr> GetMapExpr(AST::Node& node) const;
    void ProcessMapExpr(AST::Node& originExpr, bool isSubScript = false);
    void PrintDevirtualizationMessage(const AST::CallExpr& expr, const std::string& nodeType);
    Ptr<Type> GetTypeOfInvokeStatic(const AST::Decl& funcDecl);
    Ptr<Type> GetMemberFuncCallerInstType(const AST::CallExpr& expr, bool needExactTy = true);
    std::pair<std::vector<Type*>, Type*> GetMemberFuncParamAndRetInstTypes(const AST::CallExpr& expr);
    // ==================== lambda func call===============
    Ptr<Value> TranslateFuncTypeValueCall(const AST::CallExpr& expr);
    // ==================== constructor call===============
    Ptr<Value> TranslateCFuncConstructorCall(const AST::CallExpr& expr);
    Ptr<Value> TranslateEnumCtorCall(const AST::CallExpr& expr);
    /// Translate common parts of struct/class ctor call of left value and right value versions.
    /// \returns translate this arg.
    std::pair<Value*, Type*> TranslateStructOrClassCtorCallCommon(const AST::CallExpr& expr);
    Value* TranslateStructOrClassCtorCall(const AST::CallExpr& expr);
    // ==================== func args===============
    static bool IsOptimizableTy(Ptr<AST::Ty> ty);
    static bool IsOptimizableEnumTy(Ptr<AST::Ty> ty);
    static uint64_t GetJumpablePatternVal(const AST::Pattern& pattern);
    bool CanOptimizeMatchToSwitch(const AST::MatchExpr& matchExpr);

    std::vector<Type*> TranslateASTTypes(const std::vector<Ptr<AST::Ty>>& genericInfos);
    bool HasNothingTypeArg(std::vector<Value*>& args) const;
    // ============= memberaccess expr ====================
    Ptr<Value> TranslateStaticTargetOrPackageMemberAccess(const AST::MemberAccess& member);
    Ptr<Value> TranslateInstanceMemberMemberAccess(const AST::MemberAccess& member);
    Ptr<Value> TranslateEnumMemberAccess(const AST::MemberAccess& member);
    Ptr<Value> TranslateVarMemberAccess(const AST::MemberAccess& member);
    Ptr<Value> TranslateFuncMemberAccess(const AST::MemberAccess& member);
    Ptr<Value> WrapFuncMemberByLambda(
        const AST::FuncDecl& funcDecl, const Position& pos, Ptr<Value> thisVal, Type* thisType,
        InstCalleeInfo& strInstFuncType, std::vector<Ptr<Type>>& funcInstArgs, bool isSuper = false);
    InstCalleeInfo GetCustomTypeFuncRef(const AST::RefExpr& expr, const AST::FuncDecl& funcDecl);
    InstCalleeInfo GetCustomTypeMemberAccessFuncRef(const AST::MemberAccess& expr);
    Ptr<Value> GetBaseFromMemberAccess(const AST::Expr& base);

    uint64_t GetFieldOffset(const AST::Decl& target) const;
    Ptr<Value> TransformThisType(Value& rawThis, Type& expectedTy, Lambda& curLambda);

    // ============= match expr ====================
    /** Entrance of translating match with selector. */
    void TranslateMatchWithSelector(const AST::MatchExpr& matchExpr, Ptr<Value> retVal);
    /** Entrance of translating match as optimized switch table. */
    void TranslateMatchAsTable(const AST::MatchExpr& matchExpr, Ptr<Value> retVal);
    /** Entrance of translating match without selector. */
    void TranslateConditionMatches(const AST::MatchExpr& matchExpr, Ptr<Value> retVal);
    bool CanOptimizeToSwitch(const AST::LetPatternDestructor& let) const;

    Block* TranslateMatchCaseBody(
        const AST::Block& caseBody, Ptr<Value> retVal, Ptr<Block> endBlock);
    /** Entrance of translating multiple outermost or-pattern case. */
    // return value -> {false block, true block}
    std::pair<Ptr<Block>, Ptr<Block>> TranslateOrPattern(
        const std::vector<OwnedPtr<AST::Pattern>>& patterns, Ptr<Value> selectorVal, const DebugLocation& originLoc);
    std::pair<Ptr<Block>, Ptr<Block>> TranslateConstantMultiOr(const std::vector<uint64_t> values, Ptr<Value> value);
    Ptr<Value> GetEnumIDValue(Ptr<AST::Ty> ty, Ptr<Value> selectorVal);
    /* Translate or patterns except constant and enum patterns. */
    std::pair<Ptr<Block>, Ptr<Block>> TranslateComplicatedOrPattern(
        const std::vector<OwnedPtr<AST::Pattern>>& patterns,
        const Ptr<Value> selectorVal, const DebugLocation& originLoc);
    /** Entrance of translating single outermost nesting case pattern. */
    // return value -> {false block, true block}
    std::pair<Ptr<Block>, Ptr<Block>> TranslateNestingCasePattern(const AST::Pattern& pattern,
        const Ptr<Value> selectorVal, const DebugLocation& originLoc,
        const SourceExpr& sourceExpr = SourceExpr::MATCH_EXPR);
    void TranslatePatternGuard(const AST::MatchCase& matchCase, Ptr<Block> falseBlock, Ptr<Block>& trueBlock);
    // ========= helper functions for translating each of nesting patterns ==========
    // NOTE: 'blocks' is pair of {final matched block, next condition block}
    // 'enum pattern''s element value should be generated in 'next condition block'.
    // 'var pattern''s typecast should be generated in 'final matched block'.
    Ptr<Value> DispatchingPattern(std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>>& queue,
        const std::pair<Ptr<Block>, Ptr<Block>>& blocks, const DebugLocation& originLoc);
    void CollectingSubPatterns(const Ptr<AST::Ty>& patternTy, const std::vector<OwnedPtr<AST::Pattern>>& patterns,
        const Ptr<Value> value, std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>>& queue, unsigned offset = 0);
    void HandleVarPattern(const AST::VarPattern& varPattern, const Ptr<Value> value, const Ptr<Block>& trueBlock);
    Ptr<Value> CastEnumValueToConstructorTupleType(Ptr<Value> enumValue, const AST::EnumPattern& enumPattern);
    Ptr<Value> HandleEnumPattern(const AST::EnumPattern& enumPattern, Ptr<Value> value,
        std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>>& queue, const Ptr<Block>& trueBlock,
        const DebugLocation& originLoc);
    Type* GetSelectorType(const AST::EnumTy& ty) const;
    Ptr<Value> HandleTypePattern(const AST::TypePattern& typePattern, Ptr<Value> value,
        std::queue<std::pair<Ptr<const AST::Pattern>, Ptr<Value>>>& queue);
    Ptr<Value> HandleConstPattern(
        const AST::ConstPattern& constPattern, Ptr<Value> value, const DebugLocation& originLoc);
    // ========= helper functions for translating match as table ==========
    void TranslateTrivialMatchAsTable(
        const AST::MatchExpr& match, Ptr<Value> selectorVal, Ptr<Value> retVal, size_t countOfPatterns);
    // Record relation of enum pattern and its matching case body's id.
    struct SecondSwitchInfo {
        const AST::EnumPattern& ep;
        size_t caseId = 0;
    };
    // NOTE: using map for fixed order of IR generation.
    struct EnumMatchInfo {
        // enum pattern index of the case direct pattern -> map<const pattern value, case info>
        std::map<uint64_t, std::map<uint64_t, std::vector<SecondSwitchInfo>>> indexToBodies;
        // enum pattern index of the case direct pattern -> the blocks to generate body for each type of enum
        std::map<uint64_t, Block*> firstSwitchBlocks;
        // enum pattern index of the case direct pattern -> wildcard cases's info
        std::map<uint64_t, std::vector<SecondSwitchInfo>> innerDefaultInfos;
    };
    void TranslateEnumPatternMatchAsTable(const AST::MatchExpr& match, Ptr<Value> enumVal, Ptr<Value> retVal);
    void CollectEnumPatternInfo(const AST::Pattern& pattern, EnumMatchInfo& info, size_t caseId);
    std::unordered_map<size_t, std::vector<Ptr<Block>>> TranslateSecondLevelAsTable(
        const EnumMatchInfo& info, Ptr<Value> enumVal, Ptr<Block> firstDefaultBlock);
    /**
     * Translate sub patterns for each case inside second level switch table.
     * @param endBlock [in]: last block when all sub-patterns are not matched.
     * @param tableBlock [in]: the block where to start generating remain sub-patterns.
     * @param enumVal [in]: match selector.
     * @param infos [in]: enum pattern info of current second level swith.
     * @param blockBranchInfos [out]: map to store the true blocks of current match case id.
     * eg: enum E { R |G(Int64) |B(Rune, Object) }
     *     match(sel) {
     *         case R | G(1) => 0
     *         case B('c', v: ToString) => v; 2
     *         case B('c', v: Object) => v; 3
     *         case B('a', v) => 3
     *         case _ => 4
     *     }
     *  For match case above, the first level switch generate 'multibranch sel.enumId, R.bb, B.bb, _.bb'
     *  The second level for 'B' generate "multibranch id, 'c'.bb, 'a'.bb"
     *  The @p tableBlock is "'a'.bb", then @p enumVal is the value of 'sel',
     *  @p infos contains case info of 'B('c', v: ToString)' and 'B('c', v: Object)',
     *  @p blockBranchInfos will has the keys '1' for 'B('c', v: ToString)', '2' for 'B('c', v: Object)'.
     */
    void TranslateSecondLevelTable(Ptr<Block> endBlock, const Ptr<Block> tableBlock, const Ptr<Value> enumVal,
        const std::vector<SecondSwitchInfo>& infos,
        std::unordered_map<size_t, std::vector<Ptr<Block>>>& blockBranchInfos);

    template <typename... Args>
    void CreateWrappedStore(const DebugLocation& loc, Ptr<Value> value, Ptr<Value> location, Args&&... args)
    {
        auto resultType = location->GetType();
        CJC_ASSERT(resultType->IsRef());
        resultType = StaticCast<RefType*>(resultType)->GetBaseType();
        CreateAndAppendExpression<Store>(loc, builder.GetUnitTy(), TypeCastOrBoxIfNeeded(*value, *resultType, loc),
            location, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void CreateWrappedStore(const Ptr<Value>& value, const Ptr<Value>& location, Args&&... args)
    {
        auto resultType = location->GetType();
        CJC_ASSERT(resultType->IsRef());
        resultType = StaticCast<RefType*>(resultType)->GetBaseType();
        CreateAndAppendExpression<Store>(
            builder.GetUnitTy(), TypeCastOrBoxIfNeeded(*value, *resultType, value->GetDebugLocation()),
            location, std::forward<Args>(args)...);
    }

    template <typename... Args> void CreateWrappedBranch(const SourceExpr& sourceExpr, Args&&... args)
    {
        auto expr = CreateAndAppendTerminator<Branch>(std::forward<Args>(args)...);
        expr->SetSourceExpr(sourceExpr);
    }

    Ptr<LocalVar> CreateGetElementRefWithPath(const DebugLocation& loc, Ptr<Value> lhsBase,
        const std::vector<uint64_t>& path, Ptr<Block> block, const CustomType& customType);

    Ptr<LocalVar> CreateWrappedGetElementRef(Ptr<Type> type, const DebugLocation& loc, Ptr<Type> resultTy,
        Ptr<Value> location, std::vector<uint64_t>& path)
    {
        if (auto enumType = DynamicCast<CHIR::EnumType*>(type); enumType && enumType->IsBoxed(builder)) {
            path.emplace_back(0U);
        }
        return CreateAndAppendExpression<GetElementRef>(loc, resultTy, location, path, currentBlock)->GetResult();
    }

    Ptr<LocalVar> CreateWrappedGetElementRef(
        Ptr<Type> type, Ptr<Type> resultTy, Ptr<Value> location, std::vector<uint64_t>& path)
    {
        return CreateWrappedGetElementRef(type, DebugLocation(), resultTy, location, path);
    }

    Ptr<LocalVar> CreateWrappedStoreElementRef(Ptr<Type> type, const DebugLocation& loc, Ptr<Value> val,
        Ptr<Value> location, std::vector<uint64_t>& path)
    {
        if (auto enumType = DynamicCast<CHIR::EnumType*>(type); enumType && enumType->IsBoxed(builder)) {
            path.emplace_back(0U);
        }
        return CreateAndAppendExpression<StoreElementRef>(loc, builder.GetUnitTy(), val, location, path, currentBlock)
            ->GetResult();
    }

    Ptr<LocalVar> CreateWrappedStoreElementRef(
        Ptr<Type> type, Ptr<Value> val, Ptr<Value> location, std::vector<uint64_t>& path)
    {
        return CreateWrappedStoreElementRef(type, DebugLocation(), val, location, path);
    }

    /// Create a typecast or some equivalent expressions that represent a typecast.
    TypeCast* CreateWrappedTypeCast(Type* ty, Value* operand, Block* parent)
    {
        return CreateWrappedTypeCast(INVALID_LOCATION, ty, operand, parent);
    }
    TypeCast* CreateWrappedTypeCast(const DebugLocation& loc, Type* ty, Value* operand, Block* parent)
    {
        return CreateAndAppendExpression<TypeCast>(loc, ty, operand, parent);
    }

    void HandleInitializedArgVal(const AST::CallExpr& ce, std::vector<Value*>& args);

    void TranslateThisObjectForNonStaticMemberFuncCall(
        const AST::CallExpr& expr, std::vector<Value*>& args, bool needsMutableThis);
    void TranslateTrivialArgsWithSugar(
        const AST::CallExpr& expr, std::vector<Value*>& args, const std::vector<Type*>& expectedArgTys);
    Value* TranslateTrivialArgWithNoSugar(const AST::FuncArg& arg, Type* expectedArgTy, const DebugLocation& loc);
    void TranslateTrivialArgsWithNoSugar(
        const AST::CallExpr& expr, std::vector<Value*>& args, const std::vector<Type*>& expectedArgTys);
    void TranslateTrivialArgs(
        const AST::CallExpr& expr, std::vector<Value*>& args, const std::vector<Type*>& expectedArgTys);

    Ptr<Value> GetCurrentThisObject(const AST::FuncDecl& resolved);
    Value* GetCurrentThisObjectByMemberAccess(const AST::MemberAccess& memAccess, const AST::FuncDecl& resolved,
        const DebugLocation& loc);

    // Translate SubscriptExpr
    Ptr<Value> TranslateTupleAccess(const AST::SubscriptExpr& subscriptExpr);
    Ptr<Value> TranslateVArrayAccess(const AST::SubscriptExpr& subscriptExpr);

    LeftValueInfo TranslateRefExprAsLeftValue(const AST::RefExpr& refExpr);
    LeftValueInfo TranslateMemberAccessAsLeftValue(const AST::MemberAccess& member);
    LeftValueInfo TranslateStructOrClassCtorCallAsLeftValue(const AST::CallExpr& expr);
    LeftValueInfo TranslateCallExprAsLeftValue(const AST::CallExpr& expr);
    // Translate the element ref of a compound assignment where the assignee is a member access.
    void TranslateCompoundAssignmentElementRef(const AST::MemberAccess& ma);
    LeftValueInfo TranslateExprAsLeftValue(const AST::Expr& expr);
    Value* GenerateLeftValue(const LeftValueInfo& leftValInfo, const DebugLocation& loc);

    // check annotation target
    friend class AnnotationTranslator;

    Ptr<Value> GetImplicitThisParam() const;

    // ArrayExpr
    // => VArrayBuilder ( size, item, null )
    Ptr<Value> InitVArrayByItem(const AST::ArrayExpr& vArray);
    // => VArrayBuilder ( size, null, initFunc )
    Ptr<Value> InitVArrayByLambda(const AST::ArrayExpr& vArray);

    Ptr<Value> InitArrayByCollection(const AST::ArrayExpr& array);
    Ptr<Value> InitArrayByItem(const AST::ArrayExpr& array);
    Ptr<Value> InitArrayByLambda(const AST::ArrayExpr& array);

    // ArrayLit - StructArray
    Ptr<Value> TranslateStructArray(const AST::ArrayLit& array);
    // ArrayLit - VArray
    Ptr<Value> TranslateVArray(const AST::ArrayLit& array);

    // TryExpr
    std::vector<ClassType*> GetExceptionsForTry(const AST::TryExpr& tryExpr);
    Ptr<Block> TranslateCatchBlocks(const AST::TryExpr& tryExpr, Ptr<Value> retVal, Ptr<Block> endBlock);
    void CreateFinallyExpr(bool hasFinally, Ptr<Block> endBlock);
    Ptr<Block> TranslateFinally(const AST::Block& finally, Ptr<Block> exceptionBlock);
    void TranslateFinallyNormalFlows(const AST::Block& finally, const FinallyControlVal& finallyControlInfo);
    void TranslateFinallyRethrowFlows(const AST::Block& finally);
    std::pair<Ptr<Block>, Ptr<Block>> TranslateExceptionPattern(const AST::Pattern& pattern, Ptr<Value> eVal);

    GenericType* TranslateCompleteGenericType(AST::GenericsTy& ty);

    // intrinsic special
    Type* HandleSpecialIntrinsic(IntrinsicKind intrinsicKind, std::vector<Value*>& args, Type* retTy);
};

static const std::unordered_map<Cangjie::TokenKind, ExprKind> op2ExprKind = {
    {Cangjie::TokenKind::ADD, ExprKind::ADD},
    {Cangjie::TokenKind::SUB, ExprKind::SUB},
    {Cangjie::TokenKind::MUL, ExprKind::MUL},
    {Cangjie::TokenKind::DIV, ExprKind::DIV},
    {Cangjie::TokenKind::MOD, ExprKind::MOD},
    {Cangjie::TokenKind::EXP, ExprKind::EXP},
    {Cangjie::TokenKind::BITAND, ExprKind::BITAND},
    {Cangjie::TokenKind::BITOR, ExprKind::BITOR},
    {Cangjie::TokenKind::BITXOR, ExprKind::BITXOR},
    {Cangjie::TokenKind::LSHIFT, ExprKind::LSHIFT},
    {Cangjie::TokenKind::RSHIFT, ExprKind::RSHIFT},
    {Cangjie::TokenKind::LT, ExprKind::LT},
    {Cangjie::TokenKind::GT, ExprKind::GT},
    {Cangjie::TokenKind::LE, ExprKind::LE},
    {Cangjie::TokenKind::GE, ExprKind::GE},
    {Cangjie::TokenKind::NOTEQ, ExprKind::NOTEQUAL},
    {Cangjie::TokenKind::EQUAL, ExprKind::EQUAL},
};

const static std::unordered_map<std::string, const std::unordered_map<std::string, IntrinsicKind>> packageMap = {
    {CORE_PACKAGE_NAME, coreIntrinsicMap},
    {SYNC_PACKAGE_NAME, cjnativeSyncIntrinsicMap},
    {OVERFLOW_PACKAGE_NAME, overflowIntrinsicMap},
    {RUNTIME_PACKAGE_NAME, runtimeIntrinsicMap},
    {REFLECT_PACKAGE_NAME, reflectIntrinsicMap},
    {MATH_PACKAGE_NAME, mathIntrinsicMap},
};

// Below are instrinsics without a source-level declaration, their delcaration should be dinamically generated
const static std::unordered_map<std::string, IntrinsicKind> headlessIntrinsics = {
    {GET_TYPE_FOR_TYPE_PARAMETER_NAME, IntrinsicKind::GET_TYPE_FOR_TYPE_PARAMETER},
    {IS_SUBTYPE_TYPES_NAME, IntrinsicKind::IS_SUBTYPE_TYPES},
};
} // namespace Cangjie::CHIR

#endif
