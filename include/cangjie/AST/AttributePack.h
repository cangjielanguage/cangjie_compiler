// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
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
    MACRO_EXPANDED_NODE,

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
     * Mark whether a node is foreign call.
     * W: Parser, AST2CHIR.
     * R: Parser, ImportManager, Sema, AST2CHIR, HLIRCodeGen.
     */
    JAVA_APP,
    JAVA_EXT,

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
     * Inform if `platform enum` matched with non-exhaustive `common enum`
     */
    COMMON_NON_EXHAUSTIVE,
    /**
     * Mark whether it is "platform" declaration.
     * W: ASTSerialization
     * R: ASTChecker, AST2CHIR
     */
    PLATFORM,
    /**
     * Mark node that is common decl with default implementation.
     * The decl is including common func/var/prop
     * W: Parser
     * R: SEMA, AST2CHIR
     */
    COMMON_WITH_DEFAULT,

    /**
     * Mark whether a class is java mirror (binding for a java class).
     * W: Parser.
     * R: Sema.
     */
    JAVA_MIRROR,
    /**
     * Mark whether a class is a successor of java mirror (direct or indirect child of java mirror class).
     * W: Parser, Sema.
     * R: Sema.
     */
    JAVA_MIRROR_SUBTYPE,

    /**
     * Mark whether a function of @JavaMirror interface has default implementation on java side.
     * W: Parser.
     * R: Sema.
     */
    JAVA_HAS_DEFAULT,
    
    /**
     * Mark whether a class is a wrapper synthetic class generated for every mirror interface and abstract class.
     * W: Parser, Sema.
     * R: Sema.
     */
    JAVA_MIRROR_SYNTHETIC_WRAPPER,
    /**
     * Mark whether a class is an Objective-C mirror (binding for a Objective-C class).
     * W: Parser.
     * R: Sema.
     */
    OBJ_C_MIRROR,
    /**
     * Mark whether a class is a successor of an Objective-C mirror (direct or indirect child of an Objective-C class).
     * W: Parser.
     * R: Sema.
     */
    OBJ_C_MIRROR_SUBTYPE,
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
     * W: Parser
     * R: Sema.
     */
    JAVA_CJ_MAPPING,

    /**
     * Mark whether a pure cangjie decl is mapped to use by Objective-C side.
     * W: Parser
     * R: Sema.
     */
    OBJ_C_CJ_MAPPING,

    /**
     * Mark whether a class decl is an interface-forward class.
     * A forward class is used to forward the method call to Java side.
     * W: Sema.
     * R: Sema.
     */
    CJ_MIRROR_JAVA_INTERFACE_FWD,

    /**
     * Mark whether a node is a desugared mirror field decl.
     * Usually the node is a prop decl.
     * W: Parser.
     * R: Sema.
     */
    DESUGARED_MIRROR_FIELD,

    /**
     * Mark whether a node is a special flag, which marks the class instance as initialized.
     * Necessary for finalizer implementation on CHIR.
     * W: Sema.
     * R: AST2CHIR.
     */
    HAS_INITED_FIELD,

    /**
     * Mark whether a class is a wrapper synthetic class generated for every mirror interface.
     * W: Parser.
     * R: Sema.
     */
    OBJ_C_MIRROR_SYNTHETIC_WRAPPER,

    /**
     * Mark whether a function in interface-forward class is a default implementation of cj-mapping interface.
     * W: Sema.
     * R: Sema.
     */
    CJ_MIRROR_JAVA_INTERFACE_DEFAULT,

    /**
     * Mark whether a class decl is an interface-forward class.
     * A forward class is used to forward the method call to ObjC side.
     * W: Sema.
     * R: Sema.
     */
    CJ_MIRROR_OBJC_INTERFACE_FWD,

    AST_ATTR_END,
};

static const std::unordered_map<AST::Attribute, std::string> ATTR2STR{
    {AST::Attribute::IN_REFERENCE_CYCLE, "IN_REFERENCE_CYCLE"},
    {AST::Attribute::UNREACHABLE, "UNREACHABLE"},
    {AST::Attribute::IMPLICIT_USED, "IMPLICIT_USED"},
    {AST::Attribute::IMPORTED, "IMPORTED"},
    {AST::Attribute::GLOBAL, "GLOBAL"},
    {AST::Attribute::INITIALIZED, "INITIALIZED"},
    {AST::Attribute::COMPILER_ADD, "COMPILER_ADD"},
    {AST::Attribute::GENERIC, "GENERIC"},
    {AST::Attribute::DEFAULT, "DEFAULT"},
    {AST::Attribute::STATIC, "STATIC"},
    {AST::Attribute::PUBLIC, "PUBLIC"},
    {AST::Attribute::PRIVATE, "PRIVATE"},
    {AST::Attribute::PROTECTED, "PROTECTED"},
    {AST::Attribute::EXTERNAL, "EXTERNAL"},
    {AST::Attribute::INTERNAL, "INTERNAL"},
    {AST::Attribute::OVERRIDE, "OVERRIDE"},
    {AST::Attribute::REDEF, "REDEF"},
    {AST::Attribute::ABSTRACT, "ABSTRACT"},
    {AST::Attribute::SEALED, "SEALED"},
    {AST::Attribute::OPEN, "OPEN"},
    {AST::Attribute::OPERATOR, "OPERATOR"},
    {AST::Attribute::FOREIGN, "FOREIGN"},
    {AST::Attribute::UNSAFE, "UNSAFE"},
    {AST::Attribute::MUT, "MUT"},
    {AST::Attribute::HAS_BROKEN, "HAS_BROKEN"},
    {AST::Attribute::IN_STRUCT, "IN_STRUCT"},
    {AST::Attribute::IN_EXTEND, "IN_EXTEND"},
    {AST::Attribute::IN_ENUM, "IN_ENUM"},
    {AST::Attribute::IN_CLASSLIKE, "IN_CLASSLIKE"},
    {AST::Attribute::IN_MACRO, "IN_MACRO"},
    {AST::Attribute::PRIMARY_CONSTRUCTOR, "PRIMARY_CONSTRUCTOR"},
    {AST::Attribute::CONSTRUCTOR, "CONSTRUCTOR"},
    {AST::Attribute::ENUM_CONSTRUCTOR, "ENUM_CONSTRUCTOR"},
    {AST::Attribute::FINALIZER, "FINALIZER"},
    {AST::Attribute::IS_CLONED_SOURCE_CODE, "IS_CLONED_SOURCE_CODE"},
    {AST::Attribute::IS_CAPTURE, "IS_CAPTURE"},
    {AST::Attribute::IN_CORE, "IN_CORE"},
    {AST::Attribute::NEED_AUTO_BOX, "NEED_AUTO_BOX"},
    {AST::Attribute::MACRO_EXPANDED_NODE, "MACRO_EXPANDED_NODE"},
    {AST::Attribute::MACRO_FUNC, "MACRO_FUNC"},
    {AST::Attribute::MACRO_INVOKE_FUNC, "MACRO_INVOKE_FUNC"},
    {AST::Attribute::MACRO_INVOKE_BODY, "MACRO_INVOKE_BODY"},
    {AST::Attribute::LEFT_VALUE, "LEFT_VALUE"},
    {AST::Attribute::C, "C"},
    {AST::Attribute::SRC_IMPORTED, "SRC_IMPORTED"},
    {AST::Attribute::JAVA_APP, "JAVA_APP"},
    {AST::Attribute::JAVA_EXT, "JAVA_EXT"},
    {AST::Attribute::STD_CALL, "STD_CALL"},
    {AST::Attribute::NO_MANGLE, "NO_MANGLE"},
    {AST::Attribute::INITIALIZATION_CHECKED, "INITIALIZATION_CHECKED"},
    {AST::Attribute::IS_BROKEN, "IS_BROKEN"},
    {AST::Attribute::GENERIC_INSTANTIATED, "GENERIC_INSTANTIATED"},
    {AST::Attribute::HAS_INITIAL, "HAS_INITIAL"},
    {AST::Attribute::IS_VOLATILE, "IS_VOLATILE"},
    {AST::Attribute::NUMERIC_OVERFLOW, "NUMERIC_OVERFLOW"},
    {AST::Attribute::INTRINSIC, "INTRINSIC"},
    {AST::Attribute::TOOL_ADD, "TOOL_ADD"},
    {AST::Attribute::IS_CHECK_VISITED, "IS_CHECK_VISITED"},
    {AST::Attribute::INCRE_COMPILE, "INCRE_COMPILE"},
    {AST::Attribute::SIDE_EFFECT, "SIDE_EFFECT"},
    {AST::Attribute::IMPLICIT_ADD, "IMPLICIT_ADD"},
    {AST::Attribute::MAIN_ENTRY, "MAIN_ENTRY"},
    {AST::Attribute::GENERATED_TO_MOCK, "GENERATED_TO_MOCK"},
    {AST::Attribute::IS_OUTERMOST, "IS_OUTERMOST"},
    {AST::Attribute::IS_ANNOTATION, "IS_ANNOTATION"},
    {AST::Attribute::NO_REFLECT_INFO, "NO_REFLECT_INFO"},
    {AST::Attribute::MOCK_SUPPORTED, "MOCK_SUPPORTED"},
    {AST::Attribute::OPEN_TO_MOCK, "OPEN_TO_MOCK"},
    {AST::Attribute::INTERFACE_IMPL, "INTERFACE_IMPL"},
    {AST::Attribute::FOR_TEST, "FOR_TEST"},
    {AST::Attribute::CONTAINS_MOCK_CREATION_CALL, "CONTAINS_MOCK_CREATION_CALL"},
    {AST::Attribute::COMMON, "COMMON"},
    {AST::Attribute::FROM_COMMON_PART, "FROM_COMMON_PART"},
    {AST::Attribute::COMMON_NON_EXHAUSTIVE, "COMMON_NON_EXHAUSTIVE"},
    {AST::Attribute::PLATFORM, "PLATFORM"},
    {AST::Attribute::COMMON_WITH_DEFAULT, "COMMON_WITH_DEFAULT"},
    {AST::Attribute::JAVA_MIRROR, "JAVA_MIRROR"},
    {AST::Attribute::JAVA_MIRROR_SUBTYPE, "JAVA_MIRROR_SUBTYPE"},
    {AST::Attribute::JAVA_HAS_DEFAULT, "JAVA_HAS_DEFAULT"},
    {AST::Attribute::JAVA_MIRROR_SYNTHETIC_WRAPPER, "JAVA_MIRROR_SYNTHETIC_WRAPPER"},
    {AST::Attribute::OBJ_C_MIRROR, "OBJ_C_MIRROR"},
    {AST::Attribute::OBJ_C_MIRROR_SUBTYPE, "OBJ_C_MIRROR_SUBTYPE"},
    {AST::Attribute::OBJ_C_INIT, "OBJ_C_INIT"},
    {AST::Attribute::OBJ_C_OPTIONAL, "OBJ_C_OPTIONAL"},
    {AST::Attribute::JAVA_CJ_MAPPING, "JAVA_CJ_MAPPING"},
    {AST::Attribute::OBJ_C_CJ_MAPPING, "OBJ_C_CJ_MAPPING"},
    {AST::Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD, "CJ_MIRROR_JAVA_INTERFACE_FWD"},
    {AST::Attribute::DESUGARED_MIRROR_FIELD, "DESUGARED_MIRROR_FIELD"},
    {AST::Attribute::HAS_INITED_FIELD, "HAS_INITED_FIELD"},
    {AST::Attribute::OBJ_C_MIRROR_SYNTHETIC_WRAPPER, "OBJ_C_MIRROR_SYNTHETIC_WRAPPER"},
    {AST::Attribute::CJ_MIRROR_JAVA_INTERFACE_DEFAULT, "CJ_MIRROR_JAVA_INTERFACE_DEFAULT"},
    {AST::Attribute::CJ_MIRROR_OBJC_INTERFACE_FWD, "CJ_MIRROR_OBJC_INTERFACE_FWD"},
    {AST::Attribute::AST_ATTR_END, "AST_ATTR_END"},
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

    std::vector<AttrSizeType> GetAllIdxOfAttr() const
    {
        std::vector<AttrSizeType> enableAttrIdxs;
        for (auto attr : attributes) {
            for (AttrSizeType i = 0; i < ATTR_SIZE; ++i) {
                if (!attr[i]) {
                    continue;
                }
                enableAttrIdxs.emplace_back(i);
            }
        }
        return enableAttrIdxs;
    }

    std::string ToString() const
    {
        if (ATTR2STR.size() != static_cast<size_t>(AST::Attribute::AST_ATTR_END) + 1) {
            return "ATTR2STR has invalid mapping(" + std::to_string(ATTR2STR.size()) +
                "!=" + std::to_string(static_cast<size_t>(AST::Attribute::AST_ATTR_END) + 1) + ")";
        }
        std::vector<AttrSizeType> enableAttrIdxs = GetAllIdxOfAttr();
        std::stringstream ret;
        ret << "[";
        for (auto i : enableAttrIdxs) {
            ret << ATTR2STR.at(static_cast<AST::Attribute>(i));
            if (i != enableAttrIdxs.back()) {
                ret << ", ";
            }
        }
        ret << "]";
        return ret.str();
    }

private:
    static constexpr size_t attrSetSize = static_cast<size_t>(Attribute::AST_ATTR_END) / ATTR_SIZE + 1;
    std::vector<std::bitset<ATTR_SIZE>> attributes;
};
} // namespace Cangjie::AST

#endif
