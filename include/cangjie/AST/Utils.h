// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the AST utils interface.
 */

#ifndef CANGJIE_AST_UTILS_H
#define CANGJIE_AST_UTILS_H

#include <functional>

#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Utils/CastingTemplate.h"

namespace Cangjie::AST {
/**
 * Add Attribute and curfile in macro expanded node.
 */
void AddMacroAttr(AST::Node& node);

/**
 * Recursively set 'curFile' to @p file for @p root.
 */
void AddCurFile(AST::Node& root, Ptr<AST::File> file = nullptr);

/**
 * Whether the macrocall node is actually a pure annotation.
 */
inline bool IsPureAnnotation(const AST::MacroInvocation& invocation)
{
    return invocation.isCustom && invocation.isCurFile;
}

std::vector<Ptr<const AST::Modifier>> SortModifierByPos(const std::set<AST::Modifier>& modifiers);

/**
 * Whether the given node is a class or struct constructor.
 */
inline bool IsInstanceConstructor(const AST::Node& node)
{
    return node.TestAttr(AST::Attribute::CONSTRUCTOR) && !node.TestAttr(AST::Attribute::STATIC);
}

/**
 * Whether the given decl is the static initializer.
 */
inline bool IsStaticInitializer(const AST::Decl& decl)
{
    return decl.TestAttr(AST::Attribute::STATIC, AST::Attribute::CONSTRUCTOR);
}

/**
 * Whether the given decl is a class, struct or enum constructor.
 */
inline bool IsClassOrEnumConstructor(const AST::Decl& decl)
{
    return IsInstanceConstructor(decl) || decl.TestAttr(AST::Attribute::ENUM_CONSTRUCTOR);
}

inline bool IsGlobalOrMember(const AST::Node& node)
{
    return node.TestAnyAttr(AST::Attribute::GLOBAL, AST::Attribute::IN_CLASSLIKE, AST::Attribute::IN_ENUM,
        AST::Attribute::IN_STRUCT, AST::Attribute::IN_EXTEND);
}

inline bool IsInstanceMember(const AST::Decl& decl)
{
    return decl.outerDecl && decl.outerDecl->IsNominalDecl() && !decl.TestAttr(AST::Attribute::STATIC);
}

inline bool IsGlobalOrStaticVar(const Decl& decl)
{
    return decl.astKind == ASTKind::VAR_DECL && decl.TestAnyAttr(Attribute::STATIC, Attribute::GLOBAL);
}

inline bool IsInheritableClass(const Decl& decl)
{
    return decl.astKind == ASTKind::CLASS_DECL && decl.TestAnyAttr(Attribute::OPEN, Attribute::ABSTRACT);
}

bool IsMemberParam(const Decl& decl);

bool IsSingleRuneStringLiteral(const Expr& expr);
bool IsSingleByteStringLiteral(const Expr& expr);

Ptr<AST::FuncDecl> GetSizeDecl(const AST::Ty& ty);

std::optional<AST::Attribute> HasJavaAttr(const AST::Node& node) noexcept;
/**
 * Initialize @p lce 's constValue with its string value.
 */
void InitializeLitConstValue(AST::LitConstExpr& lce);

struct FloatTypeInfo {
    uint64_t inf;
    std::string min;
    std::string max;
};
FloatTypeInfo GetFloatTypeInfoByKind(AST::TypeKind kind);

void SetOuterFunctionDecl(AST::Decl& decl);
bool IsInDeclWithAttribute(const AST::Decl& decl, AST::Attribute attr);

/**
 * Iterate all toplevel decls in given 'pkg', and perform the function 'process' on each of toplevel decl.
 */
inline void IterateToplevelDecls(const Package& pkg, const std::function<void(OwnedPtr<Decl>&)>& process)
{
    for (auto& file : pkg.files) {
        (void)std::for_each(file->decls.begin(), file->decls.end(), process);
        (void)std::for_each(file->exportedInternalDecls.begin(), file->exportedInternalDecls.end(), process);
    }
}

/**
 * Iterate all exportable function from toplevel and member decls.
 */
void IterateAllExportableDecls(const AST::Package& pkg, const std::function<void(AST::Decl&)> action);

std::vector<Ptr<AST::Pattern>> FlattenVarWithPatternDecl(const AST::VarWithPatternDecl& vwpDecl);
std::string GetAnnotatedDeclKindString(const Decl& decl);
inline bool InsideAtJavaDecl(const AST::Decl& decl)
{
    if ((decl.astKind == AST::ASTKind::FUNC_DECL || decl.astKind == AST::ASTKind::VAR_DECL) &&
        decl.TestAttr(AST::Attribute::IN_CLASSLIKE)) {
        return HasJavaAttr(decl).has_value();
    }
    if (decl.astKind == AST::ASTKind::FUNC_PARAM && decl.outerDecl) {
        return InsideAtJavaDecl(*decl.outerDecl);
    }
    return false;
}

bool IsPackageMemberAccess(const AST::MemberAccess& ma);
bool IsThisOrSuper(const AST::Expr& expr);

AST::AccessLevel GetAccessLevel(const AST::Node& node);
AST::Attribute GetAttrByAccessLevel(AccessLevel level);
std::string GetAccessLevelStr(const AST::Node& node, const std::string& surround = "");
std::string GetAccessLevelStr(const AST::Package& pkg);

inline bool IsCompatibleAccessLevel(AST::AccessLevel srcLevel, AST::AccessLevel refLevel)
{
    return srcLevel <= refLevel;
}

std::string GetImportedItemFullName(const AST::ImportContent& content, const std::string& commonPrefix = "");

void ExtractArgumentsOfDeprecatedAnno(
    const Ptr<AST::Annotation> annotation,
    std::string& message,
    std::string& since,
    bool& strict
);

/// Check whether this condition or condition subtree is a condition, i.e. has a let pattern subtree.
bool IsCondition(const Expr& e);
bool DoesNotHaveEnumSubpattern(const LetPatternDestructor& let);

bool IsValidCFuncConstructorCall(const CallExpr& ce);

inline bool IsNestedFunc(const AST::FuncDecl& fd)
{
    return !fd.TestAttr(AST::Attribute::GLOBAL) && (!fd.outerDecl || fd.outerDecl->IsFunc());
}

inline bool IsDefaultImplementation(const AST::Decl& decl)
{
    return decl.TestAttr(AST::Attribute::DEFAULT) && decl.outerDecl &&
        decl.outerDecl->astKind == AST::ASTKind::INTERFACE_DECL;
}

// Check if the function can be source-exported without regard to its modifier.
inline bool CanBeSrcExported(const AST::FuncDecl& fd)
{
    if (fd.isInline) {
        return true;
    }
    const bool isGenericFunction = (fd.funcBody && fd.funcBody->generic) || fd.TestAttr(Attribute::GENERIC);
    if ((isGenericFunction || fd.IsExportedDecl() || fd.linkage != Linkage::INTERNAL) && (fd.isConst || fd.isFrozen)) {
        return true;
    }
    auto decl = fd.ownerFunc ? fd.ownerFunc.get() : &fd;
    return !IsInDeclWithAttribute(*decl, Attribute::GENERIC_INSTANTIATED) && IsDefaultImplementation(*decl);
}

inline bool IsInstMemberVarInGenericDecl(const AST::VarDecl& vd)
{
    return vd.astKind == ASTKind::VAR_DECL && !vd.TestAttr(AST::Attribute::STATIC) &&
        IsInDeclWithAttribute(vd, AST::Attribute::GENERIC);
}

inline bool IsCommonWithoutDefault(const Decl& decl)
{
    return decl.TestAttr(AST::Attribute::COMMON) && !decl.TestAttr(AST::Attribute::COMMON_WITH_DEFAULT);
}

bool IsVirtualMember(const AST::Decl& decl);

inline bool IsStaticVar(const AST::Decl& decl)
{
    return decl.astKind == AST::ASTKind::VAR_DECL && decl.TestAttr(AST::Attribute::STATIC);
}

/**
 * If the member variable has initializer and there is const init in its parent declaration,
 * it should be source exported.
 */
inline bool IsMemberVarShouldBeSrcExported(const AST::VarDecl& vd)
{
    // Non-const static member variables are not exported because the static init function cannot be invoked by users.
    // Therefore source code export is not required.
    if (!vd.outerDecl || !vd.initializer || vd.TestAttr(AST::Attribute::STATIC)) {
        return false;
    }
    auto& od = *vd.outerDecl;
    return (od.astKind == AST::ASTKind::STRUCT_DECL && StaticCast<AST::StructDecl>(od).HasConstOrFrozenInit()) ||
        (od.astKind == AST::ASTKind::CLASS_DECL && StaticCast<AST::ClassDecl>(od).HasConstOrFrozenInit());
}

struct VarDeclWithPosition {
    Ptr<VarDecl> decl;
    std::size_t fieldPosition;
};

/**
 * Constructs list of class/struct instance variables with correct initialization order.
 * The initialization order of instance variables in same file is preserved.
 * The initializations of common/specific instance variables may be interleaved
 * @param parentDecl Declaration of class/struct
 * @returns List of declarations with their original declaration's index in class/struct
 */
std::vector<VarDeclWithPosition> GetVarsInitializationOrderWithPositions(const Decl& parentDecl);

void InsertPropGetterSignature(PropDecl& prop, Attribute attrToBeSet);

void InsertPropSetterSignature(PropDecl& prop, Attribute attrToBeSet);

void InsertMirrorVarProp(ClassDecl& decl, Attribute attrToBeSet);
} // namespace Cangjie::AST

namespace Cangjie::Interop::Java {
using namespace Cangjie::AST;


bool IsImpl(const Decl& decl);
bool IsJObject(const Decl& decl);
/**
 * For stages where packageName is not set yet
 */
bool IsJObject(const Decl& decl, const std::string& packageName);
bool IsMirror(const Decl& decl);
bool IsCJMapping(const Decl& decl);
bool IsObject(const Decl& decl);

/**
 * public func $getJavaRef(): Java_CFFI_JavaEntity {
 *     return Java_CFFI_JavaEntity()
 * }
 */
void InsertJavaRefGetterStubWithBody(ClassDecl& decl);

bool IsDeclAppropriateForSyntheticClassGeneration(const Decl& decl);

std::string GetSyntheticNameFromClassLike(const ClassLikeDecl& cld);

/**
    * Generates and inserts the synthetic class declaration.
    * The synthetic class implements the given interface or abstract class and has the following structure:
    *
    * Example of generated synthetic
    * ```
    * // CL is interface or abstract class. If CL is interface then JObject will be added as super class
    * class CL_impl <: CL {
    *     init(ref: Java_CFFI_JavaEntity) { // inherited from JObject
    *         $javaref = ref
    *     }
    *
    *     public func $getJavaRef() { // inherited from JObject
    *         return $javaref
    *     }
    * }
    * ```
    */
void InsertSyntheticClassDecl(ClassLikeDecl& decl, File& file);
}

#endif
