// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Define all AST Attribute and helper classes.
 */

#ifndef CANGJIE_AST_ATTRIBUTE_H
#define CANGJIE_AST_ATTRIBUTE_H
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Cangjie::AST {
using AttrSizeType = uint64_t;
/**
 * Attribute fields size.
 */
constexpr AttrSizeType ATTR_SIZE = 64;

/**
 * Node properties: decl modifiers, invalid status, etc.
 */
enum class Attribute {
    /**
     * Mark whether nominal types form cycles or whether type alias decls form a cycles;
     * Used when checking nominal and alias types.
     * W: PreCheck.
     * R: TypeManager, TypeChecker.
     */
    IN_REFERENCE_CYCLE,

    /**
     * Mark whether code is unreachable.
     * W: Collector (Sema), TypeCheckMatchExpr, Utils (Sema), LLVMCodeGen.
     * R: AST2CHIR, LLVMCodeGen.
     * CONSIDER REMOVE THE ATTRIBUTE. READ ONLY ONCE.
     */
    UNREACHABLE,

    /**
     * Mark whether the decl will be implicit used by other package.
     * W: Sema.
     * R: ASTSerialization.
     */
    IMPLICIT_USED,

    /**
     * Mark whether a declaration is imported from other packages. Used by type checks,
     * W: HLIRCodeGen, HLIRCodeGen, LLVMCodeGen, ASTSerialization, ImportManager, GenericInstantiation
     * R: TypeChecker, ASTSerialization, FFISerialization, ADReverse, HLIRCodeGen, HLIR2LLVM.
     */
    IMPORTED,

    /**
     * Mark whether a declaration is global. Used by ADReverse, AST2GIR, HLIR, LLVM,
     * W: ADReverse, CHIRRewrite, FFISerialization, Collector, TypeChecker
     * R: ADReverse, AST2CHIR, HLIRCodeGen, LLVMCodeGen, TypeChecker,
     *    CompilerInstance
     */
    GLOBAL,

    /**
     * Mark whether a variable declaration has been initialized.
     * W: ASTSerialization, ImportManager, CheckInitialization (TypeChecker).
     * R: CheckInitialization (TypeChecker).
     */
    INITIALIZED,

    /**
     * Mark whether an AST Node is added by the compiler.
     * W: Parser, PreCheck, TypeChecker, Desugar, Collector, GIR2AST, Clone, Create, MacroExpansion.
     * R: ASTContext, ADReverse, PrintNode, TypeChecker.
     */
    COMPILER_ADD,

    /**
     * Mark whether an AST Node is a generic declaration.
     * W: Parser, GenericInstantiation.
     * R: AST2CHIR, Walker, HLIRCodeGen, LLVMCodeGen, Sema, TypeManager, Collector, GenericInstantiation,
     * TypeChecker.
     */
    GENERIC,

    // OPTIMIZE: use macro methods to keep consistent.
    /**
     * Mark whether a function in interface has default implementation
     * Or whether a vardecl has default initializer, eg: true if var a = 1 , false is let a : Int64
     * W: Parser.
     * R: ASTSerialization, Sema, CHIR Rewrite
     */
    DEFAULT,

    /**
     * Mark whether a member is a static one.
     * W: PreCheck, TypeChecker
     * R: AST2CHIR, HLIRCodeGen, LLVMCodeGen, parser, Collector, PreCheck, TypeChecker.
     */
    STATIC,

    /**
     * Mark whether a member is a public one.
     * W: PreCheck
     * R: PreCheck, TypeChecker.
     */
    PUBLIC,

    /**
     * Mark whether a member is a private one.
     * W: PreCheck.
     * R: LLVMCodeGen, CheckInitialization (Sema), PreCheck, TypeChecker, Utils (Sema).
     */
    PRIVATE,

    /**
     * Mark whether a member is a protected one.
     * W: FFISerialization, PreCheck.
     * R: PreCheck, TypeChecker.
     */
    PROTECTED,

    /**
     * Mark whether a declaration is an external one.
     * W: ADReverse, Parser, Desugar, PreCheck, TypeChecker.
     * R: ASTSerialization, TypeChecker, Types.cpp, AST2CHIR, ASTSerialization, Parser, PreCheck.
     */
    EXTERNAL,

    /**
     * Mark whether a declaration is an internal one.
     * W: ADReverse, Parser, Desugar, PreCheck, TypeChecker.
     * R: ASTSerialization, TypeChecker, Types.cpp, AST2CHIR, ASTSerialization, Parser, PreCheck.
     */
    INTERNAL,

    /**
     * Mark whether a declaration in fact overrides the inherited one (even if the user does not annotate '@override').
     * W: TypeChecker.
     * R: PreCheck, TypeChecker.
     * Problems: How does it even happen since we have PreCheck --> TypeChecker?
     */
    OVERRIDE,

    /**
     * Mark whether a declaration in fact overrides the inherited one (even if the user does not annotate '@redef').
     * W: TypeChecker.
     * R: PreCheck, TypeChecker.
     * Problems: How does it even happen since we have PreCheck --> TypeChecker?
     */
    REDEF,

    /**
     * Mark whether a function is an abstract one.
     * W: Parser.
     * R: ABSTRACT, AST2CHIR, CodeGenHLIR, FFTSerialization, PreCheck, TypeChecker.
     */
    ABSTRACT,

    /**
     * Mark whether a declaration is a sealed one.
     * W: Parser.
     * R: TypeChecker.
     */
    SEALED,

    /**
     * Mark whether a declaration is in fact open (even if the user does not annotate '@open').
     * W: FFISerialization, PreCheck.
     * R: CHIRRewrite, TypeChecker.
     */
    OPEN,

    /**
     * Mark whether a declaration is a operator one.
     * W: TypeChecker.
     * R: CodeGenLLVM, Parser, PreCheck, TypeChecker.
     */
    OPERATOR,

    /**
     * Mark whether a declaration is a foreign one.
     * W: PreCheck.
     * R: AST2CHIR, CodeGenHLIR, CodeGenJs, WellFormednessChecker, Parser, TypeChecker, PreCheck.
     */
    FOREIGN,

    /**
     * Mark whether a declaration is a unsafe one.
     * W: Parser, Desugar.
     * R: Parser, CheckInitialization (Sema), Desugar, TypeCHecker.
     */
    UNSAFE,

    /**
     * Mark whether a declaration is a mutable one.
     * W: PreCheck.
     * R: AST2CHIR, CodeGenHLIR, TypeChecker.
     */
    MUT,

    /**
     * Check if a node has broken child node.
     * W: Parser.
     * R: Parser.
     */
    HAS_BROKEN,

    /**
     * Mark whether a declaration is defined within a Struct.
     * W: Parser, PreCheck, TypeChecker, Utils (Sema).
     * R: CheckInitialization (Sema), CodeGenHLIR, CodeGenLLVM, CHIRRewrite.
     */
    IN_STRUCT,

    /**
     * Mark whether a declaration is defined within an extend.
     * W: PreCheck, TypeChecker.
     * R: TypeChecker, AST2CHIR, CodeGenHLIR.
     */
    IN_EXTEND,

    /**
     * Mark whether a declaration is defined within an enum.
     * W: PreCheck.
     * R: Sema, AST2CHIR.
     */
    IN_ENUM,

    /**
     * Mark whether a declaration is defined within an interface or class.
     * W: PreCheck, TypeChecker, Utils (Sema), ASTSerialization, Parser, Collector, Desugar.
     * R: PreCheck, TypeChecker, CodeGenHLIR,
     */
    IN_CLASSLIKE,

    /**
     * Mark whether a Node is within a macro body.
     * W: None.
     * R: None.
     * Problems: It seems not used at all.
     */
    IN_MACRO,

    /**
     * Mark whether a constructor is a primary one.
     * W: Desugar.DesugarPrimaryCtorSetPrimaryFunc (Sema).
     * R: CheckInitialization (Sema), TypeChecker.
     */
    PRIMARY_CONSTRUCTOR,

    /**
     * Mark whether a function is a constructor.
     * W: Parser, Collector, Desugar, Utils (Sema)
     * R: AllCodeGen, Parser, Desugar, PreCheck, TypeChecker, CheckInitialization (Sema), Expand, AST2CHIR.
     * Problems: The same identifier is defined in Space.h and widely used in Space.cpp
     */
    CONSTRUCTOR,

    /**
     * Mark whether a function is an enum constructor.
     * W: Collector.
     * R: AST2CHIR, PreCheck, TypeChecker, TypeManager, Space, LLVMCodeGen.
     * Problems: Attributes of the same kind are used 'randomly'... No rules and patterns.
     */
    ENUM_CONSTRUCTOR,

    /**
     * Mark whether a function is finalizer.
     * W: Parser.
     * R: Parser.
     */
    FINALIZER,

    /**
     * Mark whether source code is cloned. NEEDED BY LSP.
     * W: Clone, MacroExpansion, Parser, Utils (Sema).
     * R: None.
     */
    IS_CLONED_SOURCE_CODE,

    /**
     * Mark whether a variable is captured by a lambda or a function.
     * W: TypeChecker.
     * R: TypeChecker, CodeGenLLVM, CodeGenHLIR.
     */
    IS_CAPTURE,

    /**
     * Mark whether the target of the node is defined in core.
     * W: Desugar
     * R: Desugar
     */
    IN_CORE,

    /**
     * Mark whether a type needs to be boxed into a class type (that has uniform memory representation).
     * W: Desugar.
     * R: Desugar.
     */
    NEED_AUTO_BOX,

    /**
     * Mark whether a node is expanded from macros.
     * W: MacroExpansion.
     * R: None.
     */
    MACRO_EXPANDED_NODE [[deprecated("Redundant with Node->curMacroCall; will be removed in the future.")]],

    /**
     * Mark whether a function definition is a macro definition.
     * W: Parser.
     * R: TypeChecker, Parser.
     */
    MACRO_FUNC,

    /**
     * Mark whether a function definition is a wrapper function generated from some macro definition.
     * W: Parer.
     * R: CodeGenHLIR, TypeCheckExpr.
     */
    MACRO_INVOKE_FUNC,

    /**
     * Mark whether a function definition' body is a wrapper function's body generated from some macro definition.
     * W: Parser.
     * R: TypeCheckExpr, CodeGenLLV, CodeGenHLIR.
     */
    MACRO_INVOKE_BODY,

    /**
     * Mark whether an expression appears on the left-hand side of an assign expression.
     * W: Collector.
     * R: TypeCHeckExpr, TypeCheckExtend.
     */
    LEFT_VALUE,

    // Native node kind, for @C etc.
    /**
     * Mark whether a node is c call.
     * W: Parser.
     * R: Parser, Sema, HLIRCodeGen.
     */
    C,

    /**
     * Mark whether a function or variable is imported with definition body.
     * W: Modules.
     * R: CHIR
     */
    SRC_IMPORTED,

    /**
     * Mark whether a node is foreign java call.
     * Attribute is deprecated because it is not used anymore.
     * Could be considered for removal with any other compatibility-breaking changes in the attributes structure.
     * W: -
     * R: - (before deprecation: Parser, ImportManager, Sema, AST2CHIR, HLIRCodeGen).
     */
    JAVA_APP [[deprecated("this attribute is not a part of cjnative"
        "This attribute exists for compatibility persistance only and could be considered for removal")]],
    JAVA_EXT [[deprecated("this attribute is not a part of cjnative"
        "This attribute exists for compatibility persistance only and could be considered for removal")]],
    /**
     * Mark whether a node is std call.
     * W: Parser.
     * R: HLIRCodeGen.
     */
    STD_CALL,

    /**
     * Mark whether a node cannot mangle.
     * The attribute will be set in such cases:
     *   - All kinds of CFunc, including foreign func, @C func, and CFunc lambda.
     *   - Base autoboxed class declarations.
     *   - Wrapper functions created when desugaring macro.
     *   - Non-const and non-global VarDecl(Only temp var of StrInterpolationExpr desugar set for now).
     * W: Parser, Sema.
     * R: Sema, Mangler.
     */
    NO_MANGLE,

    /**
     * Mark whether a node has been through initialization check to avoid infinite loop.
     * W: Sema.
     * R: Sema.
     */
    INITIALIZATION_CHECKED,

    /**
     * Mark a broken node and no further checking is necessary.
     * W: Parser, Sema.
     * R: Parser, Sema.
     */
    IS_BROKEN,

    /**
     * Mark whether this decl is an instantiated generic decl.
     * W: GenericInstantiator.
     * R: Sema, ImportManager, HLIRCodeGen, LLVMCodeGen.
     */
    GENERIC_INSTANTIATED,

    /**
     * Mark whether param or arg has initial value.
     * W: Sema.
     * R: Sema, AST2CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    HAS_INITIAL,

    /**
     * Mark if foreign variable is volatile in cjo.
     * W: AUTOSDK.
     * R: Sema.
     */
    IS_VOLATILE,

    /**
     * Mark whether the node use overflow strategy.
     * W: Parser, Sema.
     * R: Sema, HLIRCodeGen.
     */
    NUMERIC_OVERFLOW,

    /**
     * Mark whether this function will be replaced with intrinsic.
     * W: Parser, Sema.
     * R: Sema, AST2CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    INTRINSIC,

    /**
     * Mark whether the node is generated by external tool.
     * W: cjogen, Parser.
     * R: Modules, Sema, ImportManager, HLIRCodeGen, Macro.
     */
    TOOL_ADD,

    /**
     * Mark whether the node is visited by type check.
     * W: Sema, ImportManager (clear the attribute when export&import AST).
     * R: Sema.
     */
    IS_CHECK_VISITED,

    /**
     * Mark whether 1. the node is a package and is going to be incremental compiled.
     *              2. the node is a decl which is incrementally cached and do not need typechecking and IR generation.
     * W: IncrementalCompilerInstance, ASTSerialization
     * R: GenericInstantiation.
     */
    INCRE_COMPILE,
    /**
     * Mark whether the node is a call expr which is desugared call with side effect, and has 'mapExpr' in children.
     * W: Sema
     * R: AST2CHIR
     */
    SIDE_EFFECT,
    /**
     * Mark whether the node is an additional node implicitly added by the compiler.
     * W: ImportManager, Desugar
     * R: ImportManager, CodeGen-Debug, CHIR.
     */
    IMPLICIT_ADD,
    /**
     * Mark whether the func is desugared from main decl.
     * W: Desugar
     * R: Sema, CodeGen
     */
    MAIN_ENTRY,
    /**
     * Mark those declarations or expressions which were generated for mocking purposes.
     * Such AST nodes aren't handled for further mocking preparations like replacing calls with accessors.
     * W: MockSupportManager, MockManager
     * R: MockSupportManager, MockManager
     */
    GENERATED_TO_MOCK,

    /**
     * Mark whether the expression is the outermost binary expression.
     * For example, in `let _ = 1 + 2 * 3`, the addition is the outermost expression.
     * W: Sema
     * R: Sema
     */
    IS_OUTERMOST,

    /**
     * Mark whether the decl is modified by @Annotation, or an array literal is a collection of annotations.
     * W: Sema
     * R: Sema
     */
    IS_ANNOTATION,

    /**
     * Mark whether the decl needs reflect info.
     * W: Sema
     * R: Codegen
     */
    NO_REFLECT_INFO,
    /**
     * Mark whether this class or package supports mocking.
     * If special compilation mode is used to compile some package,
     * then this flag should be set to each declaration of that package.
     * W: ASTLoader, MockSupportManager
     * R: ASTWriter, MockSupportManager, TestManager
     */
    MOCK_SUPPORTED,
    /**
     * Mark whether this class was changed to open only for mocking purposes.
     * W: MockSupportManager
     * R: MockSupportManager
     */
    OPEN_TO_MOCK,
    /**
     * Mark whether this function or property in extend body implement abstract function in inherited interfaces.
     * W: StructInheritanceChecker
     * R: AnalyzeFunctionLinkage
     */
    INTERFACE_IMPL,
    /**
     * Mark whether the declaration is being compiled for using in tests. It enables extended exporting abilities.
     * W: DesugarAfterInstantiation
     * R: ImportManager
     */
    FOR_TEST,
    /**
     * Mark whether this declaration contains createMock/createSpy calls inside.
     * W: TestManager
     * R: PartialInstantiation, TestManager
     */
    CONTAINS_MOCK_CREATION_CALL,
    // For compatibility, new enumerated values need add last.
    /**
     * Mark whether it is "common" declaration.
     * W: Parser
     * R: ASTSerialization, SEMA, AST2CHIR
     */
    COMMON,
    /**
     * Mark whether the decaration defined in common part of CP package.
     */
    FROM_COMMON_PART,
    /**
     * Inform if `specific enum` matched with non-exhaustive `common enum`
     */
    COMMON_NON_EXHAUSTIVE,
    /**
     * Mark whether it is "specific" declaration.
     * W: ASTSerialization
     * R: ASTChecker, AST2CHIR
     */
    SPECIFIC,
    /**
     * Mark node that is common decl with default implementation.
     * The decl is including common func/var/prop
     * W: Parser
     * R: SEMA, AST2CHIR
     */
    COMMON_WITH_DEFAULT,

    /**
     * Mark whether a declaration is a foreign java type.
     * W: Parser.
     * R: Sema.
     */
    JAVA_FOREIGN,
    /**
     * Mark whether a class is a class exported to java.
     * W: Parser, Sema.
     * R: Sema.
     */
    JAVA_EXPORT,

    /**
     * Mark whether a function of '@Java foreign' interface has default implementation on java side.
     * W: Parser.
     * R: Sema.
     */
    JAVA_HAS_DEFAULT,

    /**
     * Mark whether a class is a synthetic wrapper class generated for every foreign interface and abstract class.
     * W: Sema.
     * R: Sema.
     */
    JAVA_FOREIGN_SYNTHETIC_WRAPPER,
    /**
     * Mark whether a declaration is a foreign Objective-C type.
     * W: Parser.
     * R: Sema.
     */
    OBJ_C_FOREIGN,
    /**
     * Mark whether a class is a class exported to Objective-C.
     * W: Parser.
     * R: Sema.
     */
    OBJ_C_EXPORT,
    /**
     * Marks whether a cangjie method is an Objective-C init.
     * W: Parser.
     * R: Sema.
     */
    OBJ_C_INIT,

    /**
     * Marks cangjie method as @optional in Objective-C side
     * W: Parser.
     * R: Sema.
     */
    OBJ_C_OPTIONAL,

    /**
     * Mark whether a pure cangjie decl is mapped to use by java side.
     * Attribute is deprecated since "cjmapping" has been removed.
     * Could be considered for removal with any other compatibility-breaking changes in the attributes structure.
     * W: - (before deprecation: Parser).
     * R: - (before deprecation: Sema).
     */
    JAVA_CJ_MAPPING [[deprecated("reverse-direction interop has been removed. "
        "This attribute exists for compatibility persistance only and could be considered for removal")]],

    /**
     * Mark whether a pure cangjie decl is mapped to use by Objective-C side.
     * Attribute is deprecated since "cjmapping" has been removed.
     * Could be considered for removal with any other compatibility-breaking changes in the attributes structure.
     * W: - (before deprecation: Parser).
     * R: - (before deprecation: Sema).
     */
    OBJ_C_CJ_MAPPING [[deprecated("reverse-direction interop has been removed. "
        "This attribute exists for compatibility persistance only and could be considered for removal")]],

    /**
     * Mark whether a class decl is an interface-forward class.
     * A forward class is used to forward the method call to Java side.
     * Attribute is deprecated since "cjmapping" has been removed.
     * Could be considered for removal with any other compatibility-breaking changes in the attributes structure.
     * W: - (before deprecation: Sema).
     * R: - (before deprecation: Sema).
     */
    CJ_MIRROR_JAVA_INTERFACE_FWD [[deprecated("reverse-direction interop has been removed. "
        "This attribute exists for compatibility persistance only and could be considered for removal")]],

    /**
     * Mark whether a node is a desugared field of foreign `@Java` or `@ObjC` declaration.
     * Usually the node is a prop decl.
     * W: Parser.
     * R: Sema.
     */
    DESUGARED_FOREIGN_FIELD,

    /**
     * Mark whether a node is a special flag, which marks the class instance as initialized.
     * Attribute is deprecated since `$hasInited` is generated in CHIR and recognized by name.
     * Could be considered for removal with any other compatibility-breaking changes in the attributes structure.
     * W: - (before deprecation: Sema).
     * R: - (before deprecation: AST2CHIR).
     */
    HAS_INITED_FIELD [[deprecated("$hasInited field generation has moved to CHIR. "
        "This attribute exists for compatibility persistance only and could be considered for removal")]],

    /**
     * Mark whether a class is a handle wrapper class generated for each foreign Objective-C interface.
     * W: Sema.
     * R: Sema.
     */
    OBJ_C_FOREIGN_INTERFACE_HANDLE_WRAPPER,

    /**
     * Mark whether a function in interface-forward class is a default implementation of cj-mapping interface.
     * Attribute is deprecated since "cjmapping" has been removed.
     * Could be considered for removal with any other compatibility-breaking changes in the attributes structure.
     * W: - (before deprecation: Sema).
     * R: - (before deprecation: Sema).
     */
    CJ_MIRROR_JAVA_INTERFACE_DEFAULT [[deprecated("reverse-direction interop has been removed. "
        "This attribute exists for compatibility persistance only and could be considered for removal")]],

    /**
     * Mark whether a class decl is an interface-forward class.
     * A forward class is used to forward the method call to ObjC side.
     * Attribute is deprecated since "cjmapping" has been removed.
     * Could be considered for removal with any other compatibility-breaking changes in the attributes structure.
     * W: - (before deprecation: Sema).
     * R: - (before deprecation: Sema).
     */
    CJ_MIRROR_OBJC_INTERFACE_FWD [[deprecated("reverse-direction interop has been removed. "
        "This attribute exists for compatibility persistance only and could be considered for removal")]],

    /**
     * Mark declaration that was already loaded from another CJO,
     * this solution need to be improved by modernization of ASTLoader.
     * W: ASTLoader
     * R: ASTLoader
     */
    ALREADY_LOADED,

    /**
     * Mark whether a class is a registry companion class generated for every exported Java declaration.
     * W: Sema.
     * R: Sema.
     */
    JAVA_EXPORT_REGISTRY_COMPANION,

    /**
     * Mark that the function does not throw any exceptions.
     * Should be used only on generated functions that are generated as non-throwing.
     * W: Sema.
     * R: Sema.
     */
    DOES_NOT_THROW,

    /**
     * Mark whether a class is a registry companion class generated for every exported Objective-C declaration.
     * W: Sema.
     * R: Sema.
     */
    OBJ_C_EXPORT_REGISTRY_COMPANION,

    /**
     * Mark a member of an exported '@ObjC' that stands in for one moved to the registry companion: the property a
     * moved field is now read through, its accessors, and the forwarder left behind by a moved static
     * function. Such a member is a desugaring of what the user wrote, so the rules that tell a property from
     * a field must keep seeing the field.
     * W: Sema.
     * R: Sema.
     */
    OBJ_C_EXPORT_MOVED_MEMBER_PROXY,

    /**
     * Mark a function declared with @ArkTS, including declarations imported from CJO.
     * W: Parser, ASTLoader.
     * R: ArkTS entry selection, ASTWriter.
     */
    ARKTS,
    // Ordinary CJ wrapper generated from an ArkTS foreign function.
    ARKTS_FUNCTION_WRAPPER,

    AST_ATTR_END,
};

class AttributePack {
public:
    AttributePack() : attributes(attrSetSize)
    {
    }
    explicit AttributePack(const std::vector<std::bitset<ATTR_SIZE>>& bitSets) : attributes(bitSets)
    {
        attributes.resize(attrSetSize); // Guarantees the container's size is always enough.
    }
    ~AttributePack() = default;

    /**
     * Set the attributes in @p attrs to @c true.
     */
    void SetAttr(Attribute attr, bool enable)
    {
        size_t idx = static_cast<size_t>(attr) / ATTR_SIZE;
        (void)attributes[idx].set(static_cast<size_t>(attr) % ATTR_SIZE, enable);
    }

    /**
     * Check whether the @p attr is @c true.
     */
    bool TestAttr(Attribute attr) const
    {
        size_t idx = static_cast<size_t>(attr) / ATTR_SIZE;
        return idx < attributes.size() && attributes[idx].test(static_cast<size_t>(attr) % ATTR_SIZE);
    }

    std::vector<std::bitset<ATTR_SIZE>> GetRawAttrs() const
    {
        return attributes;
    }

    std::vector<AttrSizeType> GetAllIdxOfAttr() const;

    std::string ToString() const;

private:
    static constexpr size_t attrSetSize = static_cast<size_t>(Attribute::AST_ATTR_END) / ATTR_SIZE + 1;
    std::vector<std::bitset<ATTR_SIZE>> attributes;
};

// Static assertion to prevent integer overflow in GetAllIdxOfAttr
static_assert(static_cast<size_t>(Attribute::AST_ATTR_END) < std::numeric_limits<AttrSizeType>::max() / ATTR_SIZE,
    "Attribute count may cause overflow in GetAllIdxOfAttr");
} // namespace Cangjie::AST

#endif
