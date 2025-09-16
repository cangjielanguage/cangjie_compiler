// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file is name mangling utils.
 */

#ifndef CANGJIE_MANGLE_BASEMANGLER_H
#define CANGJIE_MANGLE_BASEMANGLER_H

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "cangjie/AST/Node.h"
#include "cangjie/Driver/StdlibMap.h"
#include "cangjie/Mangle/MangleUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/FileUtil.h"

namespace Cangjie {
class ManglerContext {
public:
    /**
     * @brief Obtains global wildcard variable index under its scope.
     *
     * @param file Indicates AST::File.fileHash.
     * @param target Indicates the index needs to be obtained which is AST::VarWithPatternDecl.
     * @return std::optional<size_t> The index.
     */
    std::optional<size_t> GetIndexOfGlobalWildcardVar(
        const Ptr<const AST::File> file, const Ptr<const AST::VarWithPatternDecl> target) const;

    /**
     * @brief Obtains local wildcard variable index under its scope.
     *
     * @param node Indicates "global AST::VarDeclAbstract"/AST::FuncDecl/AST::PrimaryCtorDecl/AST::LambdaExpr node ptr.
     * @param target Indicates the index needs to be obtained which is AST::VarWithPatternDecl.
     * @return std::optional<size_t> The index.
     */
    std::optional<size_t> GetIndexOfLocalWildcardVar(
        const Ptr<const AST::Node> node, const Ptr<const AST::VarWithPatternDecl> target) const;

    /**
     * @brief Obtains global variable, local variable of function or lambda index under its scope.
     *
     * @param node Indicates "global AST::VarDeclAbstract"/AST::FuncDecl/AST::PrimaryCtorDecl/AST::LambdaExpr node ptr.
     * @param target Indicates the index needs to be obtained which is AST::VarDeclAbstract.
     * @return std::optional<size_t> The index.
     */
    std::optional<size_t> GetIndexOfVar(
        const Ptr<const AST::Node> node, const Ptr<const AST::VarDeclAbstract> target) const;

    /**
     * @brief Obtains global lambda, local lambda of function or lambda index under its scope.
     *
     * @param node Indicates "global AST::VarDeclAbstract"/AST::FuncDecl/AST::PrimaryCtorDecl/AST::LambdaExpr node ptr.
     * @param target Indicates the index needs to be obtained which is AST::LambdaExpr.
     * @return std::optional<size_t> The index.
     */
    std::optional<size_t> GetIndexOfLambda(
        const Ptr<const AST::Node> node, const Ptr<const AST::LambdaExpr> target) const;

    /**
     * @brief Obtains global extend index under its scope.
     *
     * @param file Indicates AST::File.fileHash.
     * @param target Indicates the index needs to be obtained which is AST::ExtendDecl.
     * @return std::optional<size_t> The index.
     */
    std::optional<size_t> GetIndexOfExtend(
        const Ptr<const AST::File> file, const Ptr<const AST::ExtendDecl> target) const;

    /**
     * @brief Obtain global wildcard variable map which is used to calculate index.
     *
     * @param file Indicates AST::File.fileHash.
     * @param vpd Indicates the index needs to be calculated which is AST::VarWithPatternDecl.
     */
    void SaveGlobalWildcardVar(const Ptr<const AST::File> file, const Ptr<AST::VarWithPatternDecl> vpd);

    /**
     * @brief Obtain local wildcard variable map which is used to calculate index.
     *
     * @param node Indicates "global AST::VarDeclAbstract"/AST::FuncDecl/AST::PrimaryCtorDecl/AST::LambdaExpr node ptr.
     */
    void SaveLocalWildcardVar2Decl(const Ptr<AST::Node> node);

    /**
     * @brief Obtain map contains global variable, local variable of function or lambda.
     *
     * @param node Indicates "global AST::VarDeclAbstract"/AST::FuncDecl/AST::PrimaryCtorDecl/AST::LambdaExpr node ptr.
     */
    void SaveVar2CurDecl(const Ptr<AST::Node> node);

    /**
     * @brief Obtain map contains global lambda, local lambda of function or lambda.
     *
     * @param node Indicates "global AST::VarDeclAbstract"/AST::FuncDecl/AST::PrimaryCtorDecl/AST::LambdaExpr node ptr.
     */
    void SaveLambda2CurDecl(const Ptr<AST::Node> node);

    /**
     * @brief Obtain global extend decl map.
     *
     * @param file Indicates AST::File.fileHash.
     * @param node Indicates "global AST::ExtendDecl".
     */
    void SaveExtend2CurFile(const Ptr<const AST::File> file, const Ptr<AST::ExtendDecl> node);

    /**
     * @brief Check if all the elements are wildcard.
     *
     * @param root Indicates pointer of AST::Pattern
     */
    static bool CheckAllElementsWildcard(const Ptr<AST::Pattern>& root);

    /**
     * @brief Obtains name reduce "$test".
     *
     * @param pkgName Indicates unit test package name.
     * @return std::string The package name.
     */
    static std::string ReduceUnitTestPackageName(std::string pkgName)
    {
        const std::string suffix = "$test";
        size_t lenOfSuffix = suffix.length();
        size_t lenOfPkgName = pkgName.length();
        if (lenOfPkgName >= lenOfSuffix && pkgName.substr(lenOfPkgName - lenOfSuffix) == suffix) {
            return pkgName.substr(0, lenOfPkgName - lenOfSuffix);
        }
        return pkgName;
    }

    // Key is file hash, value is global VarWithPatternDecl.
    std::map<Ptr<const AST::File>, std::vector<Ptr<AST::VarWithPatternDecl>>> file2GlobalWildcardVar;
    // Key is AST::File/AST::FuncDecl/AST::LambdaExpr node ptr, value is vector of VarWithPatternDecl.
    std::map<Ptr<const AST::Node>, std::vector<Ptr<AST::VarWithPatternDecl>>> node2LocalWildcardVar;
    // Key is AST::File/AST::FuncDecl/AST::LambdaExpr node ptr, value is vector of lambda expr.
    std::map<Ptr<const AST::Node>, std::vector<Ptr<AST::LambdaExpr>>> node2Lambda;
    // Key is AST::File/AST::FuncDecl/AST::LambdaExpr node ptr, value is map of local var identifier to var decls.
    std::map<Ptr<const AST::Node>, std::map<std::string, std::vector<Ptr<AST::VarDeclAbstract>>>> node2LocalVar;
    std::map<Ptr<const AST::File>, std::map<std::string, std::vector<Ptr<AST::ExtendDecl>>>> file2ExtendDecl;

    std::vector<uint64_t> fileIndexes;
};

// Class BaseMangler is designed for doing name mangling and containing all
// information that mangling need.
class BaseMangler {
public:
    std::string moduleDelimiter = MANGLE_DOLLAR_PREFIX;
    size_t lambdaCounter = 0;
    // Mangler is used to generate mangledName and exportId, in most scenarios, there is no difference.
    // But in Java FFI scenario, the exportId of declarations with generics is different with the mangledName,
    // so we add the member as a flag to indicate which use the mangler is using for.
    bool exportIdMode = false;
    std::unordered_map<std::string, Ptr<ManglerContext>> manglerCtxTable;

    /**
     * @brief The constructor of class BaseMangler.
     */
    BaseMangler(){};

    /**
     * @brief The constructor of class BaseMangler.
     *
     * @param delimiter The delimiter of module.
     * @return BaseMangler The instance of BaseMangler.
     */
    BaseMangler(const std::string& delimiter) : moduleDelimiter(delimiter){};

    /**
     * @brief The destructor of class BaseMangler.
     */
    virtual ~BaseMangler() = default;

    /**
     * @brief Helper function for mangling decl name.
     *
     * @param decl The decl to be mangled.
     * @return std::string The mangled signature.
     */
    std::string Mangle(const AST::Decl& decl) const;

    /**
     * @brief Helper function for mangling decl name with prefix path.
     *
     * @param decl The decl to be mangled.
     * @param prefix Path of the decl.
     * @return std::string The mangled signature.
     */
    std::string Mangle(const AST::Decl& decl, const std::vector<Ptr<AST::Node>>& prefix) const;

    /**
     * @brief Helper function for mangling function decl name.
     *
     * @param funcDecl The function decl to be mangled.
     * @param prefix Path of the function decl.
     * @param genericsTypeStack Generic vector of function mangled name.
     * @param declare Indicates genericsTypeStack vector is get or push.
     * @return std::string The mangled signature.
     */
    std::string MangleFunctionDecl(const AST::FuncDecl& funcDecl, const std::vector<Ptr<AST::Node>>& prefix,
        std::vector<std::string>& genericsTypeStack, bool declare) const;

    /**
     * @brief Helper function for mangling decl name.
     *
     * @param decl The decl to be mangled.
     * @param prefix Path of the decl.
     * @param genericsTypeStack Generic vector of decl mangled name.
     * @param declare Indicates genericsTypeStack vector is get or push.
     * @param withSuffix Indicates whether it is default param function decl.
     * @return std::string The mangled signature.
     */
    std::string MangleDecl(const AST::Decl& decl, const std::vector<Ptr<AST::Node>>& prefix,
        std::vector<std::string>& genericsTypeStack, bool declare, bool withSuffix = true) const;

    /**
     * @brief Helper function for mangling path of the ast node.
     *
     * @param node The node needs to get the prefix path.
     * @param prefix Path of the node.
     * @param genericsTypeStack Generic vector of ast node mangled name.
     * @param declare Indicates genericsTypeStack vector is get or push.
     * @return std::string The mangled signature.
     */
    std::string ManglePrefix(const AST::Node& node, const std::vector<Ptr<AST::Node>>& prefix,
        std::vector<std::string>& genericsTypeStack, bool declare) const;

    /**
     * @brief Obtain package mangled name.
     *
     * @param decl The decl needs to get package mangled name.
     * @return std::string The mangled signature.
     */
    std::string ManglePackage(const AST::Decl& decl) const;

    /**
     * @brief Obtain lambda mangled name.
     *
     * @param lambda The lambda expr.
     * @param prefix Path of the node.
     * @return std::string The mangled signature.
     */
    std::string MangleLambda(const AST::LambdaExpr& lambda, const std::vector<Ptr<AST::Node>>& prefix) const;

    /**
     * @brief Find outer container of lambda.
     *
     * @param iter Iterator of the prefix which is second param.
     * @param prefix Path of the node.
     * @return Ptr<AST::Node> The outer container of lambda.
     */
    Ptr<AST::Node> FindOuterNodeOfLambda(
        const std::vector<Ptr<AST::Node>>::const_iterator& iter, const std::vector<Ptr<AST::Node>>& prefix) const;

    /**
     * @brief Check whether the decl is lacal variable decl.
     *
     * @param decl The decl to be judged.
     * @return bool If yes, true is returned. Otherwise, false is returned.
     */
    bool IsLocalVariable(const AST::Decl& decl) const;

    /**
     * @brief Helper function for mangling type name.
     *
     * @param ty The type to be mangled.
     * @return std::string The mangled signature.
     */
    std::string MangleType(const AST::Ty& ty) const;

    /**
     * @brief Helper function for mangling type name.
     *
     * @param ty The type to be mangled.
     * @param genericsTypeStack Generic vector of type mangled name.
     * @param declare Indicates genericsTypeStack vector is get or push.
     * @param isCollectGTy Indicates whether it is needed to collect generic type.
     * @return std::string The mangled signature.
     */
    std::string MangleType(const AST::Ty& ty, std::vector<std::string>& genericsTypeStack, bool declare = false,
        bool isCollectGTy = true) const;

    /**
     * @brief Obtain mangling type prefix string.
     *
     * @param ty The type to be mangled.
     * @return std::string The mangled prefix string.
     */
    std::string GetPrefixOfType(const AST::Ty& ty) const;

    /**
     * @brief Generate a name based on the `decl` and `typeArgs` of a `ty` node,
     * as a part of the AST type's `mangledName` or a part of the CodeGen type's name.
     *
     * @param ty The struct, interface or class type to be mangled.
     * @param genericsTypeStack Generic vector of type mangled name.
     * @param declare Indicates genericsTypeStack vector is get or push.
     * @param isCollectGTy Indicates whether it is needed to collect generic type.
     * @return std::string The mangled type string.
     */
    std::string MangleUserDefinedType(const AST::Ty& ty, std::vector<std::string>& genericsTypeStack,
        bool declare, bool isCollectGTy = true) const;

    /**
     * @brief Collect variable or lambda to calculate index.
     *
     * @param ctx Save collected variable or lambda.
     * @param pkg The AST::Package node being visited.
     */
    void CollectVarOrLambda(ManglerContext& ctx, AST::Package& pkg) const;

    /**
     * @brief Export id for AST::Package.
     *
     * @param pkg The AST::Package node being visited.
     */
    void MangleExportId(AST::Package& pkg);

    /**
     * @brief Export id for AST::decl.
     *
     * @param decl The AST::Decl node to be exported.
     * @return std::string The export id.
     */
    std::string MangleExportId(AST::Decl& decl) const;

    /**
     * @brief Obtain mangled any type name.
     *
     * @return std::string The mangled any type name.
     */
    std::string GetAnyMangledTypeName() const
    {
        return "CNat3AnyE";
    }

    /**
     * @brief Obtain mangled index of wildcard.
     *
     * @param vwpDecl Indicates The AST::VarWithPatternDecl.
     * @param prefix Path of the AST::VarWithPatternDecl.
     * @return std::optional<size_t> The index.
     */
    std::optional<size_t> GetIndexOfWildcard(
        const AST::VarWithPatternDecl& vwpDecl, const std::vector<Ptr<AST::Node>>& prefix) const;

    /**
     * @brief Obtain ManglerContext of package.
     *
     * @param pkg Indicates pointer of AST::Package.
     * @return std::unique_ptr<ManglerContext> The value of manglerCtxTable map which key is package.
     */
    std::unique_ptr<ManglerContext> PrepareContextForPackage(const Ptr<AST::Package> pkg);

    /**
     * @brief Convert file name to hash value and then to base 62 number.
     *
     * @param input Indicates file name string.
     * @return std::string The string of base 62 number.
     */
    static std::string HashToBase62(const std::string& input);

    /**
     * @brief Determine whether str is hashable.
     *
     * @param str Indicates file name which is hashable.
     * @return bool If yes, true is returned. Otherwise, false is returned.
     */
    static bool IsHashable(const std::string& str);

    /**
     * @brief Determine whether fileName ends with ".cj", if yes, strip it.
     *
     * @param fileName Indicates file name which will strip extension.
     * @return std::string FileName without extension.
     */
    static std::string FileNameWithoutExtension(const std::string& fileName);

protected:
    std::string MangleExtendEntity(const AST::ExtendDecl& extendDecl,
        std::vector<std::string>& genericsTypeStack, bool declare) const;
    std::string MangleFuncParams(const AST::FuncDecl& funcDecl, std::vector<std::string>& genericsTypeStack,
        bool declare, bool isCollectGTy = true) const;
    std::string MangleGenericArguments(const AST::Decl& decl, std::vector<std::string>& genericsTypeStack,
        bool declare) const;
    std::string MangleGenericArgumentsHelper(const AST::Decl& decl, std::vector<std::string>& genericsTypeStack,
        bool declare, bool isCollectGTy = true) const;
    std::string MangleFullPackageName(const std::string& packageName) const;
    std::string MangleFullPackageName(const AST::Decl& decl) const;
    /**
     * @brief Mangle `main` func and `test.entry` func.
     */
    virtual std::optional<std::string> MangleEntryFunction(const AST::FuncDecl& funcDecl) const;
    std::string MangleEnumType(const AST::Ty& ty, std::vector<std::string>& genericsTypeStack,
        bool declare, bool isCollectGTy = true) const;
    std::string MangleRawArrayType(const AST::Ty& ty, std::vector<std::string>& genericsTypeStack,
        bool declare, bool isCollectGTy = true) const;
    std::string MangleVArrayType(const AST::Ty& ty, std::vector<std::string>& genericsTypeStack, bool declare,
        bool isCollectGTy = true) const;
    std::string MangleTupleType(const AST::Ty& ty, std::vector<std::string>& genericsTypeStack,
        bool declare, bool isCollectGTy = true) const;
    std::string MangleFuncType(const AST::Ty& ty, std::vector<std::string>& genericsTypeStack,
        bool declare, bool isCollectGTy = true) const;

    std::string MangleVarDecl(const AST::Decl& decl, const::std::vector<Ptr<AST::Node>>& prefix) const;
    std::string MangleVarWithPatternDecl(const AST::VarWithPatternDecl& vwpDecl,
        const::std::vector<Ptr<AST::Node>>& prefix) const;

    // Collect number of pure wildcard top-level declaration of each file
    mutable std::unordered_map<std::string, int> wildcardMap{};
    mutable std::mutex manglerUpdateMutex;

private:
    // Get the direct parent decl rather than the intermediate decl like `extendDecl`.
    Ptr<const AST::Decl> GetOuterDecl(const AST::Decl& decl) const;
    /**
     * @brief Iterate parent declarations of the decl and itself to find a generic package name,
     * where the generic declaration is declared.
     */
    std::string ManglePackageNameForGeneric(const AST::Decl& decl) const;

    std::string MangleCPointerType(const AST::Ty& ty, std::vector<std::string>& genericsTypeStack, bool declare,
        bool isCollectGTy = true) const;
    std::string MangleGenericType(const AST::Ty& ty, std::vector<std::string>& genericsTypeStack,
        bool declare = false) const;
    std::string MangleGenericType(const AST::Ty& ty) const;
    std::string MangleCStringType() const
    {
        return "k";
    }
    void MangleExportIdForGenericParamDecl(const AST::Decl& decl) const;
};

namespace MangleUtils {
/**
 * @brief Mangle name.
 *
 * @param str The identifier.
 * @return std::string The mangled name which is <identifier-length><identifier>.
 */
inline std::string MangleName(const std::string& str)
{
    return std::to_string(str.length()) + str;
}

inline const std::unordered_map<AST::TypeKind, std::string> PRIMITIVE_TYPE_MANGLE = {
    {AST::TypeKind::TYPE_NOTHING, "n"},
    {AST::TypeKind::TYPE_UNIT, "u"},
    {AST::TypeKind::TYPE_RUNE, "c"},
    {AST::TypeKind::TYPE_BOOLEAN, "b"},
    {AST::TypeKind::TYPE_FLOAT16, "Dh"},
    {AST::TypeKind::TYPE_FLOAT32, "f"},
    {AST::TypeKind::TYPE_FLOAT64, "d"},
    {AST::TypeKind::TYPE_INT8, "a"},
    {AST::TypeKind::TYPE_INT16, "s"},
    {AST::TypeKind::TYPE_INT32, "i"},
    {AST::TypeKind::TYPE_INT64, "l"},
    {AST::TypeKind::TYPE_INT_NATIVE, "q"},
    {AST::TypeKind::TYPE_UINT8, "h"},
    {AST::TypeKind::TYPE_UINT16, "t"},
    {AST::TypeKind::TYPE_UINT32, "j"},
    {AST::TypeKind::TYPE_UINT64, "m"},
    {AST::TypeKind::TYPE_UINT_NATIVE, "r"},
};

inline const std::unordered_map<std::string, std::string> STD_PKG_MANGLE = {
#include "cangjie/Mangle/StdPkg.inc"
};

inline const std::unordered_map<std::string, std::string> OPERATOR_TYPE_MANGLE = {
    {"[]", "ix"},
    {"!", "nt"},
    {"-", "ng"},
    {"**", "pw"},
    {"*", "ml"},
    {"/", "dv"},
    {"%", "rm"},
    {"+", "pl"},
    {"<<", "ls"},
    {">>", "rs"},
    {"<", "lt"},
    {">", "gt"},
    {"<=", "le"},
    {">=", "ge"},
    {"==", "eq"},
    {"!=", "ne"},
    {"&", "an"},
    {"^", "eo"},
    {"|", "or"},
    {"()", "cl"},
};

/**
 * @brief Replace "from" in the string with "to".
 *
 * @param str The original string.
 * @param from The substring to be replaced.
 * @param to The replacement substring.
 * @return std::string The new string after replacement.
 */
inline std::string ReplaceSubstring(std::string str, const std::string& from, const std::string& to)
{
    auto found = str.find(from);
    while (found != std::string::npos) {
        str.replace(found, from.length(), to);
        found = str.find(from);
    }
    return str;
}

/**
 * @brief Check if a declaration is an auto-boxed base declaration.
 *
 * @param decl The declaration to check.
 * @return bool True if the declaration is an auto-boxed base declaration, false otherwise.
 */
inline bool IsAutoBoxedBaseDecl(const AST::Decl& decl)
{
    return decl.TestAttr(AST::Attribute::NO_MANGLE) && decl.astKind == AST::ASTKind::CLASS_DECL;
}

/**
 * @brief Generate a mangled name for an enumeration element.
 *
 * @param enumDeclMangledName The mangled name of the enumeration.
 * @param decl The declaration of the enumeration element.
 * @return std::string The mangled name of the enumeration element.
 */
inline std::string MangleEnumElement(const std::string& enumDeclMangledName, const AST::Decl& decl)
{
    auto elementClassName = enumDeclMangledName + MANGLE_DOLLAR_PREFIX + decl.identifier;
    if (decl.ty && decl.ty->kind == AST::TypeKind::TYPE_FUNC) {
        auto funcTy = static_cast<AST::FuncTy*>(decl.ty.get());
        // Do temp mangle of enum element to ensure not duplicate.
        elementClassName += MANGLE_DOLLAR_PREFIX + std::to_string(funcTy->paramTys.size());
    }
    return elementClassName;
}

/**
 * @brief Get the mangled name of a compiler-added class.
 *
 * @param className The name of the compiler-added class.
 * @return std::string The mangled name of the compiler-added class.
 */
inline std::string GetMangledNameOfCompilerAddedClass(const std::string& className)
{
    // As for the class added by FE, it’s not in any package, so
    // in it’s mangled name, the <package name> would be empty
    // (i.e., length is zero).
    return MANGLE_NESTED_PREFIX + MangleName(className) + MANGLE_SUFFIX;
}

/**
 * @brief Compute the mangled name of a declaration with a custom identifier.
 *
 * @param decl A reference to the AST::Decl.
 * @param customIdentifier A string representing the custom identifier.
 * @return std::string The mangled name of the declaration with the custom identifier.
 */
inline std::string ComputeMangledNameWithCustomIdentifier(AST::Decl& decl, const std::string& customIdentifier)
{
    BaseMangler mangler;
    auto declId = decl.identifier;
    auto declMangledName = decl.mangledName;
    decl.identifier = customIdentifier;
    decl.mangledName = "";
    auto mangledNameWithCustomId = mangler.Mangle(decl);
    decl.identifier = declId;
    decl.mangledName = declMangledName;
    return mangledNameWithCustomId;
}

/**
 * @brief Get the optimized std package name.
 *
 * @param pkgName The original std package name.
 * @return std::string The optimized std package name.
 */
inline std::string GetOptPkgName(const std::string& pkgName)
{
    if (MangleUtils::STD_PKG_MANGLE.find(pkgName) != MangleUtils::STD_PKG_MANGLE.end()) {
        return MangleUtils::STD_PKG_MANGLE.at(pkgName);
    }
    return MangleUtils::MangleName(pkgName);
}

/**
 * @brief Mangle file name part for global private declaration.
 *
 * @param decl A reference to the AST::Decl.
 * @return std::string The mangled file name modified by private.
 */
std::string MangleFilePrivate(const AST::Decl& decl);

/**
 * @brief Convert a decimal string to a mangled number string.
 *
 * @param decimal The input decimal string.
 * @return std::string The mangled number string.
 */
std::string DecimalToManglingNumber(const std::string& decimal);
} // namespace MangleUtils
} // namespace Cangjie
#endif // CANGJIE_MANGLE_BASEMANGLER_H
