// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements Java class generation.
 */

#include <limits>

#include "cangjie/AST/Match.h"
#include "cangjie/Utils/FileUtil.h"
#include "NativeFFI/Java/AfterTypeCheck/JavaDesugarManager.h"
#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "cangjie/Utils/StdUtils.h"
#include "JavaSourceCodeGenerator.h"

namespace {
using namespace Cangjie::AST;
using namespace Cangjie::Interop::Java;
using namespace Cangjie::Native::FFI;
using TokenKind = Cangjie::TokenKind;

constexpr auto JAVA_PACKAGE = "package";
constexpr auto JAVA_PUBLIC = "public";
constexpr auto JAVA_PRIVATE = "private";
constexpr auto JAVA_SEMICOLON = ";";
constexpr auto JAVA_COMMA = ",";
constexpr auto JAVA_WHITESPACE = " ";

constexpr auto JAVA_BOOLEAN = "boolean";
constexpr auto JAVA_SPECIAL_PARAM_NAME = "__init__";

bool IsFuncDeclAndNotConstructor(OwnedPtr<Decl>& declPtr)
{
    return declPtr->astKind == ASTKind::FUNC_DECL && !declPtr->TestAttr(Attribute::CONSTRUCTOR);
}

std::string GetModifier(Decl* decl)
{
    if (decl->TestAttr(Attribute::PUBLIC)) {
        return "public ";
    } else {
        return "";
    }
}

bool NeedExtraFinalModifier(const Decl& declArg)
{
    if (declArg.TestAttr(Attribute::JAVA_CJ_MAPPING) && declArg.astKind == ASTKind::STRUCT_DECL) {
        return true;
    }
    return false;
}

} // namespace

namespace Cangjie::Interop::Java {
JavaSourceCodeGenerator::JavaSourceCodeGenerator(
    Decl* decl, const BaseMangler& mangler, const std::string& outputFilePath, std::string cjLibName)
    : AbstractSourceCodeGenerator(outputFilePath), decl(decl), cjLibName(std::move(cjLibName)), mangler(mangler)
{
}

JavaSourceCodeGenerator::JavaSourceCodeGenerator(Decl* decl, const BaseMangler& mangler,
    const std::optional<std::string>& outputFolderPath, const std::string& outputFileName, std::string cjLibName)
    : AbstractSourceCodeGenerator(
          outputFolderPath.value_or(JavaSourceCodeGenerator::DEFAULT_OUTPUT_DIR), outputFileName),
      decl(decl),
      cjLibName(std::move(cjLibName)),
      mangler(mangler)
{
}

bool JavaSourceCodeGenerator::IsDeclAppropriateForGeneration(const Decl& declArg)
{
    return (IsImpl(declArg) &&
           (declArg.astKind == ASTKind::CLASS_DECL || declArg.astKind == ASTKind::INTERFACE_DECL)) ||
           IsCJMapping(declArg);
}

void JavaSourceCodeGenerator::ConstructResult()
{
    if (decl->TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN)) {
        return;
    }

    AddClassDeclaration();
    AddLoadLibrary();
    AddSelfIdField();
    AddProperties();
    AddConstructors();
    AddMethods();
    AddNativeDeleteCJObject();
    AddFinalize();
    AddEndClassParenthesis();
    AddHeader(); // after all because during AST traverse the 'imports' field is accumulating import declarations
}

void JavaSourceCodeGenerator::AddHeader()
{
    std::string curPackageName = GetJavaPackage(*decl);

    std::string header;
    if (!curPackageName.empty()) {
        header += JAVA_PACKAGE;
        header += JAVA_WHITESPACE;
        header += curPackageName;
        header += JAVA_SEMICOLON;
        header += "\n\n";
    }

    const std::function<std::string(const std::string&)>& mapper = [](const std::string& s) {
        return "import " + s + ";";
    };
    header += Join(imports, "\n", mapper) + "\n";
    header += "import static cangjie.lang.LibraryLoader.loadLibrary;\n";
    header += "import " + GetConstructorMarkerFQName() + ";\n";

    header += "\n";

    res = std::move(header) + res;
}

std::string JavaSourceCodeGenerator::MapCJTypeToJavaType(
    const Ptr<Ty> ty, std::set<std::string>* javaImports, const std::string* curPackageName, bool isNativeMethod)
{
    if (ty->IsCoreOptionType()) {
        return MapCJTypeToJavaType(ty->typeArgs[0], javaImports, curPackageName, isNativeMethod);
    }

    std::string javaType = "";
    auto declTy = Ty::GetDeclOfTy(ty);

    switch (ty->kind) {
        case TypeKind::TYPE_FLOAT64:
            javaType = "double";
            break;
        case TypeKind::TYPE_FLOAT32:
            javaType = "float";
            break;
        case TypeKind::TYPE_INT64:
            javaType = "long";
            break;
        case TypeKind::TYPE_INT32:
            javaType = "int";
            break;
        case TypeKind::TYPE_INT16:
            javaType = "short";
            break;
        case TypeKind::TYPE_UINT16:
            javaType = "char";
            break;
        case TypeKind::TYPE_INT8:
            javaType = "byte";
            break;
        case TypeKind::TYPE_BOOLEAN:
            javaType = JAVA_BOOLEAN;
            break;
        case TypeKind::TYPE_UNIT:
            javaType = "void";
            break;
        case TypeKind::TYPE_INTERFACE:
        case TypeKind::TYPE_CLASS:
            if (IsJArray(*declTy)) {
                return MapCJTypeToJavaType(ty->typeArgs[0], javaImports, curPackageName) + "[]";
            } else if (isNativeMethod && declTy && IsCJMapping(*declTy)) {
                return "long";
            }
            javaType = AddImport(ty, javaImports, curPackageName);
            break;
        case TypeKind::TYPE_STRUCT:
        case TypeKind::TYPE_ENUM:
            if (isNativeMethod && declTy && IsCJMapping(*declTy)) {
                return "long";
            }
            javaType = ty->name;
            break;
        default:
            if (ty->name == "String") {
                javaType = "String";
            }
    }

    CJC_ASSERT(!javaType.empty());

    return javaType;
}

std::string JavaSourceCodeGenerator::MapCJTypeToJavaType(const OwnedPtr<Type>& type, std::set<std::string>* javaImports,
    const std::string* curPackageName, bool isNativeMethod)
{
    CJC_ASSERT(type && type->ty);
    return MapCJTypeToJavaType(type->ty, javaImports, curPackageName, isNativeMethod);
}

std::string JavaSourceCodeGenerator::MapCJTypeToJavaType(const OwnedPtr<FuncParam>& param,
    std::set<std::string>* javaImports, const std::string* curPackageName, bool isNativeMethod)
{
    CJC_ASSERT(param && param->type && param->type->ty);
    return MapCJTypeToJavaType(param->type->ty, javaImports, curPackageName, isNativeMethod);
}

void JavaSourceCodeGenerator::AddClassDeclaration()
{
    std::string modifier = NeedExtraFinalModifier(*decl) ? "final " : "";
    modifier += GetModifier(decl);
    res += modifier;
    res += "class " + decl->identifier.Val();

    if (auto classDecl = As<ASTKind::CLASS_DECL>(decl)) {
        Ptr<ClassDecl> superClassPtr = classDecl->GetSuperClassDecl();
        bool isClassInheritedFromClass = !IsJObject(*superClassPtr);

        std::set<Ptr<InterfaceTy>> implementedInterfacesPtrs = classDecl->GetSuperInterfaceTys();
        size_t implementedInterfacesCnt = implementedInterfacesPtrs.size();

        if (isClassInheritedFromClass) {
            res += " extends ";
            res += MapCJTypeToJavaType(superClassPtr->ty, &imports, &classDecl->fullPackageName);
        }

        if (implementedInterfacesCnt > 0) {
            res += " implements ";
            std::set<std::string>* imp = &imports;
            const std::string* package = &classDecl->fullPackageName;
            const std::function<std::string(Ptr<InterfaceTy>)>& transformer = [imp, package](Ptr<InterfaceTy> p) {
                return MapCJTypeToJavaType(p, imp, package);
            };
            res += Join(implementedInterfacesPtrs, ", ", transformer);
        }
    }

    res += " {\n";
}

std::string JavaSourceCodeGenerator::AddImport(
    Ptr<Ty> ty, std::set<std::string>* javaImports, const std::string* curPackageName)
{
    std::string javaType = "";

    if (auto classlikety = DynamicCast<ClassLikeTy>(ty)) {
        if (auto clDecl = As<ASTKind::CLASS_LIKE_DECL>(classlikety->commonDecl)) {
            auto [package, topLevelClassName, fullClassName] = Interop::Java::DestructJavaClassName(*clDecl);
            if (!package) {
                return fullClassName;
            }
            if (*package != *curPackageName && *package != IGNORE_IMPORT) {
                javaImports->insert(*package + "." + topLevelClassName);
            }
            javaType = fullClassName;
        }
    }

    return javaType;
}

void JavaSourceCodeGenerator::AddLoadLibrary()
{
    AddWithIndent(TAB, "static {");
    AddWithIndent(TAB2, "loadLibrary(\"" + cjLibName + "\");");
    AddWithIndent(TAB, "}\n");
}

void JavaSourceCodeGenerator::AddSelfIdField()
{
    AddWithIndent(TAB, "long self;\n");
}

void JavaSourceCodeGenerator::AddProperties()
{
    for (OwnedPtr<Decl>& declPtr : decl->GetMemberDecls()) {
        if (declPtr->astKind == ASTKind::PROP_DECL && !declPtr->TestAttr(Attribute::COMPILER_ADD)) {
            const PropDecl& propDecl = *StaticAs<ASTKind::PROP_DECL>(declPtr.get());
            const OwnedPtr<FuncDecl>& funcDecl = propDecl.getters[0];
            const std::string type =
                MapCJTypeToJavaType(funcDecl->funcBody->retType, &imports, &decl->fullPackageName, false);

            std::string varDecl = GetJavaMemberName(propDecl);
            std::string varDeclSuffix = varDecl;
            varDeclSuffix[0] = static_cast<char>(toupper(varDeclSuffix[0]));

            std::string getSignature = "get" + varDeclSuffix;

            // add getter
            AddWithIndent(TAB, "public " + type + " " + getSignature + "() {");
            AddWithIndent(TAB2, "return " + getSignature + "Impl(this.self);");
            AddWithIndent(TAB, "}\n");
            AddWithIndent(TAB, "public native " + type + " " + getSignature + "Impl(long self);\n");

            // add setter
            if (!propDecl.setters.empty()) {
                std::string setSignature = "set" + varDeclSuffix;
                AddWithIndent(TAB, "public void " + setSignature + "(" + type + " " + varDecl + ") {");
                AddWithIndent(TAB2, setSignature + "Impl(this.self, " + varDecl + ");");
                AddWithIndent(TAB, "}\n");
                AddWithIndent(
                    TAB, "public native void " + setSignature + "Impl(long self, " + type + " " + varDecl + ");\n");
            }
        }
    }
}

std::string JavaSourceCodeGenerator::GenerateFuncParams(
    const std::vector<OwnedPtr<FuncParam>>& params, bool isNativeMethod)
{
    std::set<std::string>* imp = &imports;
    const std::string* curPackage = &decl->fullPackageName;
    std::function<std::string(const OwnedPtr<FuncParam>& ptr)> mapper = [imp, curPackage, isNativeMethod](
                                                                            const OwnedPtr<FuncParam>& cur) {
        return MapCJTypeToJavaType(cur, imp, curPackage, isNativeMethod) + " " + cur->identifier.Val();
    };
    return GenerateParams(params, mapper);
}

std::string JavaSourceCodeGenerator::GenerateFuncParamLists(
    const std::vector<OwnedPtr<FuncParamList>>& paramLists, bool isNativeMethod)
{
    std::set<std::string>* imp = &imports;
    const std::string* curPackage = &decl->fullPackageName;
    std::function<std::string(const OwnedPtr<FuncParam>& ptr)> mapper = [imp, curPackage, isNativeMethod](
                                                                            const OwnedPtr<FuncParam>& cur) {
        CJC_ASSERT(cur && cur->type && cur->type->ty);
        std::string res = MapCJTypeToJavaType(cur, imp, curPackage, isNativeMethod) + " " + cur->identifier.Val();
        if (auto ty = Ty::GetDeclOfTy(cur->type->ty)) {
            bool castToId = isNativeMethod && IsCJMapping(*ty);
            res += castToId ? "Id" : "";
        }
        return res;
    };
    return GenerateParamLists(paramLists, mapper);
}

std::string JavaSourceCodeGenerator::GenerateConstructorForEnumDecl(const OwnedPtr<Decl>& ctor)
{
    std::string declaration;
    declaration.append(JAVA_PUBLIC);
    declaration.append(JAVA_WHITESPACE);
    declaration.append("static");
    declaration.append(JAVA_WHITESPACE);
    declaration.append(ctor.get()->outerDecl->identifier.Val());
    declaration.append(JAVA_WHITESPACE);

    std::string params;
    auto func = As<ASTKind::FUNC_DECL>(ctor.get());
    if (func && func->funcBody) {
        declaration.append(ctor.get()->identifier.Val() + "(");
        params += GenerateFuncParamLists(func->funcBody->paramLists);
        declaration += params;
        declaration += ") {";
    } else {
        declaration.append(ctor.get()->identifier.Val());
    }

    return declaration;
}

std::string JavaSourceCodeGenerator::GenerateConstructorDecl(const FuncDecl& func, bool isForCangjie)
{
    std::string declaration;
    if (!isForCangjie && func.TestAttr(Attribute::PUBLIC)) {
        declaration += JAVA_PUBLIC;
        declaration += JAVA_WHITESPACE;
    } else if (isForCangjie) {
        declaration += JAVA_PRIVATE;
        declaration += JAVA_WHITESPACE;
    }
    declaration.append(decl->identifier.Val() + "(");
    std::string params;
    if (func.funcBody) {
        params += GenerateFuncParamLists(func.funcBody->paramLists);
        if (isForCangjie) {
            if (!params.empty()) {
                params += JAVA_COMMA;
                params += JAVA_WHITESPACE;
            }
            params += GetConstructorMarkerClassName();
            params += JAVA_WHITESPACE;
            params += JAVA_SPECIAL_PARAM_NAME;
        }
    }
    declaration += params;
    declaration += ")";

    return declaration;
}

std::string JavaSourceCodeGenerator::GenerateParams(const std::vector<OwnedPtr<FuncParam>>& params,
    const std::function<std::string(const OwnedPtr<FuncParam>& ptr)>& transform)
{
    return Join(params, ", ", transform);
}

std::string JavaSourceCodeGenerator::GenerateParamLists(const std::vector<OwnedPtr<FuncParamList>>& paramLists,
    const std::function<std::string(const OwnedPtr<FuncParam>& ptr)>& transform)
{
    if (!paramLists.empty() && paramLists[0]) {
        return GenerateParams(paramLists[0]->params, transform);
    }
    return "";
}

/**
 * The important function to generate super call argument and native declaration.
 *
 * @param arg is the super argument with desugared native func
 * @param params is original constructor parameters
 */
std::pair<std::string, std::string> JavaSourceCodeGenerator::GenNativeSuperArgCall(
    const FuncArg& arg, const std::vector<OwnedPtr<FuncParam>>& params)
{
    CJC_ASSERT(arg.expr->desugarExpr->astKind == ASTKind::REF_EXPR);
    auto ref = StaticCast<const RefExpr*>(arg.expr->desugarExpr.get());
    CJC_NULLPTR_CHECK(ref->ref.target);
    CJC_ASSERT(ref->ref.target->astKind == ASTKind::FUNC_DECL);
    auto nativeFn = StaticCast<FuncDecl*>(ref->ref.target);
    std::string id = nativeFn->identifier.Val();
    const std::string keyword = "_super";
    size_t pos = id.rfind(keyword);
    CJC_ASSERT(pos != std::string::npos);
    // The native id should be `superC0A0...`
    id = id.substr(pos + 1);
    // Decode using parameter ids
    const std::string sep = "P";
    std::vector<std::string> parts = Cangjie::Utils::SplitString(id.substr(keyword.length()), sep);
    std::vector<std::string> args;
    std::vector<std::string> nativeParams;
    auto mpTy = [this](const Ptr<Ty> ty) { return MapCJTypeToJavaType(ty, &imports, &decl->fullPackageName); };
    // Skip first part.
    for (size_t i = 1; i < parts.size(); i++) {
        auto pid = Cangjie::Stoi(parts[i]);
        CJC_ASSERT(pid.has_value());
        auto index = pid.value();
        auto& pname = params[index]->identifier.Val();
        args.push_back(pname);
        nativeParams.push_back(mpTy(params[index]->ty) + " " + pname);
    }
    std::string superCall = id + "(" + Cangjie::Utils::JoinStrings(args, ", ") + ")";
    std::string nativeFnDecl = "public static native " + mpTy(arg.ty) + " " + id +
        "(" + Cangjie::Utils::JoinStrings(nativeParams, ", ") + ");";
    return std::make_pair(superCall, nativeFnDecl);
}

// Generate super call and collection native func declaration.
std::string JavaSourceCodeGenerator::GenerateSuperCall(
    const CallExpr& call, const std::vector<OwnedPtr<FuncParam>>& params, std::vector<std::string>& nativeArgs)
{
    // generate logic
    std::vector<std::string> args;
    for (auto& arg : call.args) {
        if (arg->expr->desugarExpr) {
            auto [superCall, superNative] = GenNativeSuperArgCall(*arg, params);
            args.push_back(superCall);
            nativeArgs.push_back(superNative);
        } else {
            args.push_back(arg->expr->ToString());
        }
    }
    return "super(" + Cangjie::Utils::JoinStrings(args, ", ") + ");";
}

std::string JavaSourceCodeGenerator::GenerateConstructorSuperCall(
    const FuncBody& body, std::vector<std::string>& nativeArgs)
{
    if (!body.body) {
        return "";
    }
    std::string superCall;
    auto& params = body.paramLists[0]->params;
    // Generate super call
    auto& block = body.body->body;
    for (auto it = block.begin(); it != block.end(); it++) {
        if ((*it)->astKind != ASTKind::CALL_EXPR) {
            continue;
        }
        auto call = StaticCast<const CallExpr*>(it->get());
        if (call->callKind == CallKind::CALL_SUPER_FUNCTION &&
            call->TestAttr(Attribute::UNREACHABLE, Attribute::JAVA_MIRROR)) {
            superCall = GenerateSuperCall(*call, params, nativeArgs);
            // remove after generating java code.
            block.erase(it);
            break;
        }
    }
    return superCall;
}


void JavaSourceCodeGenerator::AddConstructor(const FuncDecl& ctor, const std::string& superCall, bool isForCangjie)
{
    AddWithIndent(TAB, GenerateConstructorDecl(ctor, isForCangjie) + " {");
    // super call
    if (!superCall.empty()) {
        AddWithIndent(TAB2, superCall);
    }
    if (!isForCangjie) {
        // initCJObject
        auto& params = ctor.funcBody->paramLists[0]->params;
        std::string selfInit = "self = " + GetMangledJniInitCjObjectFuncName(mangler, params, false) + "(";
        if (ctor.funcBody) {
            selfInit += GenerateParamLists(
                ctor.funcBody->paramLists, [](const OwnedPtr<FuncParam>& p) { return p->identifier.Val(); });
        }
        selfInit += ");";
        AddWithIndent(TAB2, selfInit);
    }
    AddWithIndent(TAB, "}\n");
}

void JavaSourceCodeGenerator::AddConstructor(const FuncDecl& ctor)
{
    std::vector<std::string> nativeArgs;
    std::string superCall = GenerateConstructorSuperCall(*ctor.funcBody, nativeArgs);
    auto& params = ctor.funcBody->paramLists[0]->params;
    // Java side constructor
    AddConstructor(ctor, superCall, false);
    // For Cangjie side constructor with marker.
    AddConstructor(ctor, superCall, true);
    // Add native init
    AddNativeInitCJObject(params);
    // Add native super args
    for (auto& fn : nativeArgs) {
        AddWithIndent(TAB, fn);
    }
}

void JavaSourceCodeGenerator::AddAllCtorsForCJMappingEnum(const EnumDecl& enumDecl)
{
    const std::string enumName = enumDecl.identifier;
    for (auto& constructor : enumDecl.constructors) {
        std::string declaration = GenerateConstructorForEnumDecl(constructor);

        if (constructor->astKind == ASTKind::FUNC_DECL) {
            auto funcDecl = As<ASTKind::FUNC_DECL>(constructor.get());
            CJC_NULLPTR_CHECK(funcDecl);
            auto funcName = funcDecl->identifier;
            auto& params = funcDecl->funcBody->paramLists[0]->params;
            AddWithIndent(TAB, declaration);
            auto nativeFuncName = GetMangledJniInitCjObjectFuncNameForEnum(mangler, params, funcName);
            std::string selfInit = "return new " + enumName + "(" + nativeFuncName + "(";
            if (funcDecl->funcBody) {
                selfInit += GenerateParamLists(funcDecl->funcBody->paramLists, [](const OwnedPtr<FuncParam>& p) {
                    std::string res = p->identifier.Val();
                    if (auto ty = Ty::GetDeclOfTy(p->type->ty)) {
                        res += IsCJMapping(*ty) ? ".self" : "";
                    }
                    return res;
                });
            }
            selfInit += "));";
            AddWithIndent(TAB2, selfInit);
            AddWithIndent(TAB, "}\n");
            // Add Native function
            auto strParams = GenerateFuncParams(params, true);
            std::string signature = "private static native long " + nativeFuncName + "(" + strParams + ");\n";
            AddWithIndent(TAB, signature);
        } else if (constructor->astKind == ASTKind::VAR_DECL) {
            auto varDecl = As<ASTKind::VAR_DECL>(constructor.get());
            CJC_NULLPTR_CHECK(varDecl);
            auto varName = varDecl->identifier;
            auto nativeFuncName = varName + "initCJObject";
            auto selfInit = declaration + " = new " + enumName + "(" + nativeFuncName + "());\n";
            AddWithIndent(TAB, selfInit);
            std::string signature = "private static native long " + nativeFuncName + "();\n";
            AddWithIndent(TAB, signature);
        }
    }
}

void JavaSourceCodeGenerator::AddConstructors()
{
    for (OwnedPtr<Decl>& declPtr : decl->GetMemberDecls()) {
        auto fd = As<ASTKind::FUNC_DECL>(declPtr.get());
        if (!fd || fd->TestAttr(Attribute::PRIVATE) || !fd->TestAttr(Attribute::CONSTRUCTOR)) {
            continue;
        }
        if (IsCJMapping(*decl) && !fd->TestAttr(Attribute::PUBLIC)) {
            continue;
        }
        const auto& funcDecl = *fd;
        if (IsGeneratedJavaImplConstructor(funcDecl)) {
            continue;
        }
        AddConstructor(*fd);
    }

    if (IsCJMapping(*decl)) {
        if (auto enumDecl = As<ASTKind::ENUM_DECL>(decl)) {
            AddPrivateCtorForCJMappringEnum();
            AddAllCtorsForCJMappingEnum(*enumDecl);
        } else {
            AddPrivateCtorForCJMappring();
        }
    }
}

void JavaSourceCodeGenerator::AddInstanceMethod(const FuncDecl& funcDecl)
{
    auto& params = funcDecl.funcBody->paramLists[0]->params;
    auto funcIdentifier = GetJavaMemberName(funcDecl);
    auto mangledNativeName = GetMangledMethodName(mangler, params, funcIdentifier);
    if (funcDecl.TestAttr(Attribute::OVERRIDE)) {
        AddWithIndent(TAB, "@Override");
    }
    const std::string retType = MapCJTypeToJavaType(funcDecl.funcBody->retType, &imports, &decl->fullPackageName);
    std::string methodSignature = "public " + retType + " ";
    methodSignature += funcIdentifier + "(";
    std::string argsWithTypes = GenerateFuncParamLists(funcDecl.funcBody->paramLists, false);
    methodSignature += argsWithTypes;
    methodSignature += ") {";
    AddWithIndent(TAB, methodSignature);

    std::string args = "this.self";
    const std::function<std::string(const OwnedPtr<FuncParam>&)>& mapper = [](const OwnedPtr<FuncParam>& p) {
        CJC_ASSERT(p && p->type && p->type->ty);
        std::string res = p->identifier.Val();
        if (auto ty = Ty::GetDeclOfTy(p->type->ty)) {
            res += IsCJMapping(*ty) ? ".self" : "";
        }
        return res;
    };
    std::string paramList = Join(params, ", ", mapper);
    if (!paramList.empty()) {
        args += ", ";
        args += paramList;
    }
    auto funcCall = mangledNativeName + "(" + args + ");";
    if (retType != "void") {
        funcCall = "return " + funcCall;
    }
    AddWithIndent(TAB2, funcCall);
    AddWithIndent(TAB, "}\n");

    argsWithTypes = GenerateFuncParamLists(funcDecl.funcBody->paramLists, true);
    auto comma = (argsWithTypes.empty()) ? "" : ", ";
    AddWithIndent(TAB,
        "public native " + retType + " " + mangledNativeName + "(long self" + comma + argsWithTypes +
            ");\n");
}

void JavaSourceCodeGenerator::AddStaticMethod(const FuncDecl& funcDecl)
{
    auto& params = funcDecl.funcBody->paramLists[0]->params;
    auto funcIdentifier = GetJavaMemberName(funcDecl);
    auto mangledNativeName = GetMangledMethodName(mangler, params, funcIdentifier);
    const std::string retType = MapCJTypeToJavaType(funcDecl.funcBody->retType, &imports, &decl->fullPackageName);
    std::string argsWithTypes = GenerateFuncParamLists(funcDecl.funcBody->paramLists, false);

    const std::function<std::string(const OwnedPtr<FuncParam>&)>& mapper = [](const OwnedPtr<FuncParam>& p) {
        CJC_ASSERT(p && p->type && p->type->ty);
        std::string res = p->identifier.Val();
        if (auto ty = Ty::GetDeclOfTy(p->type->ty)) {
            res += IsCJMapping(*ty) ? ".self" : "";
        }
        return res;
    };
    std::string paramList = Join(params, ", ", mapper);

    if (mangledNativeName != funcIdentifier) {
        AddWithIndent(TAB, "public static " + retType + " " + funcIdentifier + "(" + argsWithTypes + ") {");
        auto funcCall = mangledNativeName + "(" + paramList + ");";
        if (retType != "void") {
            funcCall = "return " + funcCall;
        }
        AddWithIndent(TAB2, funcCall);
        AddWithIndent(TAB, "}\n");
    }
    argsWithTypes = GenerateFuncParamLists(funcDecl.funcBody->paramLists, true);
    std::string methodSignature = "public static native " + retType + " ";
    methodSignature += mangledNativeName + "(";
    methodSignature += argsWithTypes;
    methodSignature += ");\n";
    AddWithIndent(TAB, methodSignature);
}

void JavaSourceCodeGenerator::AddMethods()
{
    for (OwnedPtr<Decl>& declPtr : decl->GetMemberDecls()) {
        if (IsCJMapping(*decl) && !declPtr->TestAttr(Attribute::PUBLIC)) {
            continue;
        }
        if (!declPtr->TestAttr(Attribute::PRIVATE) && IsFuncDeclAndNotConstructor(declPtr)) {
            const FuncDecl& funcDecl = *StaticAs<ASTKind::FUNC_DECL>(declPtr.get());
            if (funcDecl.funcBody && funcDecl.funcBody->retType) {
                if (funcDecl.TestAttr(Attribute::STATIC)) {
                    AddStaticMethod(funcDecl);
                } else {
                    AddInstanceMethod(funcDecl);
                }
            }
        }
    }
}

void JavaSourceCodeGenerator::AddEndClassParenthesis()
{
    res += "}\n";
}

/**
 * @param params is original constructor parameters
 */
void JavaSourceCodeGenerator::AddNativeInitCJObject(const std::vector<OwnedPtr<Cangjie::AST::FuncParam>> &params)
{
    auto name = GetMangledJniInitCjObjectFuncName(mangler, params, false);
    auto strParams = GenerateFuncParams(params, true);
    std::string signature = "public native long " + name + "(" + strParams + ");\n";
    AddWithIndent(TAB, signature);
}

void JavaSourceCodeGenerator::AddNativeDeleteCJObject()
{
    AddWithIndent(TAB, "public native void deleteCJObject(long self);\n");
}

void JavaSourceCodeGenerator::AddFinalize()
{
    AddWithIndent(TAB, "@Override");
    AddWithIndent(TAB, "public void finalize() {");
    AddWithIndent(TAB2, "deleteCJObject(this.self);");
    AddWithIndent(TAB, "}");
}

void JavaSourceCodeGenerator::AddPrivateCtorForCJMappring()
{
    std::string signature =
        "private " + decl->identifier.Val() + " (long id, " + GetConstructorMarkerClassName() + " __init__) {";
    AddWithIndent(TAB, signature);
    AddWithIndent(TAB2, "self = id;");
    AddWithIndent(TAB, "}\n");
}

void JavaSourceCodeGenerator::AddPrivateCtorForCJMappringEnum()
{
    std::string signature = "private " + decl->identifier.Val() + " (long id) {";
    AddWithIndent(TAB, signature);
    AddWithIndent(TAB2, "self = id;");
    AddWithIndent(TAB, "}\n");
}

const std::string JavaSourceCodeGenerator::DEFAULT_OUTPUT_DIR = "java-gen";
const std::string JavaSourceCodeGenerator::IGNORE_IMPORT = "java.lang";
} // namespace Cangjie::Interop::Java
