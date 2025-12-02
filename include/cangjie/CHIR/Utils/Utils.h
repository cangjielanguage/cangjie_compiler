// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares utils API for CHIR
 */

#ifndef CANGJIE_CHIR_UTILS_H
#define CANGJIE_CHIR_UTILS_H

#include <deque>
#include <map>

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"
#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/CHIRContext.h"
#include "cangjie/CHIR/Utils/DiagAdapter.h"
#include "cangjie/CHIR/IR/Type/CHIRType.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/Utils/CastingTemplate.h"

namespace Cangjie::CHIR {

const int LENGTH_OF_IF = 2;

using OptEffectCHIRMap = std::unordered_map<Ptr<Value>, std::unordered_set<Ptr<Value>>>;

// ==================================== for global var init ======================================

template <typename T> struct ElementList {
    std::vector<T> stableOrderValue;
    std::unordered_set<T> randomOrderValue;

    void AddElement(const T& element)
    {
        if (randomOrderValue.find(element) != randomOrderValue.end()) {
            return;
        }
        randomOrderValue.emplace(element);
        stableOrderValue.emplace_back(element);
    }
    void AddElements(const std::vector<T>& elements)
    {
        for (auto& element : elements) {
            AddElement(element);
        }
    }
    bool HasElement(const T& element) const
    {
        return randomOrderValue.find(element) != randomOrderValue.end();
    }
};

struct StaticInitInfo {
    Ptr<const AST::FuncDecl> staticInitFunc;
    Ptr<const AST::VarDecl> staticInitVar;
    std::vector<Ptr<const AST::VarDecl>> vars;

    StaticInitInfo(const Ptr<const AST::FuncDecl>& staticInitFunc, const Ptr<const AST::VarDecl>& staticInitVar,
        const std::vector<Ptr<const AST::VarDecl>>& vars)
        : staticInitFunc(staticInitFunc), staticInitVar(staticInitVar), vars(vars)
    {
    }
};
using StaticInitInfoMap = std::unordered_map<Ptr<const AST::Decl>, StaticInitInfo>;

using InitOrder = std::vector<std::pair<Ptr<const AST::File>, std::vector<Ptr<const AST::Decl>>>>;

template <typename T> struct DepMap {
    std::unordered_map<T, std::vector<T>> randomOrderValue;
    std::vector<std::pair<T, std::vector<T>>> stableOrderValue;

    void InitDeps(const T& root)
    {
        randomOrderValue.emplace(root, std::vector<T>{});
        stableOrderValue.emplace_back(std::make_pair(root, std::vector<T>{}));
    }
    void AddDep(const T& root, const T& dep)
    {
        auto& deps = randomOrderValue[root];
        if (std::find(deps.begin(), deps.end(), dep) != deps.end()) {
            return;
        }
        randomOrderValue[root].emplace_back(dep);
        for (auto& element : stableOrderValue) {
            if (element.first == root) {
                element.second.emplace_back(dep);
            }
        }
    }
    void AddDeps(const T& root, const std::vector<T>& deps)
    {
        CJC_ASSERT(randomOrderValue.find(root) == randomOrderValue.end());
        randomOrderValue.emplace(root, deps);
        stableOrderValue.emplace_back(std::make_pair(root, deps));
    }
    std::vector<T> GetDep(const T& root)
    {
        return randomOrderValue[root];
    }
};
using FileDepMap = DepMap<Ptr<const AST::File>>;
using DeclDepMap = DepMap<Ptr<const AST::Decl>>;

using VarDepMap = std::unordered_map<Ptr<const AST::Decl>, std::vector<Ptr<const AST::Decl>>>;

inline const std::string GV_INIT_WILDCARD_PATTERN = "wildcard_pattern_";
inline const std::string GV_PKG_INIT_ONCE_FLAG = "$has_applied_pkg_init_func";

// ==================================== for function matching ====================================
inline const std::string ANY_TYPE = "ANY_TYPE";
inline const std::string NOT_CARE = "NOT_CARE";

struct FuncInfo {
    const std::string funcName;
    const std::string parentTy;
    const std::vector<std::string> params;
    const std::string returnTy;
    const std::string pkgName;
    FuncInfo(const std::string& name, const std::string& parent, const std::vector<std::string>& params,
        const std::string& returnTy, const std::string& pkgName) noexcept
        : funcName(name), parentTy(parent), params(params), returnTy(returnTy), pkgName(pkgName)
    {
    }
    bool operator==(const FuncInfo& rhs) const
    {
        return (funcName == rhs.funcName && parentTy == rhs.parentTy && params == rhs.params &&
            returnTy == rhs.returnTy && pkgName == rhs.pkgName);
    }
};

/**
 * @brief Checks if a function matches the expected function information.
 *
 * @param func The function to check.
 * @param funcInfo The expected function information.
 * @return True if the function matches the expected information, false otherwise.
 */
bool IsExpectedFunction(const FuncBase& func, const FuncInfo& funcInfo);

/**
 * @brief Prints optimization information for an expression.
 *
 * @param e The expression to print information for.
 * @param debug A flag indicating whether to print debug information.
 * @param optName The name of the optimization.
 */
void PrintOptInfo(const Expression& e, bool debug, const std::string& optName);

// ==================================== for AST2CHIR.0 ====================================

/**
 * @brief Determines the kind of function from an AST function declaration.
 *
 * @param func The AST function declaration.
 * @return The kind of function.
 */
FuncKind GetFuncKindFromAST(const AST::FuncDecl& func);

/**
 * @brief Checks if a function is a virtual function.
 *
 * @param funcDecl The function declaration to check.
 * @return True if the function is virtual, false otherwise.
 */
bool IsVirtualFunction(const FuncBase& funcDecl);

/**
 * @brief Checks if a function is a static method in an interface.
 *
 * @param func The function to check.
 * @return True if the function is a static method in an interface, false otherwise.
 */
bool IsInterfaceStaticMethod(const Func& func);

/**
 * @brief Checks if a function is semantically abstract and an instance.
 *
 * @param func The AST function declaration to check.
 * @return True if the function is semantically abstract and an instance, false otherwise.
 */
inline bool IsSemanticAbstractInstance(const AST::FuncDecl& func)
{
    return !func.TestAttr(AST::Attribute::STATIC) &&
        (func.TestAttr(AST::Attribute::ABSTRACT) ||
            (func.outerDecl && func.outerDecl->astKind == AST::ASTKind::INTERFACE_DECL));
}

/**
 * @brief Performs a topological sort on a set of blocks starting from the entry block.
 *
 * @param entrybb The entry block to start the sort.
 * @return A deque of blocks in topological order.
 */
std::deque<Block*> TopologicalSort(Block* entrybb);

/**
 * @brief Adds expressions to the global initialization function.
 *
 * @param initFunc The global initialization function.
 * @param insertExpr The expressions to add to the initialization function.
 */
void AddExpressionsToGlobalInitFunc(const Func& initFunc, const std::vector<Expression*>& insertExpr);

/**
 * @brief Retrieves static member variables from a declaration.
 *
 * @param decl The declaration to retrieve static member variables from.
 * @return A vector of pointers to static member variables.
 */
std::vector<Ptr<AST::VarDecl>> GetStaticMemberVars(const AST::InheritableDecl& decl);

/**
 * @brief Retrieves non-static member variables from a declaration.
 *
 * @param decl The declaration to retrieve non-static member variables from.
 * @return A vector of pointers to non-static member variables.
 */
std::vector<Ptr<AST::VarDecl>> GetNonStaticMemberVars(const AST::InheritableDecl& decl);

/**
 * @brief Retrieves non-static superclass member variables from a class-like declaration.
 *
 * @param classLikeDecl The class-like declaration to retrieve non-static superclass member variables from.
 * @return A vector of pointers to non-static superclass member variables.
 */
std::vector<Ptr<AST::VarDecl>> GetNonStaticSuperMemberVars(const AST::ClassLikeDecl& classLikeDecl);

/**
 * @brief Checks if an enum declaration is an enum option.
 *
 * @param enumDecl The enum declaration to check.
 * @return True if the enum declaration is an enum option, false otherwise.
 */
inline bool IsEnumOption(const AST::EnumDecl& enumDecl)
{
    return enumDecl.identifier == STD_LIB_OPTION && enumDecl.fullPackageName == CORE_PACKAGE_NAME;
}

/**
 * @brief Constructs the output path based on the output directory and package name.
 *
 * @param output The output directory or file path.
 * @param fullPkgName The full package name.
 * @param suffix The suffix to append to the output path.
 * @return The constructed output path.
 */
inline std::string GetOutputPath(
    const std::string& output, const std::string& fullPkgName, const std::string suffix = "_trans/")
{
    if (Cangjie::FileUtil::IsDir(output)) {
        return Cangjie::FileUtil::JoinPath(output, fullPkgName) + suffix;
    } else {
        return Cangjie::FileUtil::GetFileBase(output) + suffix;
    }
}

/**
 * @brief Retrieves the debug position for a given expression.
 *
 * @tparam T The type of the expression.
 * @param expr The expression to retrieve the debug position from.
 * @return A pair containing a boolean and a range representing the debug position.
 */
template <typename T> std::pair<bool, Cangjie::Range> GetDebugPos(const T& expr)
{
    auto warningPos = expr.template Get<DebugLocationInfoForWarning>();
    auto warningRange = ToRangeIfNotZero(warningPos);
    if (warningRange.first) {
        return warningRange;
    }
    auto& pos = expr.GetDebugLocation();
    auto range = ToRangeIfNotZero(pos);
    return range;
}

/**
 * @brief Checks if a position is in a different package.
 *
 * @param pos The position to check.
 * @param currentPackage The current package name.
 * @param diag The diagnostic adapter.
 * @return True if the position is in a different package, false otherwise.
 */
bool IsCrossPackage(const Cangjie::Position& pos, const std::string& currentPackage, DiagAdapter& diag);

/**
 * @brief Sets a value to skip print warnings.
 *
 * @param value The value to set the skip print warning flag for.
 */
inline void SetSkipPrintWarning(Ptr<Value> value)
{
    value->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
}

inline void MergeEffectMap(const OptEffectCHIRMap& from, OptEffectCHIRMap& to)
{
    for (auto fromIt : from) {
        auto toIt = to.find(fromIt.first);
        if (toIt == to.end()) {
            to.emplace(fromIt);
        } else {
            toIt->second.merge(fromIt.second);
        }
    }
}

/**
 * @brief Checks if a type has a "nothing" type.
 *
 * @param type The type to check.
 * @return True if the type has a "nothing" type, false otherwise.
 */
bool HasNothingType(Type& type);

/**
 * @brief Replaces uses of a function with a wrapper function.
 *
 * @param curFunc The current function.
 * @param apply The apply to replace.
 * @param wrapperFunc The wrapper function.
 * @param isForeign A flag indicating if the replacement is foreign.
 */
void ReplaceUsesWithWrapper(Value& curFunc, const Apply* apply, Value& wrapperFunc, bool isForeign = false);

/**
 * @brief Checks if a block group is nested within another block group.
 *
 * @param blockGroup The block group to check.
 * @param scope The scope block group.
 * @return True if the block group is nested within the scope, false otherwise.
 */
bool IsNestedBlockOf(const BlockGroup* blockGroup, const BlockGroup* scope);

/**
 * @brief Creates a new type with the given arguments.
 *
 * @param oldType The old type to base the new type on.
 * @param newArgs The new type arguments.
 * @param builder The CHIR builder.
 * @return The new type with the given arguments.
 */
Type* CreateNewTypeWithArgs(Type& oldType, const std::vector<Type*>& newArgs, CHIRBuilder& builder);

/**
 * @brief Replaces raw generic argument types with new types.
 *
 * @param type The type to replace.
 * @param replaceTable The mapping of generic types to new types.
 * @param builder The CHIR builder.
 * @return The type with replaced raw generic argument types.
 */
Type* ReplaceRawGenericArgType(
    Type& type, const std::unordered_map<const GenericType*, Type*>& replaceTable, CHIRBuilder& builder);

/**
 * @brief Replaces a type with a concrete type.
 *
 * @param type The type to replace.
 * @param concreteType The concrete type to replace with.
 * @param builder The CHIR builder.
 * @return The type replaced with the concrete type.
 */
Type* ReplaceThisTypeToConcreteType(Type& type, Type& concreteType, CHIRBuilder& builder);

/**
 * @brief Converts a real function type to a virtual function type.
 *
 * @param type The real function type to convert.
 * @param builder The CHIR builder.
 * @return The virtual function type.
 */
FuncType* ConvertRealFuncTypeToVirtualFuncType(const FuncType& type, CHIRBuilder& builder);

/**
 * @brief Performs a type cast or boxes a value if needed.
 *
 * @param val The value to cast or box.
 * @param expectedTy The expected type.
 * @param builder The CHIR builder.
 * @param parentBlock The parent block.
 * @param loc The debug location.
 * @param needCheck A flag indicating if a check is needed.
 * @return The value after type casting or boxing if needed.
 */
Ptr<Value> TypeCastOrBoxIfNeeded(Value& val, Type& expectedTy, CHIRBuilder& builder, Block& parentBlock,
    const DebugLocation& loc = INVALID_LOCATION, bool needCheck = true);

/**
 * @brief Creates and appends an expression to the builder.
 *
 * @tparam TExpr The type of the expression.
 * @tparam Args The argument types for the expression constructor.
 * @param builder The CHIR builder.
 * @param args The arguments for the expression constructor.
 * @return The created expression.
 */
template <typename TExpr, typename... Args> TExpr* CreateAndAppendExpression(CHIRBuilder& builder, Args&&... args)
{
    auto expr = builder.CreateExpression<TExpr>(args...);
    expr->GetParentBlock()->AppendExpression(expr);
    return expr;
}

/**
 * @brief Creates and appends a terminator to the builder.
 *
 * @tparam TExpr The type of the terminator.
 * @tparam Args The argument types for the terminator constructor.
 * @param builder The CHIR builder.
 * @param args The arguments for the terminator constructor.
 * @return The created terminator.
 */
template <typename TExpr, typename... Args> TExpr* CreateAndAppendTerminator(CHIRBuilder& builder, Args&&... args)
{
    auto expr = builder.CreateTerminator<TExpr>(args...);
    expr->GetParentBlock()->AppendExpression(expr);
    return expr;
}

/**
 * @brief Checks if a function is a static initializer.
 *
 * @param func The function to check.
 * @return True if the function is a static initializer, false otherwise.
 */
bool IsStaticInit(const AST::FuncDecl& func);
bool IsStaticInit(const FuncBase& func);

/**
 * @brief Checks if an expression is a super or this call.
 *
 * @param expr The expression to check.
 * @return True if the expression is a super or this call, false otherwise.
 */
bool IsSuperOrThisCall(const AST::CallExpr& expr);

/**
 * @brief Retrieves the instance map from the current definition to the current type.
 *
 * @param curType The current type definition.
 * @return The instance map from the current definition to the current type.
 */
std::unordered_map<const GenericType*, Type*> GetInstMapFromCurDefToCurType(const CustomType& curType);

std::unordered_map<const GenericType*, Type*> GetInstMapFromCurDefAndExDefToCurType(const CustomType& curType);

/**
 * @brief Retrieves the instance map from a custom type definition and its parent.
 *
 * @param def The custom type definition.
 * @param instMap The instance map to populate.
 * @param builder The CHIR builder.
 */
void GetInstMapFromCustomDefAndParent(
    const CustomTypeDef& def, std::unordered_map<const GenericType*, CHIR::Type*>& instMap, CHIRBuilder& builder);

/**
 * @brief Retrieves all instantiated parent types for a given class type.
 *
 * @param cur The current class type.
 * @param builder The CHIR builder.
 * @param parents The vector to store the parent types.
 */
void GetAllInstantiatedParentType(ClassType& cur, CHIRBuilder& builder, std::vector<ClassType*>& parents,
    std::set<std::pair<const Type*, const Type*>>* visited = nullptr);

/**
 * @brief Creates a box type reference for a given base type.
 *
 * @param baseTy The base type to create a box type reference for.
 * @param builder The CHIR builder.
 * @return The created box type reference.
 */
Type* CreateBoxTypeRef(Type& baseTy, CHIRBuilder& builder);

class GenericTypeConvertor {
public:
    GenericTypeConvertor(const std::unordered_map<const GenericType*, Type*>& instMap, CHIRBuilder& builder)
        : instMap(instMap), builder(builder) {}
    Type* ConvertToInstantiatedType(Type& type);

private:
    const std::unordered_map<const GenericType*, Type*>& instMap;
    CHIRBuilder& builder;
};

/**
 * @brief Converts function parameters and return type using a convertor function.
 *
 * @param input The input function type to convert.
 * @param convertor The function to convert types.
 * @param builder The CHIR builder.
 * @return The converted function type.
 */
FuncType* ConvertFuncParamsAndRetType(FuncType& input, ConvertTypeFunc& convertor, CHIRBuilder& builder);

/**
 * @brief Retrieves the types declared in an inner definition.
 *
 * @param innerDef The inner definition to retrieve types from.
 * @return A vector of types declared in the inner definition.
 */
std::vector<Type*> GetOutDefDeclaredTypes(const Value& innerDef);

/**
 * @brief Checks if a parent definition is from an extend.
 *
 * @param cur The current custom type definition.
 * @param parent The parent class definition.
 * @return True if the parent definition is from an extend, false otherwise.
 */
bool ParentDefIsFromExtend(const CustomTypeDef& cur, const ClassDef& parent);

/**
 * @brief Visits function blocks in topological sort order.
 *
 * @param funcBody The function body to visit blocks for.
 * @param preVisit The function to call before visiting an expression.
 */
void VisitFuncBlocksInTopoSort(const BlockGroup& funcBody, std::function<VisitResult(Expression&)> preVisit);

/**
 * @brief Retrieves the function type from an auto environment base definition.
 *
 * @param autoEnvBaseDef The auto environment base definition.
 * @return A pair containing the name and function type.
 */
std::pair<std::string, FuncType*> GetFuncTypeFromAutoEnvBaseDef(const ClassDef& autoEnvBaseDef);

/**
 * @brief Retrieves the function type without the 'this' pointer from an auto environment base type.
 *
 * @param autoEnvBaseType The auto environment base type.
 * @return A pair containing a vector of parameter types and the return type.
 */
std::pair<std::vector<Type*>, Type*> GetFuncTypeWithoutThisPtrFromAutoEnvBaseType(const ClassType& autoEnvBaseType);

/**
 * @brief Retrieves the method index in an auto environment object.
 *
 * @param methodName The name of the method to find the index for.
 * @return The index of the method in the auto environment object.
 */
size_t GetMethodIdxInAutoEnvObject(const std::string& methodName);

/**
 * @brief Retrieves the selector type from an enum declaration.
 *
 * @param decl The enum declaration to retrieve the selector type from.
 * @return The selector type.
 */
Type::TypeKind GetSelectorType(const AST::EnumDecl& decl);

/**
 * @brief Retrieves the selector type from an enum definition.
 *
 * @param def The enum definition to retrieve the selector type from.
 * @return The selector type.
 */
Type::TypeKind GetSelectorType(const EnumDef& def);

/**
 * @brief Checks if a type can be used as a valid enum selector type.
 *
 * @param type The type to check.
 * @return True if the type can be used as a valid enum selector type, false otherwise.
 */
bool IsEnumSelectorType(const Type& type);

/**
 * @brief Retrieves the extended interface definitions from a custom type definition.
 *
 * @param def The custom type definition to retrieve extended interface definitions from.
 * @return A vector of pointers to the extended interface definitions.
 */
std::vector<ClassDef*> GetExtendedInterfaceDefs(const CustomTypeDef& def);

/**
 * @brief Checks if a custom type definition matches the expected package and source code name.
 *
 * @param def The custom type definition to check.
 * @param packageName The expected package name.
 * @param defSrcCodeName The expected source code name.
 * @return True if the custom type definition matches the expected package and source code name, false otherwise.
 */
bool CheckCustomTypeDefIsExpected(
    const CustomTypeDef& def, const std::string& packageName, const std::string& defSrcCodeName);

/**
 * @brief Checks if a custom type definition is the core 'Any' type.
 *
 * @param def The custom type definition to check.
 * @return True if the custom type definition is the core 'Any' type, false otherwise.
 */
bool IsCoreAny(const CustomTypeDef& def);

/**
 * @brief Checks if a custom type definition is the core 'Object' type.
 *
 * @param def The custom type definition to check.
 * @return True if the custom type definition is the core 'Object' type, false otherwise.
 */
bool IsCoreObject(const CustomTypeDef& def);

/**
 * @brief Checks if a custom type definition is the core 'Option' type.
 *
 * @param def The custom type definition to check.
 * @return True if the custom type definition is the core 'Option' type, false otherwise.
 */
bool IsCoreOption(const CustomTypeDef& def);

/**
 * @brief Checks if a custom type definition is the core 'Future' type.
 *
 * @param def The custom type definition to check.
 * @return True if the custom type definition is the core 'Future' type, false otherwise.
 */
bool IsCoreFuture(const CustomTypeDef& def);

/**
 * @brief Checks if a class definition is a closure conversion environment class.
 *
 * @param def The class definition to check.
 * @return True if the class definition is a closure conversion environment class, false otherwise.
 */
bool IsClosureConversionEnvClass(const ClassDef& def);

/**
 * @brief Checks if a class definition is a captured class.
 *
 * @param def The class definition to check.
 * @return True if the class definition is a captured class, false otherwise.
 */
bool IsCapturedClass(const ClassDef& def);

/**
 * @brief Retrieves the parent function of a given value.
 *
 * @param value The value to retrieve the parent function from.
 * @return The parent function of the given value.
 */
Func* GetTopLevelFunc(const Value& value);

/**
 * @brief Retrieves the visible generic types for a given value.
 *  class A<T1, T2> {
 *      func foo<T3, T4>() {
 *          func goo1<T5, T6>() {
 *              func goo2<T7, T8>() {
 *                  value --> visiable generic types' order is {T1, T2, T3, T4, T5, T6, T7, T8}
 *              }
 *          }
 *      }
 *  }
 *
 * @param value The value to retrieve visible generic types for.
 * @return A vector of pointers to the visible generic types.
 */
std::vector<GenericType*> GetVisiableGenericTypes(const Value& value);

/**
 * @brief Retrieves the function parameters from a function body.
 *
 * @param funcBody The function body to retrieve parameters from.
 * @return A vector of pointers to the function parameters.
 */
const std::vector<Parameter*>& GetFuncParams(const BlockGroup& funcBody);

/**
 * @brief Retrieves the return value from a function body.
 *
 * @param funcBody The function body to retrieve the return value from.
 * @return The return value of the function body.
 */
LocalVar* GetReturnValue(const BlockGroup& funcBody);

/**
 * @brief Retrieves the function type from a function body.
 *
 * @param funcBody The function body to retrieve the function type from.
 * @return The function type of the function body.
 */
FuncType* GetFuncType(const BlockGroup& funcBody);

/**
 * @brief Checks if a value is a struct or extension method.
 *
 * @param value The value to check.
 * @return True if the value is a struct or extension method, false otherwise.
 */
bool IsStructOrExtendMethod(const Value& value);

/**
 * @brief Checks if a value is a constructor.
 *
 * @param value The value to check.
 * @return True if the value is a constructor, false otherwise.
 */
bool IsConstructor(const Value& value);

/**
 * @brief get original value before cast.
 * @param expr get original value if expr is cast.
 * @return value before cast.
 */
Value* GetCastOriginalTarget(const Expression& expr);

bool IsInstanceVarInit(const Value& value);

std::vector<ClassType*> GetSuperTypesRecusively(Type& subType, CHIRBuilder& builder);

Type* GetInstParentCustomTypeForApplyCallee(const Apply& expr, CHIRBuilder& builder);
Type* GetInstParentCustomTypeForAweCallee(const ApplyWithException& expr, CHIRBuilder& builder);

std::vector<VTableSearchRes> GetFuncIndexInVTable(
    Type& root, const FuncCallType& funcCallType, CHIRBuilder& builder);

bool ParamTypeIsEquivalent(const Type& paramType, const Type& argType);

/**
 * @brief the input type may not have vtable, but the return type must have vtable.
 * because CPointer<xxx>'s vtable is store in CPointer<Unit>
 *
 * @param type given type.
 * @return type with vtable.
 */
BuiltinType* GetBuiltinTypeWithVTable(BuiltinType& type, CHIRBuilder& builder);

/**
 * @brief try to get instantiated subtype from generic subtype and instantiated parent type.
 *
 * @param genericSubType generic subtype.
 * @param instParentType instantiated parent type.
 * @param builder CHIR builder.
 * @return instantiated subtype when
 * 1. `genericSubType` and `instParentType` are both CustomType and one is generic related, the other is instantiated
 * type.
 * 2. `genericSubType` and `instParentType` have parent-child relationship, and `genericSubType` is generic related sub
 * type, `instParentType` is instantiated parent type. return `genericsSubType` directly otherwise.
 */
Type* GetInstSubType(Type& genericSubType, const ClassType& instParentType, CHIRBuilder& builder);

bool VirMethodTypeIsMatched(const FuncType& type1, const FuncType& type2);

bool VirMethodParamTypeIsMatched(const Type& type1, const Type& type2);

bool VirMethodReturnTypeIsMatched(const Type& type1, const Type& type2);

Type* RemoveBoxTypeShellIfNeed(Type& type);

Type* CreateBoxRefTypeIfNeed(Type& baseTy, CHIRBuilder& builder);

/**
 * @brief try to get instantiated parent type from instantiated subtype and generic parent type.
 *
 * @param instSubType instantiated subtype.
 * @param genericParentType generic parent type.
 * @param builder CHIR builder.
 * @return instantiated parent type when `instSubType` and `genericParentType` have parent-child relationship.
 */
Type* GetInstParentType(Type& instSubType, Type& genericParentType, CHIRBuilder& builder);
} // namespace Cangjie::CHIR
#endif
