// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_AST2CHIR_UTILS_H
#define CANGJIE_CHIR_AST2CHIR_UTILS_H

#include "cangjie/AST/Node.h"
#include "cangjie/Basic/Position.h"
#include "cangjie/CHIR/CHIRContext.h"
#include "cangjie/CHIR/Type/CHIRType.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/Option/Option.h"

namespace Cangjie {
namespace CHIR {

/**
 * @brief Translates the function's generic upper bounds.
 *
 * @param chirTy The CHIR type to be translated.
 * @param func The function declaration containing generic information.
 */
void TranslateFunctionGenericUpperBounds(CHIRType& chirTy, const AST::FuncDecl& func);

/**
 * @brief Adjusts the function type based on the function declaration.
 *
 * @param funcType The function type to be adjusted.
 * @param funcDecl The function declaration used for adjustment.
 * @param builder The CHIR builder used for type construction.
 * @param chirType The CHIR type associated with the function.
 * @return Adjusted function type.
 */
FuncType* AdjustFuncType(FuncType& funcType, const AST::FuncDecl& funcDecl, CHIRBuilder& builder, CHIRType& chirType);

/**
 * @brief Retrieves the debug location of an AST declaration.
 *
 * @param cctx The CHIR context.
 * @param decl The AST declaration.
 * @return Debug location of the variable.
 */
DebugLocation GetDeclLoc(const CHIRContext& cctx, const AST::Decl& decl);

/**
 * @brief Translates a code location without considering the scope.
 *
 * @param context The CHIR context.
 * @param beginPos The beginning position of the code.
 * @param endPos The ending position of the code.
 * @return Translated debug location.
 */
DebugLocation TranslateLocationWithoutScope(
    const CHIRContext& context, const Cangjie::Position& beginPos, const Cangjie::Position& endPos);

/**
 * @brief Retrieves the generic parameter types.
 *
 * @param decl The declaration containing generic parameters.
 * @param chirType The CHIR type associated with generics.
 * @return Vector of generic types.
 */
std::vector<GenericType*> GetGenericParamType(const AST::Decl& decl, CHIRType& chirType);

/**
 * @brief Retrieves the name of the defined package.
 *
 * @param funcDecl The function declaration used to find the package name.
 * @return Name of the defined package.
 */
std::string GetNameOfDefinedPackage(const AST::FuncDecl& funcDecl);

/**
 * @brief Builds attribute information from an attribute pack.
 *
 * @param attr The attribute pack.
 * @return Attribute information.
 */
AttributeInfo BuildAttr(const AST::AttributePack& attr);

/**
 * @brief Builds attribute information for a variable declaration.
 *
 * @param decl The variable declaration.
 * @return Attribute information for the variable.
 */
AttributeInfo BuildVarDeclAttr(const AST::VarDecl& decl);

/**
 * @brief Checks if a function is a mutable struct function.
 *
 * @param function The function declaration.
 * @return True if the function is a mutable struct function, false otherwise.
 */
bool IsStructMutFunction(const AST::FuncDecl& function);

/**
 * @brief Checks if a global declaration is imported from source code.
 *
 * @param decl The declaration.
 * @param opts The global options.
 * @return True if the declaration is an imported global declaration, false otherwise.
 */
bool IsSrcCodeImportedGlobalDecl(const AST::Decl& decl, const GlobalOptions& opts);

/**
 * @brief Checks if a symbol is an imported declaration.
 *
 * @param decl The declaration.
 * @param opts The global options.
 * @return True if the declaration is an imported symbol, false otherwise.
 */
bool IsSymbolImportedDecl(const AST::Decl& decl, const GlobalOptions& opts);

/**
 * @brief Checks if a function is local.
 *
 * @param func The function declaration.
 * @return True if the function is local, false otherwise.
 */
bool IsLocalFunc(const AST::FuncDecl& func);

/**
 * @brief Retrieves the outer declaration containing the given declaration.
 *
 * @param decl The declaration.
 * @return Pointer to the outer declaration.
 */
AST::Decl* GetOuterDecl(const AST::Decl& decl);

/**
 * @brief Checks if an operator is an overflow operator.
 *
 * @param name The name of the operator.
 * @param type The function type.
 * @return True if the operator is an overflow operator, false otherwise.
 */
bool IsOverflowOperator(const std::string& name, const FuncType& type);

/**
 * @brief Retrieves the overflow strategy prefix.
 *
 * @param ovf The overflow strategy.
 * @return Overflow strategy prefix.
 */
std::string OverflowStrategyPrefix(OverflowStrategy ovf);

/**
 * @brief Checks if an operator is an overflow operator.
 *
 * @param name The name of the operator.
 * @return True if the operator is an overflow operator, false otherwise.
 */
bool IsOverflowOperator(const std::string& name);

/**
 * @brief Checks if a type can be an integer type.
 *
 * @param type The type to check.
 * @return True if the type can be an integer type, false otherwise.
 */
bool CanBeIntegerType(const Type& type);
/**
 * @brief Adjusts the type of a variable initialization.
 *
 * This function takes in a function type, an outer declaration, a CHIR builder, and a CHIR type,
 * and returns a pointer to a FuncType. It is used to adjust the type of a variable initialization
 * based on the given parameters.
 *
 * @param funcType The function type.
 * @param outerDecl The outer declaration.
 * @param builder The CHIR builder.
 * @param chirType The CHIR type.
 * @return A pointer to a FuncType.
 */
FuncType* AdjustVarInitType(
    const FuncType& funcType, const AST::Decl& outerDecl, CHIRBuilder& builder, CHIRType& chirType);

/**
 * @brief Try to get a pointer to an object of type T from the cache.
 *
 * @param key The key associated with the object to retrieve.
 * @param cache The cache containing key-value pairs where values are pointers to U.
 * @return A pointer to the object of type T if found; otherwise, nullptr.
 */
template<typename U, typename T>
inline typename std::enable_if<std::is_base_of_v<U, T>, T*>::type TryGetFromCache(const std::string& key,
    const std::unordered_map<std::string, U*>& cache)
{
    auto it = cache.find(key);
    return it == cache.end() ? nullptr : dynamic_cast<T*>(it->second);
}

void SetCompileTimeValueFlagRecursivly(Func& initFunc);

/**
 * @brief Retrieves the instantiated member type by given type and name.
 *
 * @param rootType root type
 * @param names Member var name.
 * @param builder The CHIR builder used for building the type.
 * @return The instantiated member type.
 */
Type* GetInstMemberTypeByName(
    const CustomType& rootType, const std::vector<std::string>& names, CHIRBuilder& builder);

/**
 * @brief Retrieves the instantiated member type by given type and name, checking for read-only.
 *
 * @param rootType root type
 * @param path Member var name.
 * @param builder The CHIR builder used for building the type.
 * @return A pair containing the type and a boolean flag indicating read-only status.
 */
std::pair<Type*, bool> GetInstMemberTypeByNameCheckingReadOnly(
    const CustomType& rootType, const std::vector<std::string>& names, CHIRBuilder& builder);
std::pair<Type*, bool> GetInstMemberTypeByNameCheckingReadOnly(
    const GenericType& rootType, const std::vector<std::string>& names, CHIRBuilder& builder);

Type* AddRefIfFuncIsMutOrClass(Type& thisType, const AST::FuncDecl& funcDecl, CHIRBuilder& builder);
} // namespace CHIR
} // namespace Cangjie

#endif
