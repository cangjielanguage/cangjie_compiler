// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaDesugarManager.h"
#include "NativeFFI/Java/JavaCodeGenerator/JavaSourceCodeGenerator.h"
#include "Utils.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Symbol.h"

namespace Cangjie::Interop::Java {
using namespace Cangjie::Native::FFI;

// Support Struct decl and Enum decl for now.
OwnedPtr<Decl> JavaDesugarManager::GenerateCJMappingNativeDeleteCjObjectFunc(Decl& decl)
{
    std::vector<OwnedPtr<FuncParam>> params;
    FuncParam* jniEnvPtrParam = nullptr;
    OwnedPtr<Expr> selfParamRef;
    GenerateFuncParamsForNativeDeleteCjObject(decl, params, jniEnvPtrParam, selfParamRef);

    auto removeFromRegistryCall = lib.CreateRemoveFromRegistryCall(std::move(selfParamRef));
    auto wrappedNodesLambda = WrapReturningLambdaExpr(typeManager, Nodes(std::move(removeFromRegistryCall)));
    Ptr<Ty> unitTy = typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT).get();
    auto funcName = GetJniDeleteCjObjectFuncName(decl);
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(params)));

    return GenerateNativeFuncDeclBylambda(decl, wrappedNodesLambda, paramLists, *jniEnvPtrParam, unitTy, funcName);
}

// Current support struct, class type.
void JavaDesugarManager::GenerateForCJStructOrClassTypeMapping(const File &file, AST::Decl* decl)
{
    CJC_ASSERT(decl && IsCJMapping(*decl));
    auto classDecl = DynamicCast<AST::ClassDecl*>(decl);
    auto structDecl = DynamicCast<AST::StructDecl*>(decl);
    CJC_ASSERT((classDecl || structDecl) && "Not a support ref type.");

    std::vector<FuncDecl*> generatedCtors;
    for (auto& member : decl->GetMemberDecls()) {
        if (member->TestAnyAttr(Attribute::IS_BROKEN, Attribute::PRIVATE, Attribute::PROTECTED, Attribute::INTERNAL) ||
            (file.curPackage.get()->isInteropCJPackageConfig && member.get()->symbol &&
            !member.get()->symbol->isNeedExposedToInterop)) {
            continue;
        }
        if (auto fd = As<ASTKind::FUNC_DECL>(member.get())) {
            if (fd->TestAttr(Attribute::CONSTRUCTOR)) {
                generatedCtors.push_back(fd);
            } else {
                auto nativeMethod = GenerateNativeMethod(*fd, *decl);
                if (nativeMethod != nullptr) {
                    generatedDecls.push_back(std::move(nativeMethod));
                }
            }
        }
    }
    if (!generatedCtors.empty()) {
        generatedDecls.push_back(GenerateCJMappingNativeDeleteCjObjectFunc(*decl));
        for (auto generatedCtor : generatedCtors) {
            generatedDecls.push_back(GenerateNativeInitCjObjectFunc(*generatedCtor, false));
        }
    }
}

OwnedPtr<Decl> JavaDesugarManager::GenerateNativeInitCjObjectFuncForEnumCtorNoParams(
    AST::EnumDecl& enumDecl, AST::VarDecl& ctor)
{
    // Empty params to build constructor from VarDecl.
    std::vector<OwnedPtr<FuncParam>> params;
    std::vector<OwnedPtr<FuncArg>> ctorCallArgs;
    PushEnvParams(params, "env");
    auto curFile = ctor.curFile;
    CJC_NULLPTR_CHECK(curFile);
    auto& jniEnvPtrParam = *(params[0]);

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(params)));

    auto enumRef = WithinFile(CreateRefExpr(enumDecl), curFile);
    auto objectCtorCall = CreateMemberAccess(std::move(enumRef), ctor.identifier);

    auto putToRegistryCall = lib.CreatePutToRegistryCall(std::move(objectCtorCall));
    auto bodyLambda = WrapReturningLambdaExpr(typeManager, Nodes(std::move(putToRegistryCall)));
    auto jlongTy = lib.GetJlongTy();
    auto funcName = GetJniInitCjObjectFuncNameForVarDecl(ctor);
    return GenerateNativeFuncDeclBylambda(ctor, bodyLambda, paramLists, jniEnvPtrParam, jlongTy, funcName);
}

void JavaDesugarManager::GenerateNativeInitCJObjectEnumCtor(AST::EnumDecl& enumDecl)
{
    auto nativeMethod = MakeOwned<Decl>();
    for (auto& ctor : enumDecl.constructors) {
        if (ctor->astKind == ASTKind::FUNC_DECL) {
            auto fd = As<ASTKind::FUNC_DECL>(ctor.get());
            CJC_NULLPTR_CHECK(fd);
            nativeMethod = GenerateNativeInitCjObjectFunc(*fd, false);
        } else if (ctor->astKind == ASTKind::VAR_DECL) {
            auto varDecl = As<ASTKind::VAR_DECL>(ctor.get());
            CJC_NULLPTR_CHECK(varDecl);
            nativeMethod = GenerateNativeInitCjObjectFuncForEnumCtorNoParams(enumDecl, *varDecl);
        }
        generatedDecls.push_back(std::move(nativeMethod));
    }
}

void JavaDesugarManager::GenerateForCJEnumMapping(AST::EnumDecl& enumDecl)
{
    CJC_ASSERT(IsCJMapping(enumDecl));

    GenerateNativeInitCJObjectEnumCtor(enumDecl);

    for (auto& member : enumDecl.GetMemberDecls()) {
        if (member->TestAttr(Attribute::IS_BROKEN) || !member->TestAttr(Attribute::PUBLIC)) {
            continue;
        }
        if (auto fd = As<ASTKind::FUNC_DECL>(member.get())) {
            generatedDecls.push_back(GenerateNativeMethod(*fd, enumDecl));
        } else if (member->astKind == ASTKind::PROP_DECL && !member->TestAttr(Attribute::COMPILER_ADD)) {
            const PropDecl& propDecl = *StaticAs<ASTKind::PROP_DECL>(member.get());
            const OwnedPtr<FuncDecl>& funcDecl = propDecl.getters[0];
            auto getSignature = GetJniMethodNameForProp(propDecl, false);
            auto nativeMethod = GenerateNativeMethod(*funcDecl.get(), enumDecl);
            if (nativeMethod != nullptr) {
                nativeMethod->identifier = getSignature;
                generatedDecls.push_back(std::move(nativeMethod));
            }
        }
    }

    generatedDecls.push_back(GenerateCJMappingNativeDeleteCjObjectFunc(enumDecl));
}

OwnedPtr<FuncDecl> JavaDesugarManager::GenerateInterfaceFwdclassMethod(
    AST::ClassDecl& fwdclassDecl, FuncDecl& interfaceFuncDecl)
{
    auto funcDecl = ASTCloner::Clone(Ptr<FuncDecl>(&interfaceFuncDecl));
    funcDecl->DisableAttr(Attribute::ABSTRACT, Attribute::OPEN);
    funcDecl->EnableAttr(Attribute::PUBLIC, Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD);

    DesugarJavaMirrorMethod(*funcDecl, fwdclassDecl);
    funcDecl->outerDecl = Ptr<Decl>(&fwdclassDecl);

    return funcDecl;
}

void JavaDesugarManager::GenerateInterfaceFwdclassBody(AST::ClassDecl& fwdclassDecl, AST::InterfaceDecl& interfaceDecl)
{
    InsertJavaRefVarDecl(fwdclassDecl);
    InsertJavaMirrorCtor(fwdclassDecl, true);
    for (auto& decl : interfaceDecl.GetMemberDecls()) {
        if (FuncDecl* fd = As<ASTKind::FUNC_DECL>(decl.get());
            fd && !fd->TestAttr(Attribute::CONSTRUCTOR) && !fd->TestAttr(Attribute::STATIC)) {
            fwdclassDecl.body->decls.push_back(GenerateInterfaceFwdclassMethod(fwdclassDecl, *fd));
        } else if (auto prop = As<ASTKind::PROP_DECL>(decl.get())) {
            // not support yet
            auto message = "property in interface";
            diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported, *prop, message);
        }
    }
}

void JavaDesugarManager::GenerateForCJInterfaceMapping(AST::InterfaceDecl& interfaceDecl)
{
    auto fwdclassDecl = MakeOwned<ClassDecl>();
    fwdclassDecl->identifier = interfaceDecl.identifier.Val() + "_fwd";
    fwdclassDecl->identifier.SetPos(interfaceDecl.identifier.Begin(), interfaceDecl.identifier.End());
    fwdclassDecl->fullPackageName = interfaceDecl.fullPackageName;
    fwdclassDecl->moduleName = ::Cangjie::Utils::GetRootPackageName(interfaceDecl.fullPackageName);
    fwdclassDecl->curFile = interfaceDecl.curFile;

    fwdclassDecl->inheritedTypes.emplace_back(CreateRefType(interfaceDecl));

    fwdclassDecl->ty = typeManager.GetClassTy(*fwdclassDecl, interfaceDecl.ty->typeArgs);
    auto classLikeTy = DynamicCast<ClassLikeTy*>(interfaceDecl.ty);
    CJC_ASSERT(classLikeTy);
    classLikeTy->directSubtypes.insert(fwdclassDecl->ty);

    fwdclassDecl->EnableAttr(Attribute::PUBLIC, Attribute::COMPILER_ADD, Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD);

    fwdclassDecl->body = MakeOwned<ClassBody>();
    GenerateInterfaceFwdclassBody(*fwdclassDecl, interfaceDecl);

    generatedDecls.push_back(std::move(fwdclassDecl));
}

void JavaDesugarManager::GenerateFwdClassInCJMapping(File& file) 
{
    for (auto& decl : file.decls) {
        if (!decl.get()->TestAttr(Attribute::PUBLIC) || decl.get()->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto interfaceDecl = As<ASTKind::INTERFACE_DECL>(decl.get());
        if (interfaceDecl && IsCJMapping(*interfaceDecl)) {
            GenerateForCJInterfaceMapping(*interfaceDecl);
        }
    }
}

void JavaDesugarManager::GenerateInCJMapping(File& file)
{
    for (auto& decl : file.decls) {
        if (!decl.get()->TestAttr(Attribute::PUBLIC) || decl.get()->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto astDecl = As<ASTKind::DECL>(decl.get());
        if (astDecl && astDecl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto structDecl = As<ASTKind::STRUCT_DECL>(decl.get());
        if (file.curPackage.get()->isInteropCJPackageConfig && !structDecl->symbol->isNeedExposedToInterop) {
            continue;
        }
        if (structDecl && IsCJMapping(*structDecl)) {
            GenerateForCJStructOrClassTypeMapping(file, structDecl);
            continue;
        }
        auto enumDecl = As<ASTKind::ENUM_DECL>(decl.get());
        if (enumDecl && IsCJMapping(*enumDecl)) {
            GenerateForCJEnumMapping(*enumDecl);
            continue;
        }
        auto classDecl = As<ASTKind::CLASS_DECL>(decl.get());
        if (classDecl && IsCJMapping(*classDecl)) {
            GenerateForCJStructOrClassTypeMapping(file, classDecl);
            continue;
        }
    }
}

void JavaDesugarManager::DesugarInCJMapping(File& file)
{
    for (auto& decl : file.decls) {
        if (!decl.get()->TestAttr(Attribute::PUBLIC) || decl.get()->TestAttr(Attribute::IS_BROKEN) ||
            !JavaSourceCodeGenerator::IsDeclAppropriateForGeneration(*decl.get()) || !IsCJMapping(*decl.get())) {
            continue;
        }

        const std::string fileJ = decl.get()->identifier.Val() + ".java";
        auto codegen = JavaSourceCodeGenerator(decl.get(), mangler, javaCodeGenPath, fileJ,
            GetCangjieLibName(outputLibPath, decl.get()->GetFullPackageName()),
                              file.curPackage.get()->isInteropCJPackageConfig);
        codegen.Generate();
    }
}

} // namespace Cangjie::Interop::Java
