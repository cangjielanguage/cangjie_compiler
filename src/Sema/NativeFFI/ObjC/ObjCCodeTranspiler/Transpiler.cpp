// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements class for objc code generation.
 */

#include "Transpiler.h"
#include "Emitter.h"
#include "NativeFFI/ObjC/Utils/ASTFactory.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "cangjie/Utils/FileUtil.h"
#include <set>

namespace Cangjie::Interop::ObjC {

using namespace Cangjie;
using namespace AST;
using namespace Native::FFI;
using std::string;

Transpiler::Transpiler(InteropContext& ctx, Ptr<Decl> declArg, const std::string& outputFilePath,
    const std::string& cjLibOutputPath)
    : outputFilePath(outputFilePath.empty() ? DEFAULT_OUTPUT_DIR : outputFilePath),
      cjLibOutputPath(cjLibOutputPath),
      decl(declArg),
      ctx(ctx)
{
    if (outputFilePath.empty()) {
        Warningln(DEFAULT_OUTPUT_DIR_WARNING);
    }
}

bool Transpiler::CheckFunction(OwnedPtr<Decl>& arg) const
{
    if (ctx.factory.IsGeneratedMember(*arg)) {
        return false;
    }

    if (!arg->TestAttr(Attribute::PUBLIC)) {
        return false;
    }
    if (arg->astKind == ASTKind::FUNC_DECL &&
        !arg->TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::FINALIZER)) {
        return true;
    }

    return false;
}

bool Transpiler::CheckCtor(OwnedPtr<Decl>& arg) const
{
    if (!arg->TestAttr(Attribute::PUBLIC)) {
        return false;
    }
    if (ctx.factory.IsGeneratedMember(*arg.get())) {
        return false;
    }

    if (!arg->TestAttr(Attribute::CONSTRUCTOR)) {
        return false;
    }

    if (arg->astKind != ASTKind::FUNC_DECL) {
        // skip primary ctor, as it is desugared to init already
        return false;
    }

    const FuncDecl& funcDecl = *StaticAs<ASTKind::FUNC_DECL>(arg.get());
    if (!funcDecl.funcBody) {
        return false;
    }

    return true;
}

bool Transpiler::CheckProp(OwnedPtr<Decl>& arg) const
{
    if (arg->astKind != ASTKind::VAR_DECL && arg->astKind != ASTKind::PROP_DECL) {
        return false;
    }

    if (!arg->TestAttr(Attribute::PUBLIC)) {
        return false;
    }

    if (ctx.factory.IsGeneratedNativeHandleField(*arg)) {
        return false;
    }

    return true;
}

void Transpiler::CollectDependencies(ModalTy ty)
{
    if (ctx.typeMapper.IsObjCObjectType(*ty)) {
        // ObjCId is `id` representative which is builtin type for Objective-C
        if (TypeMapper::IsObjCId(*ty)) {
            return;
        }

        if (!ty->IsCoreOptionType()) {
            auto declTy = Ty::GetDeclOfTy(ty);
            if (declTy->identifier != ctx.nameGenerator.GetObjCDeclName(*decl)) {
                dependencies.insert(declTy);
            }
        }
    }
    for (auto&& typeArg : ty->typeArgs) {
        CollectDependencies(typeArg);
    }
}

std::string Transpiler::FormatCJLibName()
{
    auto cjLibName = Native::FFI::GetCangjieLibName(cjLibOutputPath, decl->fullPackageName, false);
    // Crutch that solves issue when cjLibOutputPath is dir or not provided.
    if (cjLibName.find("lib", 0) != 0) {
        cjLibName = "lib" + cjLibName;
    }
    size_t extIdx = cjLibName.find_last_of(".");
    const auto& sharedLibraryExtension = ctx.sharedLibraryExtension;
    if (extIdx == std::string::npos || cjLibName.substr(extIdx) != sharedLibraryExtension) {
        cjLibName += sharedLibraryExtension;
    }
    return cjLibName;
}

void Transpiler::ProcessPreamble(
    struct EmittableObjCClassMetainfo metainfo,
    bool hasImplicitImplParent)
{
    Emitter::EmitFileImport(sourceImport, metainfo.className);
    Emitter::EmitSourceImport(sourceImport);
    Emitter::EmitHeaderImport(headerImport);
    for (auto i : metainfo.interfaces) {
        Emitter::EmitFileImport(headerImport, i);
    }

    Emitter::EmitInterfaceDeclaration(headerBody, metainfo);
    Emitter::EmitFileImport(headerImport, metainfo.superclassName);
    Emitter::EmitImplementationDeclaration(sourceBody, metainfo);
    Emitter::EmitSyntheticHeaderFunctionDecls(headerBody);
    if (!hasImplicitImplParent) {
        Emitter::EmitSyntheticRefsDealloc(headerBody);
    }
}

void Transpiler::ProcessSuffix(
    std::string cjLibName,
    struct EmittableObjCClassMetainfo metainfo,
    bool hasImplicitImplParent)
{
    Emitter::EmitAdditionalStaticReferences(staticReferences);

    if (!hasImplicitImplParent) {
        auto deleteCJObjectMangledName = ctx.nameGenerator.GenerateDeleteCjObjectName(*decl);
        Emitter::EmitDeleteCJObject(defaultFuncImpls, deleteCJObjectMangledName);
        Emitter::EmitDeleteCJObjectInitialization(funcInitialization, deleteCJObjectMangledName);
        Emitter::EmitDealloc(defaultFuncImpls);
    }

    Emitter::EmitInitialize(initialize, cjLibName, metainfo);
}

void Transpiler::ProcessMemberDecls(
    struct EmittableObjCClassMetainfo metainfo,
    ClassDecl* classDecl)
{
    std::set<std::vector<std::string>> generatedCtors = {};

    for (OwnedPtr<Decl>& declPtr : decl->GetMemberDecls()) {
        switch (declPtr->astKind) {
            case ASTKind::FUNC_DECL:
                if (CheckFunction(declPtr)) {
                    FuncDecl& funcDecl = *StaticAs<ASTKind::FUNC_DECL>(declPtr.get());
                    auto funcMeta = GetObjCFuncMetainfo(funcDecl);

                    Emitter::EmitObjCFuncHeaderFunctionDecl(headerBody, funcMeta);
                    Emitter::EmitObjCFuncInitialization(funcInitialization, funcMeta);
                    Emitter::EmitDefaultFunctionImplementation(defaultFuncImpls, funcMeta);

                    CollectDependencies(funcDecl.GetTy());
                }
                if (CheckCtor(declPtr)) {
                    FuncDecl& funcDecl = *StaticAs<ASTKind::FUNC_DECL>(declPtr.get());
                    if (funcDecl.funcBody && funcDecl.funcBody->retType) {
                        auto funcMeta = GetObjCCtorMetainfo(funcDecl);

                        Emitter::EmitCtor(ctors, funcMeta, metainfo);
                        Emitter::EmitObjCFuncInitialization(funcInitialization, funcMeta);
                        Emitter::EmitObjCFuncHeaderFunctionDecl(headerBody, funcMeta);

                        CollectDependencies(funcDecl.GetTy());
                        generatedCtors.insert(funcMeta.selectorComponents);
                    }
                }
                break;
            case ASTKind::VAR_DECL:
                if (CheckProp(declPtr)) {
                    VarDecl& propDecl = *StaticAs<ASTKind::VAR_DECL>(declPtr.get());
                    auto propMeta = GetObjCPropMetainfoFromProp(propDecl);
                    Emitter::EmitObjCFuncInitialization(funcInitialization, propMeta.getter);
                    Emitter::EmitDefaultFunctionImplementation(defaultFuncImpls, propMeta.getter);
                    Emitter::EmitProperty(headerBody, propMeta);
                    Emitter::EmitObjCFuncHeaderFunctionDecl(headerBody, propMeta.getter);
                    if (propMeta.isReadwrite) {
                        Emitter::EmitObjCFuncInitialization(funcInitialization, propMeta.setter);
                        Emitter::EmitDefaultFunctionImplementation(defaultFuncImpls, propMeta.setter);
                        Emitter::EmitObjCFuncHeaderFunctionDecl(headerBody, propMeta.setter);
                    }
                    CollectDependencies(propDecl.GetTy());
                }
                break;
            case ASTKind::PROP_DECL:
                if (CheckProp(declPtr)) {
                    VarDecl& propDecl = *StaticAs<ASTKind::PROP_DECL>(declPtr.get());
                    auto propMeta = GetObjCPropMetainfoFromProp(propDecl);
                    Emitter::EmitObjCFuncInitialization(funcInitialization, propMeta.getter);
                    Emitter::EmitDefaultFunctionImplementation(defaultFuncImpls, propMeta.getter);
                    Emitter::EmitProperty(headerBody, propMeta);
                    Emitter::EmitObjCFuncHeaderFunctionDecl(headerBody, propMeta.getter);
                    if (propMeta.isReadwrite) {
                        Emitter::EmitObjCFuncInitialization(funcInitialization, propMeta.setter);
                        Emitter::EmitDefaultFunctionImplementation(defaultFuncImpls, propMeta.setter);
                        Emitter::EmitObjCFuncHeaderFunctionDecl(headerBody, propMeta.setter);
                    }
                    CollectDependencies(propDecl.GetTy());
                }
                break;
            default:
                break;
        }
    }
    for (auto &declPtr : ctx.genDecls) {
        if (declPtr->curFile != decl->curFile || !declPtr->TestAttr(Attribute::C) ||
            declPtr->TestAttr(Attribute::FOREIGN)) {
            continue;
        }

        switch (declPtr->astKind) {
            case ASTKind::FUNC_DECL:
                if (CheckFunction(declPtr)) {
                    Emitter::EmitObjCFuncStaticReference(staticReferences,
                        GetObjCFuncMetainfo(*StaticAs<ASTKind::FUNC_DECL>(declPtr.get()), true));
                }
                break;
            default:
                break;
        }
    }
    // suffix
    for (auto d : dependencies) {
        auto identifier = ctx.nameGenerator.GetObjCDeclName(*d);
        (d->astKind == ASTKind::INTERFACE_DECL)
            ? Emitter::EmitForwardDeclarationProtocol(dependenciesStream, identifier)
            : Emitter::EmitForwardDeclaration(dependenciesStream, identifier);
        Emitter::EmitFileImport(sourceImport, identifier);
    }
    std::vector<std::string> visited;
    for (auto td : typedefs) {
        if (std::find(visited.begin(), visited.end(), td) == visited.end()) {
            Emitter::EmitTypedef(headerImport, td);
            visited.emplace_back(td);
        }
    }

    if (classDecl) {
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

            auto funcMeta = GetObjCFuncMetainfo(*ctor);
            funcMeta.retType = ObjCParamMapper::ID_TYPE;

            std::string result = "";
            Emitter::EmitDoesNotRecognizeSelectorCtor(ctors, funcMeta);
            Emitter::EmitObjCFuncHeaderFunctionDecl(headerBody, funcMeta);
            generatedCtors.insert(selectorComponents);
        }
    }
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
void Transpiler::Generate()
{
    auto cjLibName = FormatCJLibName();
    auto classDecl = dynamic_cast<ClassDecl*>(decl.get());
    auto metainfo = GetObjCClassMetainfo(classDecl);

    auto hasImplicitImplParent = HasImplSuperClass(*classDecl);

    ProcessPreamble(metainfo, hasImplicitImplParent);
    ProcessMemberDecls(metainfo, classDecl);
    ProcessSuffix(cjLibName, metainfo, hasImplicitImplParent);

    WriteToFile();
}

struct EmittableObjCClassMetainfo Transpiler::GetObjCClassMetainfo(ClassDecl* classDecl)
{
    auto className = ctx.nameGenerator.GetObjCDeclName(*classDecl);

    Ptr<ClassDecl> superClassPtr = classDecl ? classDecl->GetSuperClassDecl() : Ptr<ClassDecl>(nullptr);
    bool isClassInheritedFromClass = superClassPtr && superClassPtr->identifier.Val() != Cangjie::OBJECT_NAME;
    auto superclassName = isClassInheritedFromClass ? ctx.nameGenerator.GetObjCDeclName(*superClassPtr) : "";

    auto isFinal = !decl->IsOpen();

    std::set<Ptr<Cangjie::AST::InterfaceTy>> interfaces = classDecl
        ? classDecl->GetSuperInterfaceTys()
        : std::set<Ptr<Cangjie::AST::InterfaceTy>>();

    std::vector<Ptr<Cangjie::AST::InterfaceTy>> interfacesVec;
    std::copy_if (interfaces.begin(), interfaces.end(),
        std::back_inserter(interfacesVec),
        [](Ptr<Cangjie::AST::InterfaceTy> i) { return !TypeMapper::IsObjCId(*i); }
    );

    std::vector<std::string> interfaceNames;
    for (auto i : interfacesVec) {
        interfaceNames.push_back(this->ctx.nameGenerator.GetObjCDeclName(*i->decl));
    }

    EmittableObjCClassMetainfo eocm;
    eocm.className = className;
    eocm.superclassName = superclassName;
    eocm.isFinal = isFinal;
    eocm.interfaces = interfaceNames;
    return eocm;
}

struct EmittableObjCPropMetainfo Transpiler::GetObjCPropMetainfoFromProp(VarDeclAbstract& varDecl)
{
    EmittableObjCPropMetainfo empm = EmittableObjCPropMetainfo();
    empm.isStatic = varDecl.TestAttr(Attribute::STATIC);
    empm.type = ObjCParamMapper::MapCJTypeToObjCType(typedefs, *varDecl.GetTy());
    empm.isReadwrite = varDecl.isVar;
    empm.name = ctx.nameGenerator.GetObjCDeclName(varDecl);
    auto bridge = ctx.typeMapper.IsObjCObjectType(*varDecl.GetTy()) || ctx.typeMapper.IsObjCBlock(*varDecl.GetTy());

    empm.getter = ObjCParamMapper::GetGetterForProp(empm, ctx.nameGenerator.GetObjCGetterName(varDecl),
        ctx.nameGenerator.GetFieldGetterWrapperName(varDecl), bridge);

    if (empm.isReadwrite) {
        empm.setter = ObjCParamMapper::GetSetterForProp(empm, varDecl.GetTy(),
            ctx.nameGenerator.GetObjCSetterName(varDecl),
            ctx.nameGenerator.GetFieldSetterWrapperName(varDecl));
    }

    return empm;
}

struct EmittableObjCFuncMetainfo Transpiler::GetObjCFuncMetainfo(FuncDecl& funcDecl, bool mangled)
{
    EmittableObjCFuncMetainfo eofm;

    eofm.isStatic            = funcDecl.TestAttr(Attribute::STATIC);
    eofm.selectorComponents  = ctx.nameGenerator.GetObjCDeclSelectorComponents(funcDecl);
    eofm.mangledIdentifier   = mangled ? funcDecl.identifier : ctx.nameGenerator.GenerateMethodWrapperName(funcDecl);
    eofm.identifier          = eofm.selectorComponents[0];
    auto& retTy              = *StaticCast<const FuncTy*>(funcDecl.DataTy())->retTy;
    eofm.retType             = ObjCParamMapper::MapCJTypeToObjCType(typedefs, retTy);
    eofm.paramsDecl          = ObjCParamMapper::GenerateFuncParamLists(typedefs, funcDecl.funcBody->paramLists,
        eofm.selectorComponents, FunctionListFormat::DECLARATION,
        eofm.isStatic ? ObjCFunctionType::STATIC : ObjCFunctionType::INSTANCE,
        GetForeignNameAnnotation(funcDecl) != nullptr);
    auto collectTypes = [this](const OwnedPtr<FuncParam>& fp) -> std::string {
        return ObjCParamMapper::MapCJTypeToObjCType(typedefs, *fp->GetTy());
    };
    eofm.paramStaticRef      = funcDecl.funcBody->paramLists[0]->params.size() == 0
        ? "()"
        : JoinVec<OwnedPtr<FuncParam>>(funcDecl.funcBody->paramLists[0]->params,
            collectTypes, ",", "(", ")", false);
    auto collectNames = [](const OwnedPtr<FuncParam>& fp) -> std::string {
        return fp->identifier;
    };
    eofm.callingParams       = JoinVec<OwnedPtr<FuncParam>>(funcDecl.funcBody->paramLists[0]->params,
        collectNames, ", ", ", ", "", false);
    eofm.convertedParams     = ObjCParamMapper::ConvertParamsListToArgsListToString(funcDecl.funcBody->paramLists,
        !eofm.isStatic);
    eofm.bridge              = ctx.typeMapper.IsObjCObjectType(retTy) || ctx.typeMapper.IsObjCBlock(retTy);
    return eofm;
}

struct EmittableObjCFuncMetainfo Transpiler::GetObjCCtorMetainfo(FuncDecl& funcDecl)
{
    EmittableObjCFuncMetainfo eofm;

    const auto ctor = ctx.factory.GetGeneratedImplCtor(*decl, funcDecl).get();
    CJC_ASSERT(ctor);
    const auto selectorComponents = ctx.nameGenerator.GetObjCDeclSelectorComponents(funcDecl);
    CJC_ASSERT(selectorComponents.size() > 0);
    // wrapper name MUST use generated ctor
    const auto cjWrapperName = ctx.nameGenerator.GenerateInitCjObjectName(*ctor);

    eofm.isStatic            = ctor->TestAttr(Attribute::STATIC);
    eofm.selectorComponents  = selectorComponents;
    eofm.mangledIdentifier   = cjWrapperName;
    eofm.identifier          = eofm.selectorComponents[0];
    eofm.retType             = ObjCParamMapper::ID_TYPE;
    eofm.paramsDecl          = ObjCParamMapper::GenerateFuncParamLists(typedefs, funcDecl.funcBody->paramLists,
        eofm.selectorComponents, FunctionListFormat::DECLARATION,
        eofm.isStatic ? ObjCFunctionType::STATIC : ObjCFunctionType::INSTANCE,
        GetForeignNameAnnotation(funcDecl) != nullptr);
    auto collectTypes = [this](const OwnedPtr<FuncParam>& fp) -> std::string {
        return ObjCParamMapper::MapCJTypeToObjCType(typedefs, *fp->GetTy());
    };
    eofm.paramStaticRef      = JoinVec<OwnedPtr<FuncParam>>(funcDecl.funcBody->paramLists[0]->params,
        collectTypes, ",", "(", ")", false);
    auto collectNames = [](const OwnedPtr<FuncParam>& fp) -> std::string {
        return ObjCParamMapper::GenerateArgumentCast(*fp->GetTy(), fp->identifier);
    };
    eofm.callingParams       = JoinVec<OwnedPtr<FuncParam>>(funcDecl.funcBody->paramLists[0]->params,
        collectNames, ", ", ", ", "", false);
    eofm.bridge              = false;
    return eofm;
}

void Transpiler::WriteToFile()
{
    WriteToHeader(
        Emitter::GENERATED_FILE_PREAMBULE   +
        headerImport.str()                  +
        dependenciesStream.str()            +
        headerBody.str()                    +
        Emitter::END
    );
    WriteToSource(
        Emitter::GENERATED_FILE_PREAMBULE   +
        sourceImport.str()                  +
        staticReferences.str()              +
        sourceBody.str()                    +
        ctors.str()                         +
        initialize.str()                    +
        defaultFuncImpls.str()              +
        Emitter::END
    );
}

void Transpiler::WriteToHeader(std::string content)
{
    auto objCDeclName = ctx.nameGenerator.GetObjCDeclName(*decl);
    auto headerPath = FileUtil::JoinPath(outputFilePath, objCDeclName + ".h");
    FileUtil::WriteToFile(headerPath, content);
}

void Transpiler::WriteToSource(std::string content)
{
    auto objCDeclName = ctx.nameGenerator.GetObjCDeclName(*decl);
    auto sourcePath = FileUtil::JoinPath(outputFilePath, objCDeclName + ".m");
    FileUtil::WriteToFile(sourcePath, content);
}
} // namespace Cangjie::Interop::ObjC
