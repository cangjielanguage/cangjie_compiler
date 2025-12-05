// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements class for objc code generation.
 */

#include <iostream>
#include <set>
#include "NativeFFI/ObjC/Utils/ASTFactory.h"
#include "NativeFFI/Utils.h"
#include "cangjie/Utils/FileUtil.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "cangjie/Sema/TypeManager.h"
#include "ObjCGenerator.h"


namespace {
constexpr auto INTERFACE_KEYWORD = "@interface";
constexpr auto IMPORT_KEYWORD = "#import";
constexpr auto PROPERTY_KEYWORD = "@property";
constexpr auto END_KEYWORD = "@end";
constexpr auto STRUCT_KEYWORD = "struct";
constexpr auto RTIME_PARAM_KEYWORD = "RuntimeParam";
constexpr auto IMPL_KEYWORD = "@implementation";
constexpr auto RETURN_KEYWORD = "return";
constexpr auto SUPER_KEYWORD = "super";
constexpr auto CLASS_KEYWORD = "class";
constexpr auto CLASS_FWD_KEYWORD = "@class";
constexpr auto PROTOCOL_KEYWORD = "@protocol";
constexpr auto SETTER_KEYWORD = "setter=";
constexpr auto GETTER_KEYWORD = "getter=";

constexpr auto FOUNDATION_IMPORT = "<Foundation/Foundation.h>";
constexpr auto STDDEF_IMPORT = "<stddef.h>";
constexpr auto CJ_IMPORT = "\"Cangjie.h\"";
constexpr auto DLFCN_IMPORT = "<dlfcn.h>";
constexpr auto STDLIB_IMPORT = "<stdlib.h>";

constexpr auto READWRITE_MODIFIER = "readwrite";
constexpr auto READONLY_MODIFIER = "readonly";
constexpr auto STATIC_FUNC_MODIFIER = "+";
constexpr auto INSTANCE_MODIFIER = "-";
constexpr auto STATIC_MODIFIER = "static";
constexpr auto FINAL_MODIFIER = "__attribute__((objc_subclassing_restricted))";

constexpr auto VOID_TYPE = "void";
constexpr auto BOOL_TYPE = "bool";
constexpr auto VOID_POINTER_TYPE = "void*";
constexpr auto ID_TYPE = "id";
constexpr auto UNSUPPORTED_TYPE = "UNSUPPORTED_TYPE";
constexpr auto INT64_T = "int64_t";

constexpr auto SELF_WEAKLINK_NAME = "$registryId";
constexpr auto INITED_FROM_OBJC_NAME = "$initedFromObjC";
constexpr auto SELF_NAME = "self";
constexpr auto SETTER_PARAM_NAME = "value";
constexpr auto REGISTRY_ID_PARAM_NAME = "registryId";
constexpr auto TMP_REG_ID = "regId";

constexpr auto INITIALIZE_FUNC_NAME = "initialize";
constexpr auto INIT_FUNC_NAME = "init";
constexpr auto RETAIN_FUNC_NAME = "retain";
constexpr auto DELETE_FUNC_NAME = "deleteCJObject";
constexpr auto DEALLOC_FUNC_NAME = "dealloc";
constexpr auto INIT_CJ_RUNTIME_NAME = "InitCJRuntime";
constexpr auto INIT_WITH_REGISTRY_ID_NAME = "initWithRegistryId";
constexpr auto INIT_WITH_REGISTRY_ID_SIGNATURE = "- (id)initWithRegistryId:(int64_t)registryId";
constexpr auto REINIT_WITH_REGISTRY_ID_SIGNATURE = "- (id)reinitWithRegistryId:(int64_t)registryId";
constexpr auto RELEASE_SIGNATURE = "- (oneway void)release";
constexpr auto NSLOG_FUNC_NAME = "NSLog";
constexpr auto EXIT_FUNC_NAME = "exit";
constexpr auto DLOPEN_FUNC_NAME = "dlopen";
constexpr auto DLERROR_FUNC_NAME = "dlerror";
constexpr auto DLSYM_FUNC_NAME = "dlsym";
constexpr auto LOAD_LIB_FUNC_NAME = "LoadCJLibraryWithInit";
constexpr auto CAST_TO_VOID_PTR = "(__bridge void*)";
constexpr auto CAST_TO_VOID_PTR_RETAINED = "(__bridge_retained void*)";
constexpr auto CAST_TO_VOID_PTR_UNSAFE = "(void*)";

constexpr auto CJ_DLL_HANLDE = "CJWorldDLHandle";
constexpr auto CJ_RTIME_PARAMS = "defaultCJRuntimeParams";
constexpr auto CJ_RTIME_PARAMS_LOG_LEVEL = "defaultCJRuntimeParams.logParam.logLevel";
constexpr auto RTLOG_ERROR = "RTLOG_ERROR";

constexpr auto EQ_OP = "=";
constexpr auto EQ_CHECK_OP = "==";
constexpr auto NOT_EQ_OP = "!=";

constexpr auto E_OK = "E_OK";
constexpr auto RTLD_LAZY = "RTLD_LAZY";
constexpr auto NULL_KW = "NULL";
constexpr auto FAIL_INIT_CJ_RT_MSG = "@\"ERROR: Failed to initialize Cangjie runtime\"";
constexpr auto FAIL_INIT_CJ_LIB = "@\"ERROR: Failed to init cjworld library \"";
constexpr auto FAIL_OPEN_CJ_LIB = "@\"ERROR: Failed to open cjworld library \"";
constexpr auto FAIL_FIND_SYM_1 = "@\"ERROR: Failed to find ";
constexpr auto FAIL_FIND_SYM_2 = " symbol in cjworld\"";
constexpr auto FAIL_CALL_UNINIT_CTOR = "[self doesNotRecognizeSelector:_cmd];";
constexpr auto STRING_FORMAT = "@\"%s\"";

constexpr auto INDENT_SIZE = 4;

template <typename... Args> inline std::string Join(Args&&... args)
{
    std::stringstream ss;
    ((ss << args), ...);
    return ss.str();
}

template <typename T>
std::string JoinVec(const std::vector<T>& vec, std::function<std::string(const T&)> trans,
    const std::string& sep = ", ", const std::string& pre = "", const std::string& suf = "", bool force = true)
{
    std::stringstream ss;
    auto it = vec.cbegin();
    if (it == vec.end()) {
        if (!force) {
            return "";
        }
        ss << pre << suf;
        return ss.str();
    }
    ss << pre;
    do {
        ss << trans(*it);
        ++it;
        if (it != vec.cend()) {
            ss << sep;
        }
    } while (it != vec.cend());
    ss << suf;
    return ss.str();
}
} // namespace

namespace Cangjie::Interop::ObjC {

using namespace Cangjie;
using namespace AST;
using namespace Native::FFI;
using std::string;

std::string ToDecl(ObjCFunctionType fType)
{
    if (fType == ObjCFunctionType::STATIC) {
        return STATIC_FUNC_MODIFIER;
    }

    return INSTANCE_MODIFIER;
}

ObjCGenerator::ObjCGenerator(InteropContext& ctx, Ptr<Decl> declArg, const std::string& outputFilePath,
    const std::string& cjLibOutputPath, InteropType interopType)
    : outputFilePath(outputFilePath),
      cjLibOutputPath(cjLibOutputPath),
      decl(declArg),
      ctx(ctx),
      interopType(interopType)
{
}

/*
    Main access point to translation of CJ class to Objective C. Generates two files - .h and .m

    Example of header, .h:
    #import <Foundation/Foundation.h>
            ...
    @interface A : M
    + (void)initialize;
    - (id)init;
    @property (readwrite) UIntNative obj;
    - (void)goo;
            ...
    @end

    Example of source, .m:
    #import "A.h"
            ...
    static void (*CJImpl_ObjC_A_goo)(size_t) = NULL;
            ...
    @implementation A
    + (void)initialize {
            ...
    }
            ...
    - (void)goo {
        CJImpl_ObjC_A_goo(self.obj);
    }
            ...
    @end
*/
void ObjCGenerator::Generate()
{
    if (this->interopType == InteropType::CJ_Mapping_Interface) {
        GenerateProtocolDecl();
        return;
    }

    const auto objCDeclName = ctx.nameGenerator.GetObjCDeclName(*decl);
    GenerateImports(objCDeclName);
    GenerateForwardDeclarations();
    GenerateExternalDeclarations4CJMapping();
    GenerateStaticReferences();

    GenerateInterfaceDecl();

    AddConstructors();
    
    GenerateInitializer(objCDeclName);

    AddProperties();
    AddMethods();
    GenerateDeleteObject();
    GenerateDealloc();

    AddWithIndent(END_KEYWORD, GenerationTarget::BOTH);
    WriteToFile();
}

/* ------------------indent handling-------------------- */
void ObjCGenerator::OpenBlock()
{ // needed only in source file generation
    resSource += " {";
    currentBlockIndent += INDENT_SIZE;
}
void ObjCGenerator::CloseBlock(bool newLineBefore = false, bool newLineAfter = false)
{
    currentBlockIndent -= INDENT_SIZE;
    resSource += (newLineBefore ? "\n" : "") + std::string(currentBlockIndent, ' ') + "}" + (newLineAfter ? "\n" : "");
}
void ObjCGenerator::AddWithIndent(const std::string& s, const GenerationTarget target, const OptionalBlockOp bOp)
{
    if (target == GenerationTarget::HEADER || target == GenerationTarget::BOTH) {
        res += s;
        if (bOp == OptionalBlockOp::OPEN) {
            res += ";";
        }
        res += "\n";
    }
    if (target == GenerationTarget::SOURCE || target == GenerationTarget::BOTH) {
        resSource += std::string(currentBlockIndent, ' ');
        resSource += s;
        if (bOp == OptionalBlockOp::OPEN) {
            OpenBlock();
        }
        if (bOp == OptionalBlockOp::CLOSE) {
            CloseBlock(true);
        }
        resSource += "\n";
    }
}
/* ------------------------------------------------------ */

/*
 *   return statement;
 */
std::string ObjCGenerator::GenerateReturn(const std::string& statement) const
{
    return string(RETURN_KEYWORD) + " " + statement + ";";
}

/*
 *   lhs = rhs
 */
std::string ObjCGenerator::GenerateAssignment(const std::string& lhs, const std::string& rhs) const
{
    return lhs + " = " + rhs;
}

/*
 *   if (lhs op rhs)
 */
std::string ObjCGenerator::GenerateIfStatement(const std::string& lhs, const std::string& rhs,
    const std::string& op) const
{
    return "if (" + lhs + " " + op + " " + rhs + ")";
}

/*
 *   objective c style function call
 *   [lhs rhs]
 */
std::string ObjCGenerator::GenerateObjCCall(const std::string& lhs, const std::string& rhs) const
{
    return "[" + lhs + " " + rhs + "]";
}

/*
 *   funcName(arg1, arg2, ... argN)
 */
std::string ObjCGenerator::GenerateCCall(const std::string& funcName, const std::vector<std::string> args) const
{
    std::string result = funcName + "(";
    if (args.size() != 0) {
        for (size_t i = 0; i < args.size(); i++) {
            result += args.at(i);
            if (i != args.size() - 1) {
                result += ", ";
            }
        }
    }
    result += ")";
    return result;
}

std::string ObjCGenerator::WrapperCallByInitForCJMappingReturn(const Ty& retTy, const std::string& nativeCall) const
{
    std::string result =
        std::string("[[") + retTy.name + " alloc]" + INIT_WITH_REGISTRY_ID_NAME + ": " + nativeCall + "]";
    return result;
}

/*
 *  return CJImpl_ObjC_cjworld_A_goo(arg1, arg2, ... arg3);
 */
std::string ObjCGenerator::GenerateDefaultFunctionImplementation(
    const std::string& name, const Ty& retTy, const ArgsList args, [[maybe_unused]] const ObjCFunctionType type) const
{
    std::string result = retTy.IsUnit() ? "" : RETURN_KEYWORD;
    result += " ";
    std::string nativeCall = "";
    if (ctx.typeMapper.IsObjCObjectType(retTy) && !ctx.typeMapper.IsObjCCJMapping(retTy)) {
        nativeCall += "(__bridge " + ctx.typeMapper.Cj2ObjCForObjC(retTy) + ")";
    }
    nativeCall += name + "(";
    for (size_t i = 0; i < args.size(); i++) {
        nativeCall += args.at(i).second;
        if (i != args.size() - 1) {
            nativeCall += ", ";
        }
    }
    nativeCall += ")";
    result +=
        ctx.typeMapper.IsObjCCJMapping(retTy) ? WrapperCallByInitForCJMappingReturn(retTy, nativeCall) : nativeCall;
    result += ";";
    return result;
}

/*
 *  - (returnType)name
 */
std::string ObjCGenerator::GenerateFunctionDeclaration(
    const ObjCFunctionType type, const std::string& returnType, const std::string& name) const
{
    std::string result = "";
    result += ToDecl(type) + " ";
    result += "(" + returnType + ")";
    result += name;
    return result;
}

/**
 * @property (<class>, readwrite, getter=name, <setter=setName:>) type name;
 * Note: <...> are optional.
 */
std::string ObjCGenerator::GeneratePropertyDeclaration(
    const ObjCFunctionType staticType, const std::string& mode, const std::string& type, const std::string& name,
    const std::string& getterName, const std::string& setterName) const
{
    std::string result = "";
    if (name == "" || type == "") {
        return result;
    }

    result += PROPERTY_KEYWORD;
    result += " (";
    if (staticType == ObjCFunctionType::STATIC) {
        result += CLASS_KEYWORD;
        result += ", ";
    }
    if (mode != "") {
        result += mode;
    }

    if (name != SELF_WEAKLINK_NAME) {
        result += ", ";
        result += GETTER_KEYWORD + getterName;
        if (mode == READWRITE_MODIFIER) {
            result += ", ";
            result += SETTER_KEYWORD + setterName;
        }
    }

    result += ") ";
    result += type + " ";
    result += name + ";";
    return result;
}

/*
 *  #import name
 */
std::string ObjCGenerator::GenerateImport(const std::string& name)
{
    return string(IMPORT_KEYWORD) + " " + name;
}

void ObjCGenerator::GenerateImports(const std::string& objCDeclName)
{
    AddWithIndent(GenerateImport(FOUNDATION_IMPORT), GenerationTarget::HEADER);
    AddWithIndent(GenerateImport(STDDEF_IMPORT), GenerationTarget::HEADER);
    AddWithIndent(GenerateImport("\"" + objCDeclName + ".h\""), GenerationTarget::SOURCE);
    AddWithIndent(GenerateImport(CJ_IMPORT), GenerationTarget::SOURCE);
    AddWithIndent(GenerateImport(DLFCN_IMPORT), GenerationTarget::SOURCE);
    AddWithIndent(GenerateImport(STDLIB_IMPORT), GenerationTarget::SOURCE);
}

void ObjCGenerator::GenerateForwardDeclarations()
{
    std::set<Ptr<Decl>> dependencies;
    auto walker = [this, &dependencies](Ptr<Ty> ty, auto& self) -> void {
        if (ctx.typeMapper.IsObjCObjectType(*ty)) {
            // ObjCId is `id` representative which is builtin type for Objective-C
            if (TypeMapper::IsObjCId(*ty)) {
                return;
            }

            if (!ty->IsCoreOptionType()) {
                dependencies.insert(Ty::GetDeclOfTy(ty));
            }
        }
        for (auto&& typeArg : ty->typeArgs) {
            self(typeArg, self);
        }
    };
    for (auto&& member : decl->GetMemberDecls()) {
        walker(member->ty, walker);
    }
    // remove our own declaration from the set
    dependencies.erase(decl);
    for (auto&& dep : dependencies) {
        std::string keyword;
        switch (dep->astKind) {
            case ASTKind::STRUCT_DECL:
            case ASTKind::ENUM_DECL:
            case ASTKind::CLASS_DECL:
                keyword = CLASS_FWD_KEYWORD;
                break;
            case ASTKind::INTERFACE_DECL:
                keyword = PROTOCOL_KEYWORD;
                break;
            default:
                CJC_ABORT();
        }
        AddWithIndent(GenerateImport("\"" + dep->identifier.Val() + ".h\""), GenerationTarget::SOURCE);
        AddWithIndent(keyword + " " +  dep->identifier.Val(), GenerationTarget::HEADER, OptionalBlockOp::OPEN);
    }
}

void ObjCGenerator::GenerateStaticReferences()
{
    for (auto& declPtr : ctx.genDecls) {
        // Must be filtered out earlier
        if (declPtr->curFile != decl->curFile || !declPtr->TestAttr(Attribute::C) || declPtr->TestAttr(Attribute::FOREIGN)) {
            continue;
        }

        if (declPtr->astKind == ASTKind::FUNC_DECL) {
            const auto& funcDecl = *StaticAs<ASTKind::FUNC_DECL>(declPtr.get());
            CJC_ASSERT(funcDecl.ty->IsFunc());
            auto& retTy = *StaticCast<const FuncTy*>(funcDecl.ty)->retTy;
            const auto retType = ctx.typeMapper.IsObjCObjectType(retTy) || ctx.typeMapper.IsObjCBlock(retTy)
                ? VOID_POINTER_TYPE
                : ctx.typeMapper.Cj2ObjCForObjC(retTy);
            std::string argTypes = "";
            for (auto& arg: funcDecl.funcBody->paramLists[0]->params) {
                argTypes += ctx.typeMapper.Cj2ObjCForObjC(*arg->ty) + ",";
            }
            // trim unnecessary ","
            argTypes.erase(std::find_if(argTypes.rbegin(), argTypes.rend(), [](auto c) { return c != ','; }).base(),
                argTypes.end());
            AddWithIndent(
                GenerateStaticFunctionReference(funcDecl.identifier, retType, argTypes), GenerationTarget::SOURCE);
        }
    }
    // static variable
    AddWithIndent(GenerateStaticReference(CJ_DLL_HANLDE, string(VOID_TYPE) + "*", NULL_KW), GenerationTarget::SOURCE);
    AddWithIndent(GenerateStaticReference(CJ_RTIME_PARAMS, string(STRUCT_KEYWORD) + " " + RTIME_PARAM_KEYWORD, "{0}"),
        GenerationTarget::SOURCE);
}

/*
 *  static type (*CJImpl_ObjC_A_funcName)(arg1Type, ... argNType) = NULL;
 */
std::string ObjCGenerator::GenerateStaticFunctionReference(
    const std::string& funcName, const std::string& retType, const std::string& argTypes) const
{
    std::string result = STATIC_MODIFIER;
    result += " ";
    result += retType + " (*";
    result += funcName + ")";
    result += "(" + argTypes + ")";
    result += " = " + string(NULL_KW) + ";";
    return result;
}

/*
 *  static type name = defaultValue;
 */
std::string ObjCGenerator::GenerateStaticReference(
    const std::string& name, const std::string& type, const std::string& defaultValue) const
{
    std::string result = STATIC_MODIFIER;
    result += " ";
    result += type + " ";
    result += name;
    if (defaultValue != "") {
        result += " = " + defaultValue + ";";
    }
    return result;
}

/*
 * Generate code to load baked and dynamic functions from dylib
 */
void ObjCGenerator::GenerateFunctionSymbolsInitialization()
{
    for (OwnedPtr<Decl>& declPtr : ctx.genDecls) {
        // Must be filtered out earlier
        if (declPtr->curFile != decl->curFile || !declPtr->TestAttr(Attribute::C) || declPtr->TestAttr(Attribute::FOREIGN)) {
            continue;
        }
        if (declPtr->astKind == ASTKind::FUNC_DECL) {
            const FuncDecl& funcDecl = *StaticAs<ASTKind::FUNC_DECL>(declPtr.get());
            GenerateFunctionSymInit(funcDecl.identifier);
        }
    }
}

/*
 *  if ((CJImpl_ObjC_A_name = dlsym(CJWorldDLHandle, "CJImpl_ObjC_A_name")) == NULL) {
 *      NSLog(@"ERROR: Failed to find CJImpl_ObjC_A_name symbol in cjworld");
 *      exit(1);
 *  }
 */
void ObjCGenerator::GenerateFunctionSymInit(const std::string& fName)
{
    AddWithIndent(GenerateIfStatement(
        "(" +
        GenerateAssignment(fName,
            GenerateCCall(DLSYM_FUNC_NAME,
                std::vector<std::string>{CJ_DLL_HANLDE, "\"" + fName + "\""})) +
        ")",
        NULL_KW, EQ_CHECK_OP),
        GenerationTarget::SOURCE, OptionalBlockOp::OPEN);
    AddWithIndent(
        GenerateCCall(NSLOG_FUNC_NAME, std::vector<std::string>{FAIL_FIND_SYM_1 + fName + FAIL_FIND_SYM_2}) + ";",
        GenerationTarget::SOURCE);
    AddWithIndent(GenerateCCall(EXIT_FUNC_NAME, std::vector<std::string>{"1"}) + ";",
        GenerationTarget::SOURCE);

    CloseBlock(false, true);
}

/*
 *  HEADER: @interface A : M
 *  SOURCE: @implementation A
 */
void ObjCGenerator::GenerateInterfaceDecl()
{
    std::string resultH = "";
    std::string resultS = "";

    ClassDecl* classDecl = dynamic_cast<ClassDecl*>(decl.get());
    Ptr<ClassDecl> superClassPtr = classDecl ? classDecl->GetSuperClassDecl() : Ptr<ClassDecl>(nullptr);
    bool isClassInheritedFromClass = superClassPtr && superClassPtr->identifier.Val() != Cangjie::OBJECT_NAME;
    if (isClassInheritedFromClass) {
        AddWithIndent(GenerateImport("\"" + superClassPtr->identifier.Val() + ".h\""), GenerationTarget::HEADER);
    }
    if (interopType == InteropType::CJ_Mapping && ctx.typeMapper.IsOneWayMapping(*decl) && !decl->IsOpen()) {
        AddWithIndent(FINAL_MODIFIER, GenerationTarget::HEADER);
    }
    auto objCDeclName = ctx.nameGenerator.GetObjCDeclName(*decl);
    resultH += INTERFACE_KEYWORD;
    resultH += " ";
    resultH += objCDeclName;

    if (isClassInheritedFromClass) {
        resultH += " : ";
        resultH += superClassPtr->identifier.Val();
    } else {
        resultH += " : ";
        resultH += Cangjie::OCOBJECT_NAME;
    }

    AddWithIndent(resultH, GenerationTarget::HEADER);

    resultS += IMPL_KEYWORD;
    resultS += " ";
    resultS += objCDeclName;
    AddWithIndent(resultS, GenerationTarget::SOURCE);
}

/*
 *  DECLARATION: :(arg1Type)arg1:(arg2Type)arg2:...(argNType)argN
 *  STATIC_REF: (arg1Type, arg2Type, ...argNType)
 */
std::string ObjCGenerator::GenerateFuncParamLists(
    const std::vector<OwnedPtr<FuncParamList>>& paramLists,
    const std::vector<std::string>& selectorComponents,
    FunctionListFormat format, const ObjCFunctionType type,
    bool hasForeignNameAnno)
{
    auto componentIterator = std::begin(selectorComponents);
    // skip function name
    componentIterator++;
    std::string genParams = format == FunctionListFormat::DECLARATION ? "" : "(";
    if (paramLists.empty() || !paramLists[0]) {
        return "";
    }
    for (size_t i = 0; i < paramLists[0]->params.size(); i++) {
        OwnedPtr<FuncParam>& cur = paramLists[0]->params[i];
        switch (format) {
            case FunctionListFormat::DECLARATION:
                if (i != 0) {
                    /*
                     * for CJMapping Objective-C method signature, label shouldn't generate if not use @ForeignName
                     * as it would be more user-friendly to keep both side method signatures the same
                     */
                    auto name = hasForeignNameAnno ? *componentIterator++ : "";
                    genParams += name + ":"; // label
                } else {
                    genParams += ":";
                }
                genParams += "(" + MapCJTypeToObjCType(cur) + ")";
                genParams += cur->identifier.Val();
                if (i != paramLists[0]->params.size() - 1) {
                    genParams += " ";
                }
                break;
            case FunctionListFormat::STATIC_REF:
                if (paramLists[0]->params.size() == 0 && type != ObjCFunctionType::STATIC) {
                    genParams += VOID_POINTER_TYPE;
                }
                genParams += ")";
                break;
            case FunctionListFormat::CANGJIE_DECL:
                genParams += cur->identifier.Val() + ": " + Ty::ToString(cur->type->ty);
                if (i != paramLists[0]->params.size() - 1) {
                    genParams += ", ";
                }
                break;
            default:
                break;
        }
    }
    if (format == FunctionListFormat::STATIC_REF || format == FunctionListFormat::CANGJIE_DECL) {
        if (paramLists[0]->params.size() == 0 && type != ObjCFunctionType::STATIC) {
            genParams += VOID_POINTER_TYPE;
        }
        genParams += ")";
    }

    return genParams;
}

std::string ObjCGenerator::GenerateSetterParamLists(const std::string& type) const
{
    return "(" + type + ")" + SETTER_PARAM_NAME;
}

bool ObjCGenerator::SkipSetterForValueTypeDecl(Decl& declArg) const
{
    return interopType == InteropType::CJ_Mapping && DynamicCast<StructTy*>(declArg.ty.get()) != nullptr;
}

/*
 *  Iterate over properties and generate declarations
 */
void ObjCGenerator::AddProperties()
{
    auto registryIdType = CreateType(TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64));
    AddWithIndent(
        GeneratePropertyDeclaration(ObjCFunctionType::INSTANCE, READWRITE_MODIFIER, INT64_T, SELF_WEAKLINK_NAME, SELF_WEAKLINK_NAME, NameGenerator::MakeSetterName(SELF_WEAKLINK_NAME))
    );

    for (OwnedPtr<Decl>& declPtr : decl->GetMemberDecls()) {
        CJC_NULLPTR_CHECK(declPtr);
        if (declPtr->astKind != ASTKind::VAR_DECL && declPtr->astKind != ASTKind::PROP_DECL) {
            continue;
        }

        if (!declPtr->TestAttr(Attribute::PUBLIC)) {
            continue;
        }

        if (ctx.factory.IsGeneratedNativeHandleField(*declPtr)) {
            continue;
        }
        
        if (interopType == InteropType::CJ_Mapping && !ctx.typeMapper.IsObjCCJMappingMember(*declPtr)) {
            continue;
        }

        const auto& varDecl = *As<ASTKind::VAR_DECL>(declPtr.get());
        const auto& staticType =
            varDecl.TestAttr(Attribute::STATIC) ? ObjCFunctionType::STATIC : ObjCFunctionType::INSTANCE;
        const std::string& type = ctx.typeMapper.Cj2ObjCForObjC(*varDecl.ty);
        bool genSetter = varDecl.isVar && !SkipSetterForValueTypeDecl(*decl);
        const auto modeModifier = genSetter ? READWRITE_MODIFIER : READONLY_MODIFIER;
        const auto name = ctx.nameGenerator.GetObjCDeclName(varDecl);
        const auto getterName = ctx.nameGenerator.GetObjCGetterName(varDecl);
        const auto setterName = ctx.nameGenerator.GetObjCSetterName(varDecl);
        AddWithIndent(GeneratePropertyDeclaration(staticType, modeModifier, type, name, getterName, setterName));

        std::string getterResult = "";
        getterResult += GenerateFunctionDeclaration(staticType, type, getterName);
        ArgsList argList = ArgsList();
        if (staticType == ObjCFunctionType::INSTANCE) {
            argList.emplace_back(INT64_T, string(SELF_NAME) + "." + SELF_WEAKLINK_NAME);
        }
        AddWithIndent(getterResult, GenerationTarget::BOTH, OptionalBlockOp::OPEN);
        AddWithIndent(
            GenerateDefaultFunctionImplementation(ctx.nameGenerator.GetFieldGetterWrapperName(varDecl), *varDecl.ty,
                argList,
                staticType),
            GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);

        if (!genSetter) {
            continue;
        }

        ArgsList setterArgsList = ArgsList();
        if (staticType == ObjCFunctionType::INSTANCE) {
            setterArgsList.emplace_back(
                std::pair<std::string, std::string>(INT64_T, string(SELF_NAME) + "." + SELF_WEAKLINK_NAME));
        }
        const auto setterArg = GenerateArgumentCast(*varDecl.ty, SETTER_PARAM_NAME);
        setterArgsList.emplace_back(std::pair<std::string, std::string>(type, setterArg));

        std::string setterResult = "";
        setterResult += GenerateFunctionDeclaration(staticType, VOID_TYPE, setterName);
        setterResult += GenerateSetterParamLists(type);
        AddWithIndent(setterResult, GenerationTarget::BOTH, OptionalBlockOp::OPEN);
        AddWithIndent(
            GenerateDefaultFunctionImplementation(
                ctx.nameGenerator.GetFieldSetterWrapperName(varDecl),
                *ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT),
                setterArgsList,
                staticType),
            GenerationTarget::SOURCE,
            OptionalBlockOp::CLOSE);
    }
    // Add properties for cj mapping
    AddProperties4CJMapping();
}

void ObjCGenerator::AddCtorsForCjMappingEnum(AST::EnumDecl& enumDecl)
{
    const std::string enumName = enumDecl.identifier;
    for (auto& ctor : enumDecl.constructors) {
        std::string result = "";
        const ::std::string retType = enumName + "*";
        const ::std::string ctorName = ctor->identifier.Val();
        result += GenerateFunctionDeclaration(ObjCFunctionType::STATIC, retType, ctorName);
        std::vector<std::string> argList;
        std::string nativeFuncName = ctx.nameGenerator.GenerateInitCjObjectName(*ctor.get());

        if (ctor->astKind == ASTKind::FUNC_DECL) {
            auto funcDecl = As<ASTKind::FUNC_DECL>(ctor.get());
            const auto selectorComponents = ctx.nameGenerator.GetObjCDeclSelectorComponents(*funcDecl);
            result += GenerateFuncParamLists(funcDecl->funcBody->paramLists, selectorComponents,
                FunctionListFormat::DECLARATION, ObjCFunctionType::STATIC,
                GetForeignNameAnnotation(*funcDecl) != nullptr);
            argList = ConvertParamsListToCallableParamsString(funcDecl->funcBody->paramLists, false);
        }
        AddWithIndent(result, GenerationTarget::BOTH, OptionalBlockOp::OPEN);
        AddWithIndent(
            GenerateAssignment(string(INT64_T) + " " + string(TMP_REG_ID), GenerateCCall(nativeFuncName, argList)) +
                ";",
            GenerationTarget::SOURCE);
        AddWithIndent(GenerateReturn(WrapperCallByInitForCJMappingReturn(*enumDecl.ty, string(TMP_REG_ID))),
            GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);
    }
}

/*
 *  Generate `+ initialize`
 */
void ObjCGenerator::GenerateInitializer(const string& objCDeclName)
{
    AddWithIndent(GenerateFunctionDeclaration(ObjCFunctionType::STATIC, VOID_TYPE, INITIALIZE_FUNC_NAME),
        GenerationTarget::BOTH, OptionalBlockOp::OPEN);
    AddWithIndent(GenerateIfStatement(SELF_NAME, GenerateObjCCall(objCDeclName, CLASS_KEYWORD), EQ_CHECK_OP),
        GenerationTarget::SOURCE, OptionalBlockOp::OPEN);
    AddWithIndent(GenerateAssignment(CJ_RTIME_PARAMS_LOG_LEVEL, RTLOG_ERROR) + ";", GenerationTarget::SOURCE);
    AddWithIndent(GenerateIfStatement(
                      GenerateCCall(INIT_CJ_RUNTIME_NAME, std::vector<std::string>{"&" + string(CJ_RTIME_PARAMS)}),
                      E_OK, NOT_EQ_OP),
        GenerationTarget::SOURCE, OptionalBlockOp::OPEN);
    AddWithIndent(
        GenerateCCall(NSLOG_FUNC_NAME, std::vector<std::string>{FAIL_INIT_CJ_RT_MSG}) + ";", GenerationTarget::SOURCE);
    AddWithIndent(GenerateCCall(EXIT_FUNC_NAME, std::vector<std::string>{"1"}) + ";", GenerationTarget::SOURCE);

    CloseBlock(false, true);

    auto cjLibName = Native::FFI::GetCangjieLibName(cjLibOutputPath, decl->fullPackageName, false);
    // Crutch that solves issue when cjLibOutputPath is dir or not provided.
    if (cjLibName.find("lib", 0) != 0) {
        cjLibName = "lib" + cjLibName;
    }
    size_t extIdx = cjLibName.find_last_of(".");
    if (extIdx == std::string::npos || cjLibName.substr(extIdx) != ".dylib") {
        cjLibName += ".dylib"; // what if we compile on linux target?
    }

    cjLibName = "\"" + cjLibName + "\"";
    AddWithIndent(
        GenerateIfStatement(GenerateCCall(LOAD_LIB_FUNC_NAME, std::vector<std::string>{cjLibName}), E_OK, NOT_EQ_OP),
        GenerationTarget::SOURCE, OptionalBlockOp::OPEN);
    AddWithIndent(
        GenerateCCall(NSLOG_FUNC_NAME, std::vector<std::string>{FAIL_INIT_CJ_LIB}) + ";", GenerationTarget::SOURCE);
    AddWithIndent(GenerateCCall(EXIT_FUNC_NAME, std::vector<std::string>{"1"}) + ";", GenerationTarget::SOURCE);

    CloseBlock(false, true);

    AddWithIndent(GenerateIfStatement("(" +
                          GenerateAssignment(CJ_DLL_HANLDE,
                              GenerateCCall(DLOPEN_FUNC_NAME, std::vector<std::string>{cjLibName, RTLD_LAZY})) +
                          ")",
                      NULL_KW, EQ_CHECK_OP),
        GenerationTarget::SOURCE, OptionalBlockOp::OPEN);
    AddWithIndent(
        GenerateCCall(NSLOG_FUNC_NAME, std::vector<std::string>{FAIL_OPEN_CJ_LIB}) + ";", GenerationTarget::SOURCE);
    AddWithIndent(
        GenerateCCall(NSLOG_FUNC_NAME, std::vector<std::string>{STRING_FORMAT, GenerateCCall(DLERROR_FUNC_NAME)}) + ";",
        GenerationTarget::SOURCE);
    AddWithIndent(GenerateCCall(EXIT_FUNC_NAME, std::vector<std::string>{"1"}) + ";", GenerationTarget::SOURCE);

    CloseBlock(false, true);

    GenerateFunctionSymbolsInitialization();

    CloseBlock(false, true);
    CloseBlock(false, true);
}

void ObjCGenerator::AddConstructors()
{
    std::set<std::vector<std::string>> generatedCtors = {};
    for (OwnedPtr<Decl>& declPtr : decl->GetMemberDecls()) {
        if (!declPtr->TestAttr(Attribute::PUBLIC)) { continue; }
        if (ctx.factory.IsGeneratedMember(*declPtr.get())) {
            continue;
        }

        if (!declPtr->TestAttr(Attribute::CONSTRUCTOR)) {
            continue;
        }

        if (declPtr->astKind != ASTKind::FUNC_DECL) {
            // skip primary ctor, as it is desugared to init already
            continue;
        }

        const FuncDecl& funcDecl = *StaticAs<ASTKind::FUNC_DECL>(declPtr.get());
        if (!funcDecl.funcBody) {
            continue;
        }

        if (interopType == InteropType::CJ_Mapping && !ctx.typeMapper.IsObjCCJMappingMember(*declPtr)) {
            continue;
        }

        const auto ctor = interopType == InteropType::ObjC_Mirror
                          ? ctx.factory.GetGeneratedImplCtor(*decl, funcDecl).get()
                          : &funcDecl;
        CJC_ASSERT(ctor);
        const std::string& retType = ID_TYPE;
        const auto selectorComponents = ctx.nameGenerator.GetObjCDeclSelectorComponents(funcDecl);
        CJC_ASSERT(selectorComponents.size() > 0);
        // wrapper name MUST use generated ctor
        const auto cjWrapperName = ctx.nameGenerator.GenerateInitCjObjectName(*ctor);
        const auto staticType = ObjCFunctionType::INSTANCE;

        std::string result = "";
        result += GenerateFunctionDeclaration(staticType, retType, selectorComponents[0]);
        result += GenerateFuncParamLists(funcDecl.funcBody->paramLists, selectorComponents, FunctionListFormat::DECLARATION, staticType, GetForeignNameAnnotation(funcDecl) != nullptr);
        AddWithIndent(result, GenerationTarget::BOTH, OptionalBlockOp::OPEN);
        AddWithIndent(GenerateIfStatement(SELF_NAME, GenerateObjCCall(SUPER_KEYWORD, INIT_FUNC_NAME), EQ_OP),
            GenerationTarget::SOURCE, OptionalBlockOp::OPEN);
        AddWithIndent(GenerateAssignment(string(SELF_NAME) + "." + SELF_WEAKLINK_NAME,
            GenerateCCall(cjWrapperName, ConvertParamsListToCallableParamsString(
                funcDecl.funcBody->paramLists, interopType == InteropType::ObjC_Mirror))) + ";",
            GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);
        AddWithIndent(GenerateReturn(SELF_NAME), GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);
        if (interopType == InteropType::ObjC_Mirror) {
            generatedCtors.insert(selectorComponents);
        }
    }
    ClassDecl* classDecl = dynamic_cast<ClassDecl*>(decl.get());
    if (interopType == InteropType::ObjC_Mirror && classDecl) {
        auto ctorsToGenerate = ctx.factory.GetAllParentCtors(*classDecl);
        for (auto ctor : ctorsToGenerate) {
            // public superconstructors & non-public own constructors
            if (!ctor->TestAttr(Attribute::PUBLIC) && ctor->outerDecl != decl) {
                continue;
            }
            if (ctx.factory.IsGeneratedMember(*ctor)) {
                continue;
            }
            if (ctor->funcBody->paramLists[0]->params.size() > 1 && !ctx.nameGenerator.GetUserDefinedObjCName(*ctor)) {
                // remove when generating selectors without foreign name is implemented
                continue;
            }
            auto selectorComponents = ctx.nameGenerator.GetObjCDeclSelectorComponents(*ctor);
            if (generatedCtors.count(selectorComponents) > 0) {
                continue;
            }

            std::string result = "";
            result += GenerateFunctionDeclaration(ObjCFunctionType::INSTANCE, ID_TYPE, selectorComponents[0]);
            result += GenerateFuncParamLists(
                ctor->funcBody->paramLists, selectorComponents, FunctionListFormat::DECLARATION, ObjCFunctionType::INSTANCE,
                GetForeignNameAnnotation(*ctor) != nullptr);
            AddWithIndent(result, GenerationTarget::BOTH, OptionalBlockOp::OPEN);
            AddWithIndent(FAIL_CALL_UNINIT_CTOR, GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);
            generatedCtors.insert(selectorComponents);
        }
    }
    // Add init for cj mapping
    AddConstructors4CJMapping();
}

void ObjCGenerator::GenerateDeleteObject()
{
    if (GenerateDeleteObject4CJMapping()) {
        return;
    }
    AddWithIndent(GenerateFunctionDeclaration(ObjCFunctionType::INSTANCE, VOID_TYPE, DELETE_FUNC_NAME),
        GenerationTarget::BOTH, OptionalBlockOp::OPEN);
    AddWithIndent(GenerateDefaultFunctionImplementation(ctx.nameGenerator.GenerateDeleteCjObjectName(*decl),
                      *ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT),
                      {std::pair<std::string, std::string>(INT64_T, string(SELF_NAME) + "." + SELF_WEAKLINK_NAME)}),
        GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);
}

void ObjCGenerator::GenerateDealloc()
{
    AddWithIndent(GenerateFunctionDeclaration(ObjCFunctionType::INSTANCE, VOID_TYPE, DEALLOC_FUNC_NAME),
        GenerationTarget::BOTH, OptionalBlockOp::OPEN);
    AddWithIndent(
        GenerateObjCCall(SELF_NAME, DELETE_FUNC_NAME) + ";", GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);
}

/*
 *  Iterate over methods and generate default implementations
 */
void ObjCGenerator::AddMethods()
{
    for (OwnedPtr<Decl>& declPtr : decl->GetMemberDecls()) {
        if (ctx.factory.IsGeneratedMember(*declPtr.get())) { continue; }

        if (!declPtr->TestAttr(Attribute::PUBLIC)) { continue; }
        
        if (interopType == InteropType::CJ_Mapping &&
            (!ctx.typeMapper.IsObjCCJMappingMember(*declPtr) || declPtr->IsOpen())) {
            continue;
        }
        if (declPtr->astKind == ASTKind::FUNC_DECL &&
            !declPtr->TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::FINALIZER)) {
            const FuncDecl& funcDecl = *StaticAs<ASTKind::FUNC_DECL>(declPtr.get());
            if (funcDecl.funcBody && funcDecl.funcBody->retType) {
                auto staticType =
                    funcDecl.TestAttr(Attribute::STATIC) ? ObjCFunctionType::STATIC : ObjCFunctionType::INSTANCE;
                auto retTy = funcDecl.funcBody->retType->ty;
                const std::string& retType = MapCJTypeToObjCType(funcDecl.funcBody->retType);
                const auto selectorComponents = ctx.nameGenerator.GetObjCDeclSelectorComponents(funcDecl);

                std::string result = "";
                result += GenerateFunctionDeclaration(staticType, retType, selectorComponents[0]);
                result +=
                    GenerateFuncParamLists(funcDecl.funcBody->paramLists, selectorComponents, FunctionListFormat::DECLARATION, staticType, GetForeignNameAnnotation(funcDecl) != nullptr);
                AddWithIndent(result, GenerationTarget::BOTH, OptionalBlockOp::OPEN);
                AddWithIndent(
                    GenerateDefaultFunctionImplementation(ctx.nameGenerator.GenerateMethodWrapperName(funcDecl), *retTy,
                        ConvertParamsListToArgsList(
                            funcDecl.funcBody->paramLists, staticType == ObjCFunctionType::INSTANCE),
                        staticType),
                    GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);
            }
        }
    }
    // Add methods for cj mapping
    GenerateMethods4CJMapping();
}

ArgsList ObjCGenerator::ConvertParamsListToArgsList(
    const std::vector<OwnedPtr<FuncParamList>>& paramLists, bool withRegistryId)
{
    ArgsList result = ArgsList();

    if (withRegistryId) {
        result.emplace_back(std::pair<std::string, std::string>(INT64_T, string(SELF_NAME) + "." + SELF_WEAKLINK_NAME));
    }

    if (!paramLists.empty() && paramLists[0]) {
        for (size_t i = 0; i < paramLists[0]->params.size(); i++) {
            OwnedPtr<FuncParam>& cur = paramLists[0]->params[i];
            auto name = cur->identifier.Val();
            name = GenerateArgumentCast(*cur->ty, std::move(name));
            result.push_back(std::pair<std::string, std::string>(MapCJTypeToObjCType(cur), name));
        }
    }
    return result;
}

std::vector<std::string> ObjCGenerator::ConvertParamsListToCallableParamsString(
    std::vector<OwnedPtr<FuncParamList>>& paramLists, bool withSelf) const
{
    std::vector<string> result = {};

    if (withSelf) {
        result.emplace_back(std::string(CAST_TO_VOID_PTR) + SELF_NAME);
    }

    if (!paramLists.empty() && paramLists[0]) {
        for (size_t i = 0; i < paramLists[0]->params.size(); i++) {
            OwnedPtr<FuncParam>& cur = paramLists[0]->params[i];
            std::string name = GenerateArgumentCast(*cur->ty, cur->identifier.Val());
            result.push_back(std::move(name));
        }
    }
    return result;
}

void ObjCGenerator::WriteToFile()
{
    WriteToHeader();
    WriteToSource();
}

void ObjCGenerator::WriteToHeader()
{
    auto objCDeclName = ctx.nameGenerator.GetObjCDeclName(*decl);
    auto headerPath = FileUtil::JoinPath(outputFilePath, objCDeclName + ".h");
    FileUtil::WriteToFile(headerPath, res);
}

void ObjCGenerator::WriteToSource()
{
    auto objCDeclName = ctx.nameGenerator.GetObjCDeclName(*decl);
    auto sourcePath = FileUtil::JoinPath(outputFilePath, objCDeclName + ".m");
    FileUtil::WriteToFile(sourcePath, resSource);
}

std::string ObjCGenerator::MapCJTypeToObjCType(const OwnedPtr<Type>& type)
{
    if (!type) {
        return UNSUPPORTED_TYPE;
    }

    return ctx.typeMapper.Cj2ObjCForObjC(*type->ty);
}

std::string ObjCGenerator::MapCJTypeToObjCType(const OwnedPtr<FuncParam>& param)
{
    if (!param) {
        return UNSUPPORTED_TYPE;
    }

    return ctx.typeMapper.Cj2ObjCForObjC(*param->type->ty);
}

std::string ObjCGenerator::GenerateArgumentCast(const Ty& retTy, std::string value) const
{
    if (ctx.typeMapper.IsObjCCJMapping(retTy)) {
        return value + "." + SELF_WEAKLINK_NAME;
    }
    const auto& actualTy = retTy.IsCoreOptionType() ? *retTy.typeArgs[0] : retTy;
    if (ctx.typeMapper.IsObjCImpl(actualTy)) {
        return CAST_TO_VOID_PTR + std::move(value);
    }
    if (ctx.typeMapper.IsObjCMirror(actualTy) || ctx.typeMapper.IsObjCBlock(actualTy) ||
        ctx.typeMapper.IsObjCCJMappingInterface(actualTy)) {
        return CAST_TO_VOID_PTR_RETAINED + std::move(value);
    }
    if (ctx.typeMapper.IsObjCPointer(actualTy)) {
        return CAST_TO_VOID_PTR_UNSAFE + std::move(value);
    }
    return value;
}

void ObjCGenerator::GenerateProtocolDecl()
{
    const auto objCDeclName = ctx.nameGenerator.GetObjCDeclName(*decl);
    AddWithIndent(GenerateImport(FOUNDATION_IMPORT), GenerationTarget::HEADER);
    AddWithIndent(GenerateImport(STDDEF_IMPORT), GenerationTarget::HEADER);

    std::string protocolDecl = "";
    protocolDecl += PROTOCOL_KEYWORD;
    protocolDecl += " ";
    protocolDecl += objCDeclName;

    AddWithIndent(protocolDecl, GenerationTarget::HEADER);

    AddMethods();

    AddWithIndent(END_KEYWORD, GenerationTarget::HEADER);
    WriteToHeader();
}

// CJMapping

/**
 * ObjC runtime functions used to calculate override-mask
 *
 */
void ObjCGenerator::GenerateExternalDeclarations4CJMapping()
{
    // If there is no forwarder class, it will not be generated.
    if (interopType != InteropType::CJ_Mapping || ctx.fwdClassMap.empty()) {
        return;
    }
    resSource += R"(
typedef struct objc_method* Method;
extern IMP method_getImplementation(Method);
extern Method class_getInstanceMethod(Class, SEL);
extern id objc_getClass(const char *);
)";
}

/**
 * Constructors for cjmapping type
 */
void ObjCGenerator::AddConstructors4CJMapping()
{
    if (interopType != InteropType::CJ_Mapping) {
        return;
    }
    // initWithRegistryId
    AddInitWithRegistryId();
    // For enum
    if (auto enumDecl = As<ASTKind::ENUM_DECL>(decl)) {
        AddCtorsForCjMappingEnum(*enumDecl);
    }
    if (!decl->IsOpen()) {
        return;
    }
    // init
    auto fwd = ctx.fwdClassMap.at(decl);
    CJC_ASSERT(fwd);
    for (auto declPtr : fwd->GetMemberDeclPtrs()) {
        if (declPtr->TestAttr(Attribute::CONSTRUCTOR)) {
            AddConstructor4CJMapping(*StaticCast<FuncDecl>(declPtr));
        }
    }
}

/**
 * for open member func.
 */
void ObjCGenerator::GenerateMethods4CJMapping()
{
    if (interopType != InteropType::CJ_Mapping || !decl->IsOpen()) {
        return;
    }
    // For open class
    AddReinitWithRegistryId();
    AddRelease();
    // Member
    auto fwd = ctx.fwdClassMap.at(decl);
    CJC_ASSERT(fwd);
    std::vector<Ptr<FuncDecl>> overrides;
    for (auto declPtr : fwd->GetMemberDeclPtrs()) {
        if (declPtr->TestAttr(Attribute::OVERRIDE)) {
            overrides.emplace_back(StaticCast<FuncDecl>(declPtr));
            GenerateMethod4CJMapping(*StaticCast<FuncDecl>(declPtr));
        }
    }
    AddCalcMask(overrides);
}

/**
 * open method
 * - (int64_t)add1:(int64_t)x  {
 *     if (self.$initedFromObjC) {
 *         CJImpl_ObjC_glue_A_fwd_add1(self.$registryId, x);
 *     } else {
 *         CJImpl_ObjC_glue_A_add1(self.$registryId, x);
 *     }
 * }
 */
void ObjCGenerator::GenerateMethod4CJMapping(AST::FuncDecl& fn)
{
    auto fnTy = DynamicCast<FuncTy>(fn.ty);
    CJC_ASSERT(fnTy);
    auto originalOpenFn = ctx.fwdOverrideTable.at(&fn);
    CJC_ASSERT(originalOpenFn);
    std::vector<Ptr<FuncParam>> params;
    auto& fwdParams = fn.funcBody->paramLists[0]->params;
    std::vector<std::string> callArgs{"self.$registryId"};
    std::transform(fwdParams.begin(), fwdParams.end(), std::back_inserter(params), [](auto& p) { return p.get(); });
    auto transAndCollectArgs = [this, &callArgs](const Ptr<FuncParam>& p) {
        auto pname = p->identifier.Val();
        callArgs.emplace_back(pname);
        return ":(" + ctx.typeMapper.Cj2ObjCForObjC(*p->ty) + ")" + pname;
    };
    auto sig = Join("-(", ctx.typeMapper.Cj2ObjCForObjC(*fnTy->retTy), ")", fn.identifier.Val(),
        JoinVec<Ptr<FuncParam>>(params, transAndCollectArgs, " ", "", "", false));
    // Write into header
    res += Join(sig, ";\n");

    auto argStr = JoinVec<std::string>(
        callArgs, [](auto& arg) { return arg; }, ", ", "(", ");");
    auto ifThen = [this, &fn, &argStr]() {
        WriteSeq({Join("return ", ctx.nameGenerator.GenerateMethodWrapperName(fn), argStr)});
    };
    auto ifElse = [this, originalOpenFn, &argStr]() {
        WriteSeq({Join("return ", ctx.nameGenerator.GenerateMethodWrapperName(*originalOpenFn), argStr)});
    };
    // Write into source
    WriteFunc(sig, [this, ifThen, ifElse]() { WriteIf("self.$initedFromObjC", ifThen, ifElse); });
}

/**
 * construnctor
 * -(id)init :(ty)name :(ty)name {
 *     if (self = [super init]) {
 *         self.$registryId = CJImpl_xxx_init_FWD_xxx();
 *         self.initedFromObjc = true;
 *     }
 * }
 */
void ObjCGenerator::AddConstructor4CJMapping(AST::FuncDecl& ctor)
{
    std::vector<Ptr<FuncParam>> params;
    auto& fwdParams = ctor.funcBody->paramLists[0]->params;
    auto const offset = 2;
    std::vector<std::string> callArgs{CAST_TO_VOID_PTR + string("self"), "[self calcOverrideMask]"};
    std::transform(
        fwdParams.begin() + offset, fwdParams.end(), std::back_inserter(params), [](auto& p) { return p.get(); });
    auto transAndCollectArgs = [this, &callArgs](const Ptr<FuncParam>& p) {
        auto pname = p->identifier.Val();
        callArgs.emplace_back(pname);
        return ":(" + ctx.typeMapper.Cj2ObjCForObjC(*p->ty) + ")" + pname;
    };
    const auto initSig = Join("-(id)init", JoinVec<Ptr<FuncParam>>(params, transAndCollectArgs, " ", "", "", false));
    // Write into header
    res += Join(initSig, ";\n");

    auto wrapperName = ctx.nameGenerator.GenerateInitCjObjectName(ctor);
    auto argStr = JoinVec<std::string>(
        callArgs, [](auto& arg) { return arg; }, ", ", "(", ");");
    auto callStr = Join(wrapperName, argStr);
    auto ifThen = [this, &callStr]() {
        WriteSeq({Join(SELF_NAME, ".", INITED_FROM_OBJC_NAME, " = ", "true;"),
            Join(SELF_NAME, ".", SELF_WEAKLINK_NAME, " = ", callStr),
            Join("[", SELF_NAME, " ", RETAIN_FUNC_NAME, "];")});
    };
    // Write into source
    WriteFunc(initSig, [this, ifThen]() {
        WriteIf("self = [super init]", ifThen);
        WriteSeq({Join("return ", SELF_NAME, ";")});
    });
}

/**
 * Properties for cjmapping type
 */
void ObjCGenerator::AddProperties4CJMapping()
{
    if (interopType != InteropType::CJ_Mapping || !decl->IsOpen()) {
        return;
    }
    // @property (readwrite) bool $initedFromObjC;
    res += Join(PROPERTY_KEYWORD, " (", READWRITE_MODIFIER, ") ", BOOL_TYPE, " ", INITED_FROM_OBJC_NAME, ";\n");
}

/**
 * initWithRegistryId
 */
void ObjCGenerator::AddInitWithRegistryId()
{
    AddWithIndent(INIT_WITH_REGISTRY_ID_SIGNATURE, GenerationTarget::BOTH, OptionalBlockOp::OPEN);
    AddWithIndent(GenerateIfStatement(SELF_NAME, GenerateObjCCall(SUPER_KEYWORD, INIT_FUNC_NAME), EQ_OP),
        GenerationTarget::SOURCE, OptionalBlockOp::OPEN);
    if (decl->IsOpen()) {
        AddWithIndent(GenerateAssignment(string(SELF_NAME) + "." + INITED_FROM_OBJC_NAME, "false") + ";",
            GenerationTarget::SOURCE);
    }
    AddWithIndent(GenerateAssignment(string(SELF_NAME) + "." + SELF_WEAKLINK_NAME, REGISTRY_ID_PARAM_NAME) + ";",
        GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);

    AddWithIndent(GenerateReturn(SELF_NAME), GenerationTarget::SOURCE, OptionalBlockOp::CLOSE);
}

/**
 * reinitWithRegistryId
 */
void ObjCGenerator::AddReinitWithRegistryId()
{
    res += Join(REINIT_WITH_REGISTRY_ID_SIGNATURE, ";\n");
    // implementation
    WriteFunc(REINIT_WITH_REGISTRY_ID_SIGNATURE, [this]() {
        WriteSeq({Join(SELF_NAME, ".", SELF_WEAKLINK_NAME, " = ", REGISTRY_ID_PARAM_NAME, ";"),
            Join("[", SELF_NAME, " ", RETAIN_FUNC_NAME, "];"), Join("return ", SELF_NAME, ";")});
    });
}

/**
 * - (void)deleteCJObject
 */
bool ObjCGenerator::GenerateDeleteObject4CJMapping()
{
    if (interopType != InteropType::CJ_Mapping || !decl->IsOpen()) {
        return false;
    }
    const auto deleteSig = "- (void)deleteCJObject";
    WriteFunc(deleteSig, [this]() {
        WriteIf("!self.$initedFromObjC", [this]() {
            auto deleteFuncName = ctx.nameGenerator.GenerateDeleteCjObjectName(*decl);
            WriteSeq({Join(deleteFuncName, "(self.$registryId);")});
        });
    });
    return true;
}

/**
 * - (oneway void) release
 */
void ObjCGenerator::AddRelease()
{
    auto fwd = ctx.fwdClassMap.at(decl);
    CJC_ASSERT(fwd);
    res += RELEASE_SIGNATURE + std::string(";\n");

    auto ifThen = [this, fwd]() {
        auto lockFuncName = ctx.nameGenerator.GenerateLockCjObjectName(*fwd);
        WriteSeq({"int64_t idToDelete = self.$registryId;", Join(lockFuncName, "(idToDelete);")});
        WriteIf(
            "1 == CFGetRetainCount((__bridge CFTypeRef)self)",
            [this, fwd]() {
                auto deleteFuncName = ctx.nameGenerator.GenerateDeleteCjObjectName(*fwd);
                WriteSeq({"self.$registryId = -1;", Join(deleteFuncName, "(idToDelete);")});
            },
            [this, fwd]() {
                auto unlockFuncName = ctx.nameGenerator.GenerateUnlockCjObjectName(*fwd);
                WriteSeq({Join(unlockFuncName, "(idToDelete);")});
            });
    };
    auto body = [this, ifThen]() {
        WriteSeq({"[super release];"});
        WriteIf("self.$initedFromObjC && 1 == CFGetRetainCount((__bridge CFTypeRef)self)", ifThen);
    };
    WriteFunc(RELEASE_SIGNATURE, body);
}

/**
 * - (uint64_t)calcOverrideMask
 */
void ObjCGenerator::AddCalcMask(const std::vector<Ptr<FuncDecl>>& overrides)
{
    resSource += R"(
static uint64_t calcMask(Class baseCls, Class selfCls, SEL* methods, int len) {
    int max = len;
    if (max > 64) {
        max = 64;
    }
    uint64_t mask = 0;
    for (int i = 0; i < max; i++) {
        SEL m = methods[i];
        bool override = method_getImplementation(class_getInstanceMethod(baseCls, m)) !=
            method_getImplementation(class_getInstanceMethod(selfCls, m));
        if (override) {
            mask |= (UINT64_C(1) << i);
        }
    }
    return mask;
}

)";
    const auto calcOverrideMaskSig = "- (uint64_t)calcOverrideMask";
    auto genSel = [](const Ptr<FuncDecl>& fn) {
        CJC_ASSERT(fn && fn->funcBody && fn->funcBody->paramLists.size() == 1);
        return "@selector(" + fn->identifier.Val() + std::string(fn->funcBody->paramLists[0]->params.size(), ':') + ")";
    };
    auto calcOverrideMaskBody = [this, &overrides, genSel]() {
        WriteSeq({Join("Class baseCls = objc_getClass(\"", ctx.nameGenerator.GetObjCDeclName(*decl), "\");"),
            "Class selfCls = [self class];", JoinVec<Ptr<FuncDecl>>(overrides, genSel, ", ", "SEL methods[] = {", "};"),
            Join("return calcMask(baseCls, selfCls, methods, ", overrides.size(), ");")});
    };
    WriteFunc(calcOverrideMaskSig, calcOverrideMaskBody);
}

void ObjCGenerator::WriteFunc(const std::string& signature, const std::function<void()> body)
{
    buffer << std::string(currentBlockIndent, ' ') << signature;
    WriteBlock(body, "", "\n\n", true);
}

void ObjCGenerator::WriteFor(const std::string& header, const std::function<void()> loop)
{
    buffer << std::string(currentBlockIndent, ' ') << Join("for (", header, ")");
    WriteBlock(loop);
    buffer << std::endl;
}

void ObjCGenerator::WriteIf(
    const std::string& cond, const std::function<void()> then, const std::function<void()> other)
{
    buffer << std::string(currentBlockIndent, ' ') << Join("if (", cond, ")");
    WriteBlock(then);
    if (other) {
        WriteBlock(other, " else");
    }
    buffer << std::endl;
}

void ObjCGenerator::WriteBlock(std::function<void()> action, const std::string& pre, const std::string& suf, bool flush)
{
    buffer << Join(pre, " {") << std::endl;
    currentBlockIndent += INDENT_SIZE;
    action();
    currentBlockIndent -= INDENT_SIZE;
    buffer << std::string(currentBlockIndent, ' ') << Join("}", suf);
    if (flush) {
        resSource += buffer.str();
        buffer.str("");
        buffer.clear();
    }
}

void ObjCGenerator::WriteSeq(const std::vector<std::string>& statements)
{
    for (auto& statement : statements) {
        buffer << std::string(currentBlockIndent, ' ') << statement << std::endl;
    }
}
} // namespace Cangjie::Interop::ObjC
