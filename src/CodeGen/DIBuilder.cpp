// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "DIBuilder.h"

#include "Base/CGTypes/CGType.h"
#include "Utils/CGUtils.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
namespace CodeGen {
DIBuilder::DIBuilder(CGModule& cgMod) : LLVMDIBuilder(*cgMod.GetLLVMModule()), cgMod(cgMod)
{
    enabled = cgMod.GetCGContext().GetCompileOptions().enableCompileDebug ||
        cgMod.GetCGContext().GetCompileOptions().enableCoverage;
    enableLineInfo = cgMod.GetCGContext().GetCompileOptions().displayLineInfo;
}

namespace {
std::string RemovePathPrefix(const std::string& absolutePath, const std::vector<std::string>& removedPathPrefix)
{
    // for the path: aa/bb/cc.cj, the absolutePath is "aa/bb", and the last "/" is need to be added.
#ifdef _WIN32
    char split = '\\';
    std::string res = absolutePath + split;
    std::string tempRes{};
    tempRes.reserve(res.size());
    std::transform(res.begin(), res.end(), std::back_inserter(tempRes), &tolower);
    for (auto prefix : removedPathPrefix) {
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), &tolower);
        auto pos = tempRes.find(prefix);
#else
    char split = '/';
    std::string res = absolutePath + split;
    for (auto& prefix : removedPathPrefix) {
        auto pos = res.find(prefix);
#endif
        // when pos is 0, it means the path starts with the prefix.
        if (pos == 0) {
            res = res.substr(prefix.length());
            if (res.empty()) {
                return res;
            }
            res = res[0] == split ? res.substr(1) : res;
            break;
        }
    }
    // remove the last symbol.
    return res.empty() ? res : res.substr(0, res.size() - 1);
}

std::string GenerateLinkageName(const CHIR::GlobalVar& variable)
{
    std::string linkageName = variable.GetPackageName() + "::";
    if (variable.TestAttr(CHIR::Attribute::STATIC)) {
        auto typeIdentifier = variable.GetParentCustomTypeDef()->GetSrcCodeIdentifier();
        linkageName += typeIdentifier;
        if (!variable.GetParentCustomTypeDef()->GetGenericTypeParams().empty()) {
            linkageName += "<";
            for (auto genericTy : variable.GetParentCustomTypeDef()->GetGenericTypeParams()) {
                linkageName += genericTy->GetSrcCodeIdentifier() + ",";
            }
            linkageName.pop_back();
            linkageName += ">";
        }
        linkageName += "::";
    }
    return linkageName += variable.GetSrcCodeIdentifier();
}

const CHIR::Type* GetOptionLikeArgTy(const CHIR::EnumType& enumTy, CGModule& cgMod)
{
    auto cgType = StaticCast<const CGEnumType*>(CGType::GetOrCreate(cgMod, &enumTy));
    size_t index = cgType->IsAntiOptionLike() ? 1 : 0;
    auto someCtor = enumTy.GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder())[index];
    auto argTy = DeRef(*someCtor.funcType->GetParamTypes()[0]);
    return argTy;
}

bool IsBoxSelf(const CHIR::Type& ty, CGModule& cgMod)
{
    if (!ty.IsBox()) {
        return false;
    }
    auto boxedTy = StaticCast<const CHIR::BoxType*>(&ty);
    auto baseTy = boxedTy->GetBaseType();
    auto cgType = CGType::GetOrCreate(cgMod, baseTy);
    if (baseTy->IsEnum() && StaticCast<const CGEnumType*>(cgType)->IsOptionLike()) {
        auto enumTy = StaticCast<const CHIR::EnumType*>(baseTy);
        auto argTy = DeRef(*GetOptionLikeArgTy(*enumTy, cgMod));
        return argTy == baseTy || argTy == boxedTy;
    }
    return false;
}

bool IsParaInitFunc(const CHIR::Value& chirFunc)
{
    auto chirFuncName = chirFunc.GetIdentifierWithoutPrefix();
    return chirFuncName.find(MANGLE_CANGJIE_PREFIX + MANGLE_FUNC_PARA_INIT_PREFIX) == 0;
}
} // namespace

void DIBuilder::CreateCompileUnit(const std::string& pkgName)
{
    std::string tempPkgName(pkgName);
    if (cgMod.GetCGContext().GetCompileOptions().enableCoverage) {
        // When compiled with '--test', files with the "_test.cj" suffix will also be compiled, which means the output
        // of '.gcno' file for the same package is affected by the '--test' option. Therefore, when compiled with the
        // '--test' option, the "$test" suffix is added to the corresponding '.gcno' file.
        // NB: in the "test-only" mode, packages are already suffixed with "$test".
        if (cgMod.GetCGContext().GetCompileOptions().enableCompileTest &&
            !cgMod.GetCGContext().GetCompileOptions().compileTestsOnly) {
            tempPkgName += SourceManager::testPkgSuffix;
        }
        // When compiled with the `--coverage` option, a corner case should be handled.
        // If the package name contains a "." symbol, e.g., "some.package.name",
        // a gcov note file will be generated
        // whose path is constructed by replacing the extension of the package name with the ".gcno",
        // i.e., "some.package.gcno".
        // However, since the ".name" is not a part of the extension, an incorrect file is generated here.
        // To fix this bug, we add an extra extension ".pkg" to the package name intentionally;
        // thus, a gcov file will always be created by replacing ".pkg" with ".gcno", i.e., "some.package.name.gcno".
        if (pkgName.find('.') != std::string::npos) {
            tempPkgName += ".pkg";
        }
    }
    defaultFile = createFile(pkgName, ".");
    diCompileUnit = createCompileUnit(static_cast<unsigned>(llvm::dwarf::DW_LANG_C_plus_plus_14),
        createFile(tempPkgName, "."), "Cangjie Compiler", false, "", 0u);
}

void DIBuilder::CreateNameSpace(const std::string& pkgName)
{
    diNamespace = createNameSpace(diCompileUnit, pkgName, false);
}

void DIBuilder::Finalize()
{
    if (!enabled && !enableLineInfo) {
        return;
    }
    finalize();
    lexicalBlocks.clear();
}

void DIBuilder::CreateGlobalVar(const CHIR::GlobalVar& variable)
{
    if (!cgMod.GetCGContext().GetCompileOptions().enableCompileDebug ||
        variable.TestAttr(CHIR::Attribute::NO_DEBUG_INFO)) {
        return;
    }
    CJC_ASSERT(!variable.TestAttr(CHIR::Attribute::IMPORTED));
    auto& position = variable.GetDebugLocation();
    auto ty = DeRef(*variable.GetType());
    llvm::DIFile* file = GetOrCreateFile(position);
    bool isReadOnly = variable.TestAttr(CHIR::Attribute::READONLY);
    llvm::DIType* type = GetOrCreateType(*ty, isReadOnly);
    auto llvmValue = llvm::cast<llvm::GlobalVariable>(cgMod.GetMappedCGValue(&variable)->GetRawValue());
    std::vector<uint64_t> expr;
    // To add the deref flag, need to guarantee the refType is not nullptr, which corresponds to Option<Ref>.None.
    if (IsReferenceType(*ty, cgMod)) {
        if (IsOptionOrOptionLike(*ty)) {
            if (!IsOption(*ty)) {
                type = CreatePointerType(type);
            }
        } else {
            expr.emplace_back(llvm::dwarf::DW_OP_deref);
        }
    }
    auto linkageName = GenerateLinkageName(variable);
    bool isMemberVar = variable.GetParentCustomTypeDef() != nullptr;
    llvm::DIGlobalVariableExpression* globalVar =
        createGlobalVariableExpression(diNamespace, variable.GetSrcCodeIdentifier(), linkageName, file,
            position.GetBeginPos().line, type, isMemberVar, true, createExpression(expr), nullptr);
    llvmValue->addDebugInfo(globalVar);
    cgMod.GetCGContext().AddGlobalsOfCompileUnit(variable.GetIdentifierWithoutPrefix());
}

void DIBuilder::CreateAnonymousTypeForGenericType(llvm::GlobalVariable* typeTemplate, const CHIR::Type& ty)
{
    // class/struct genericDef generated by init.
    bool isInterface = ty.IsClass() && StaticCast<const CHIR::ClassType*>(&ty)->GetClassDef()->IsInterface();
    if (!cgMod.GetCGContext().GetCompileOptions().enableCompileDebug) {
        return;
    }
    if (!ty.IsEnum() && !isInterface) {
        return;
    }
    ++genericID;
    llvm::DIType* type = GetOrCreateType(ty);
    llvm::DIGlobalVariableExpression* globalVar = createGlobalVariableExpression(
        diNamespace, "$" + std::to_string(genericID), "$" + std::to_string(genericID), defaultFile, 0, type, false);
    typeTemplate->addDebugInfo(globalVar);
}

llvm::DISubroutineType* DIBuilder::CreateDefaultFunctionType()
{
    return createSubroutineType(getOrCreateTypeArray({}));
}

#ifndef __APPLE__
static const CHIR::Type* GetOuterTypeOfMemberFunc(const CHIR::Func& func)
{
    const auto parent = func.GetParentCustomTypeDef();
    CJC_NULLPTR_CHECK(parent);
    if (parent->GetCustomKind() != CHIR::CustomDefKind::TYPE_EXTEND) {
        return parent->GetType();
    }
    const auto extendType = StaticCast<const CHIR::ExtendDef*>(parent)->GetExtendedType();
    CJC_NULLPTR_CHECK(extendType);
    return extendType;
}
#endif

void DIBuilder::SetSubprogram(const CHIR::Func* func, llvm::Function* function)
{
    if (!enabled && !enableLineInfo) {
        return;
    }
    CJC_ASSERT(function->getSubprogram() == nullptr);
    // reset the parameter number when generating a brand new one.
    paramNo = 1;
    auto funcTy = DeRef(*func->GetType());
    bool lineInfoOnly = !enabled && enableLineInfo;
    auto funcName = lineInfoOnly ? "" : func->GetIdentifierWithoutPrefix();
    // the name of parameter init function is useless for debug, and may cause duplicate identifier error
    auto funcIdentifier = lineInfoOnly || IsParaInitFunc(*func)
        ? "" 
        : GenerateGenericFuncName(func->GetSrcCodeIdentifier(), func->GetOriginalGenericTypeParams());
    auto rawMethod = func->Get<CHIR::WrappedRawMethod>();
    auto& position = rawMethod ? rawMethod->GetDebugLocation() : func->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    bool isGV = funcName.find(MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_VARIABLE_INIT_PREFIX) == 0;
    bool isCompilerAddInit = func->GetSrcCodeIdentifier() == "init" && func->TestAttr(CHIR::Attribute::NO_DEBUG_INFO);
    auto scopeLine = isGV ? 0 : position.GetBeginPos().line;
    scopeLine = isCompilerAddInit ? position.GetEndPos().line : scopeLine;
    auto scope = llvm::cast<llvm::DIScope>(diNamespace);
    llvm::DISubprogram* decl = nullptr;
#ifndef __APPLE__
    bool isMemberFunc = func->GetParentCustomTypeDef() != nullptr;
    if (isMemberFunc && cgMod.GetCGContext().GetCompileOptions().enableCompileDebug) {
        auto parentTy = GetOuterTypeOfMemberFunc(*func);
        bool isWrapperFunc = parentTy->IsClass() && StaticCast<const CHIR::ClassType*>(parentTy)->IsAutoEnv();
        if (!parentTy->IsPrimitive() && !isWrapperFunc) {
            scope = GetOrCreateType(*parentTy);
            auto realScope = scope->getTag() == llvm::dwarf::DW_TAG_typedef
                ? llvm::cast<llvm::DIDerivedType>(scope)->getBaseType()
                : scope;
            auto compositeType = llvm::cast<llvm::DICompositeType>(realScope);
            for (auto element : compositeType->getElements()) {
                if (element->getTag() != llvm::dwarf::DW_TAG_subprogram) {
                    continue;
                }
                auto memberSP = llvm::cast<llvm::DISubprogram>(element);
                if (memberSP->getLinkageName() == funcName) {
                    decl = memberSP;
                    break;
                }
            }
        }
    }
#endif
    auto funcType = lineInfoOnly || func->TestAttr(CHIR::Attribute::NO_DEBUG_INFO)
        ? CreateDefaultFunctionType()
        : CreateFuncType(StaticCast<CHIR::FuncType*>(funcTy));
    llvm::DINode::DIFlags flags = llvm::DINode::FlagPrototyped;
    flags |= isGV ? llvm::DINode::FlagArtificial : llvm::DINode::FlagZero;
    llvm::DISubprogram::DISPFlags spFlags = llvm::DISubprogram::SPFlagDefinition;
    if (func->GetSrcCodeIdentifier() == INST_VIRTUAL_FUNC || func->GetSrcCodeIdentifier() == GENERIC_VIRTUAL_FUNC) {
        // To properly step in to the closure function body, need to set a valid value for scopeLine.
        scopeLine = 1;
    }
    llvm::DISubprogram* sp = createFunction(
        scope, funcIdentifier, funcName, diFile, scopeLine, funcType, scopeLine, flags, spFlags, nullptr, decl);
    function->setSubprogram(sp);
    curScope = sp;
}

void DIBuilder::FinalizeSubProgram(llvm::Function& function)
{
    lexicalBlocks.clear();
    curLocation = {0, 0};
    curScope = nullptr;
    finalizeSubprogram(function.getSubprogram());
    if (!needSubprogram) {
        function.setSubprogram(nullptr);
    }
    needSubprogram = false;
}

void DIBuilder::EmitDeclare(const CHIR::Debug& debugNode, llvm::BasicBlock& curBB, bool pointerWrapper)
{
    CJC_ASSERT(cgMod.GetCGContext().GetCompileOptions().enableCompileDebug);

    if (curBB.getParent()->getSubprogram() == nullptr) {
        return;
    }
    if (debugNode.GetValue()->IsParameter()) {
        CreateParameter(debugNode, curBB, pointerWrapper);
        return;
    }
    auto identifier = debugNode.GetSrcCodeIdentifier();
    auto position = debugNode.GetDebugLocation();
    auto scope = GetOrCreateScope(position, curBB);
    auto diFile = GetOrCreateFile(position);
    auto ty = DeRef(*debugNode.GetValue()->GetType());
    bool isReadOnly = debugNode.GetValue()->TestAttr(CHIR::Attribute::READONLY);
    auto type = GetOrCreateType(*ty, isReadOnly);
    std::vector<uint64_t> expr;
    auto flags = llvm::DINode::FlagZero;
    // To add the deref flag, need to guarantee the refType is not nullptr, which corresponds to Option<Ref>.None.
    auto hasSize = CGType::GetOrCreate(cgMod, ty)->GetSize();
    if (IsReferenceType(*ty, cgMod) || ty->IsGeneric() || !hasSize) {
        // for the generic option(does not have size), does not need wrap a pointer, because it has typeInfo.
        if (IsOptionOrOptionLike(*ty) && hasSize) {
            if (!IsOption(*ty)) {
                type = CreatePointerType(type);
            }
        } else {
            flags |= llvm::DINode::FlagObjectPointer;
            expr.emplace_back(llvm::dwarf::DW_OP_deref);
        }
    } else if (pointerWrapper) {
        expr.emplace_back(llvm::dwarf::DW_OP_deref);
    }
    auto llvmValue = **(cgMod | debugNode.GetValue());
    auto localVariable =
        createAutoVariable(scope, identifier, diFile, position.GetBeginPos().line, type, true, flags, 0u);
    insertDeclare(llvmValue, localVariable, createExpression(expr),
        llvm::DILocation::get(
            cgMod.GetLLVMContext(), position.GetBeginPos().line, position.GetBeginPos().column, scope),
        &curBB);
}

void DIBuilder::EmitUnBoxDeclare(const CHIR::Expression& exprNode, const CHIR::Type& ty, const std::string& name,
    llvm::Value* value, llvm::BasicBlock& curBB)
{
    auto position = exprNode.GetDebugLocation();
    auto scope = GetOrCreateScope(position, curBB);
    auto diFile = GetOrCreateFile(position);
    auto type = GetOrCreateType(ty);
    // the value is a pointer.
    auto flags = llvm::DINode::FlagObjectPointer;
    std::vector<uint64_t> expr = {llvm::dwarf::DW_OP_deref};
    if (IsReferenceType(ty, cgMod)) {
        type = CreatePointerType(type);
        // this case for optionLikeRef.
        if (!ty.IsEnum()) {
            expr.emplace_back(llvm::dwarf::DW_OP_deref);
        }
    }
    auto localVariable = createAutoVariable(scope, name, diFile, position.GetBeginPos().line, type, true, flags, 0u);
    insertDeclare(value, localVariable, createExpression(expr),
        llvm::DILocation::get(
            cgMod.GetLLVMContext(), position.GetBeginPos().line, position.GetBeginPos().column, scope),
        &curBB);
}

void DIBuilder::EmitGenericDeclare(
    const CHIR::GenericType& genericTy, llvm::Value* arg, llvm::BasicBlock& curBB, size_t index)
{
    auto scope = curBB.getParent()->getSubprogram();
    auto name = "$G_" + std::to_string(index);
    auto type = createTypedef(CreateRefType(), genericTy.GetSrcCodeIdentifier(), defaultFile, 0, diCompileUnit);
    auto localVariable = createAutoVariable(scope, name, defaultFile, 0u, type, true, llvm::DINode::FlagArtificial);
    insertDeclare(
        arg, localVariable, createExpression({}), llvm::DILocation::get(cgMod.GetLLVMContext(), 0u, 0u, scope), &curBB);
}

void DIBuilder::CreateParameter(const CHIR::Debug& debugNode, llvm::BasicBlock& curBB, bool pointerWrapper)
{
    bool isEnv = debugNode.GetSrcCodeIdentifier() == "$env";
    auto paraTy = DeRef(*debugNode.GetValue()->GetType());
    // skip DebugNode generation when env is empty.
    if (isEnv && IsCoreObject(*StaticCast<CHIR::ClassType*>(paraTy)->GetClassDef())) {
        return;
    }
    CJC_NULLPTR_CHECK(curBB.getParent());
    auto scope = curBB.getParent()->getSubprogram();
    std::vector<uint64_t> expr;
    auto& position = debugNode.GetDebugLocation();
    auto file = GetOrCreateFile(position);
    auto type = GetOrCreateType(*paraTy, true);
    auto name = debugNode.GetSrcCodeIdentifier();
    auto llvmValue = **(cgMod | debugNode.GetValue());
    if (name == "this") {
        bool isTrivialEnum = paraTy->IsEnum() &&
            StaticCast<const CHIR::EnumType*>(paraTy)->GetEnumDef()->IsAllCtorsTrivial();
        bool isSizeableGenericStruct =
            paraTy->IsStruct() && CGType::GetOrCreate(cgMod, paraTy)->GetSize() && paraTy->IsGenericRelated();
        if ((!isTrivialEnum && !isSizeableGenericStruct) || pointerWrapper) {
            expr.emplace_back(llvm::dwarf::DW_OP_deref);
        }
        auto flag = llvm::DINode::FlagObjectPointer | llvm::DINode::FlagArtificial;
        auto localVariable = createParameterVariable(scope, name, 1, file, 0u, type, true, flag);
        insertDeclare(llvmValue, localVariable, createExpression(expr),
            llvm::DILocation::get(cgMod.GetLLVMContext(), 0u, 0u, scope), &curBB);
    } else {
        bool isRefEnum = paraTy->IsEnum() &&
            !StaticCast<CHIR::EnumType*>(paraTy)->GetEnumDef()->IsAllCtorsTrivial() &&
            !IsReferenceType(*paraTy, cgMod);
        // To add the deref flag, need to guarantee the refType is not nullptr, which corresponds to Option<Ref>.None.
        bool isTrivialEnum =
            paraTy->IsEnum() &&
            StaticCast<const CHIR::EnumType*>(paraTy)->GetEnumDef()->IsAllCtorsTrivial();
        bool isSizeableGenericStruct =
            paraTy->IsStruct() && CGType::GetOrCreate(cgMod, paraTy)->GetSize() && paraTy->IsGenericRelated();
        bool isPointerType = IsReferenceType(*paraTy, cgMod) || paraTy->IsStruct() || isRefEnum || paraTy->IsVArray() ||
            paraTy->IsTuple() || paraTy->IsGeneric();
        if (!isTrivialEnum && !isSizeableGenericStruct && isPointerType) {
            auto cgType = CGType::GetOrCreate(cgMod, paraTy);
            bool isRefOption =
                paraTy->IsEnum() && (cgType->IsOptionLikeRef() || StaticCast<CGEnumType*>(cgType)->IsOptionLikeT());
            if (isEnv) {
                type = CreatePointerType(type);
            } else if (!isRefOption) {
                expr.emplace_back(llvm::dwarf::DW_OP_deref);
            }
        } else if (pointerWrapper) {
            expr.emplace_back(llvm::dwarf::DW_OP_deref);
        }
        auto flag = llvm::DINode::FlagZero;
        name = isEnv ? "$CapturedVars" : name;
        llvm::DILocalVariable* localVariable =
            createParameterVariable(scope, name, paramNo, file, scope->getLine(), type, true, flag);
        insertDeclare(llvmValue, localVariable, createExpression(expr),
            llvm::DILocation::get(
                cgMod.GetLLVMContext(), position.GetBeginPos().line, position.GetBeginPos().column, scope),
            &curBB);
    }
    ++paramNo;
}

llvm::DILocation* DIBuilder::HandleDefaultParamLocation(const CHIRExprWrapper& chirNode, llvm::DIScope* scope)
{
    auto& position = chirNode.GetDebugLocation();
    bool isApply = chirNode.GetExprKind() == CHIR::ExprKind::APPLY ||
        chirNode.GetExprKind() == CHIR::ExprKind::APPLY_WITH_EXCEPTION;
    if (!position.GetBeginPos().IsZero() && isApply) {
        curLocation = Position(position.GetBeginPos().line, position.GetBeginPos().column);
    }
    return llvm::DILocation::get(cgMod.GetLLVMContext(), static_cast<unsigned>(curLocation.line),
        static_cast<unsigned>(curLocation.column), scope);
}

llvm::DILocation* DIBuilder::CreateDILoc(const CHIRExprWrapper& chirNode, const bool removable)
{
    CJC_ASSERT(enabled || enableLineInfo);
    llvm::BasicBlock* currentBB = cgMod.GetMappedBB(chirNode.GetParentBlock());
    auto& position = chirNode.GetDebugLocation();
    // Should not try to generate DebugLocation in the function without subprogram.
    CJC_NULLPTR_CHECK(currentBB);
    if (!currentBB->getParent()->getSubprogram()) {
        return nullptr;
    }
    if (removable && position.IsInvalidPos()) {
        return nullptr;
    }
    needSubprogram = true;
    llvm::DIScope* scope = GetOrCreateScope(position, *currentBB);
    auto parentFunc = chirNode.GetTopLevelFunc();
    if (parentFunc && parentFunc->GetFuncKind() == CHIR::DEFAULT_PARAMETER_FUNC) {
        return HandleDefaultParamLocation(chirNode, scope);
    }
    auto [line, column] = position.GetBeginPos();
    return llvm::DILocation::get(
        cgMod.GetLLVMContext(), line, column, scope, nullptr, !position.GetBeginPos().IsLegal());
}

llvm::DILocation* DIBuilder::CreateDILoc(llvm::DIScope* currentScope, const CHIR::Position& position)
{
    if (!enabled && !enableLineInfo) {
        return nullptr;
    }
    return llvm::DILocation::get(
        cgMod.GetLLVMContext(), position.line, position.column, currentScope, nullptr, !position.IsLegal());
}

llvm::DIScope* DIBuilder::GetOrCreateScope(const CHIR::DebugLocation& position, llvm::BasicBlock& currentBB)
{
    if (position.GetScopeInfo().size() > 1) {
        std::vector<int> scopeInfo = position.GetScopeInfo();
        curScope = GetOrCreateLexicalScope(position, currentBB, scopeInfo);
        return curScope;
    }
    if (position.GetScopeInfo().size() == 1) {
        curScope = currentBB.getParent()->getSubprogram();
    }
    return position.GetScopeInfo().empty() && curScope ? curScope : currentBB.getParent()->getSubprogram();
}

llvm::DIScope* DIBuilder::GetOrCreateLexicalScope(
    const CHIR::DebugLocation& position, llvm::BasicBlock& currentBB, std::vector<int>& scopeInfo)
{
    if (lexicalBlocks.find(scopeInfo) != lexicalBlocks.end()) {
        return lexicalBlocks[scopeInfo];
    }
    auto file = currentBB.getParent()->getSubprogram()->getFile();
    scopeInfo.pop_back();
    auto parentScope = scopeInfo.size() == 1 ? currentBB.getParent()->getSubprogram()
                                             : GetOrCreateLexicalScope(position, currentBB, scopeInfo);
    auto lb = createLexicalBlock(parentScope, file, position.GetBeginPos().line, position.GetBeginPos().column);
    lexicalBlocks[position.GetScopeInfo()] = lb;
    return lb;
}

llvm::DISubroutineType* DIBuilder::CreateFuncType(const CHIR::FuncType* func, bool isImported, bool hasThis)
{
    // SmallVector Size: 8.
    llvm::SmallVector<llvm::Metadata*, 8> eltTys;
    // Function return type.
    auto returnType = GetOrCreateType(*func->GetReturnType());
    auto paramsTypes = func->GetParamTypes();
    eltTys.push_back(returnType);
    bool skipFirstParm = false;
    if (isImported) {
        eltTys.push_back(CreateRefType()); // Context or this.
        skipFirstParm = true;
    }
    // Function paramsType.
    for (size_t index = 0; index < paramsTypes.size(); index++) {
        if (skipFirstParm) {
            skipFirstParm = false;
            continue;
        }
        llvm::DIType* cgType = GetOrCreateType(*paramsTypes[index]);
        if (hasThis && index == 0) {
            auto flag = llvm::DINode::FlagObjectPointer;
            flag |= llvm::DINode::FlagArtificial;
            cgType = createMemberPointerType(cgType, cgType, 64u, 0, flag);
        }
        eltTys.push_back(cgType);
    }
    llvm::DISubroutineType* subroutineType = createSubroutineType(getOrCreateTypeArray(eltTys));
    return subroutineType;
}

llvm::DIFile* DIBuilder::GetOrCreateFile(const CHIR::DebugLocation& position)
{
    std::string dirPath = position.GetAbsPath();
    auto pos = dirPath.find_last_of(DIR_SEPARATOR);
    std::string fileName = "";
    if (pos != std::string::npos) {
        fileName = dirPath.substr(pos + 1);
        dirPath = dirPath.substr(0, pos);
    }
    // Do option '--trimpath'.
    // if '--coverage' is enabled, '--trimpath' would be disabled.
    if (!cgMod.GetCGContext().GetCompileOptions().removedPathPrefix.empty()) {
        dirPath = RemovePathPrefix(dirPath, cgMod.GetCGContext().GetCompileOptions().removedPathPrefix);
    }

    auto diFile = createFile(fileName, dirPath);
    return diFile;
}

llvm::DIType* DIBuilder::GetOrCreateType(const CHIR::Type& ty, bool isReadOnly)
{
    const CHIR::Type* baseTy = DeRef(const_cast<CHIR::Type&>(ty));
    auto ref = typeCache.find(baseTy);
    if (ref != typeCache.end()) {
        auto diType = llvm::cast<llvm::DIType>(ref->second);
        return isReadOnly ? createQualifiedType(llvm::dwarf::DW_TAG_const_type, diType) : diType;
    }

    auto diType = CreateDIType(*baseTy);
    CJC_ASSERT(diType);
    typeCache[baseTy] = llvm::TrackingMDRef(diType);
    return isReadOnly ? createQualifiedType(llvm::dwarf::DW_TAG_const_type, diType) : diType;
}

uint64_t DIBuilder::GetBasicTypeSize(const CHIR::Type& ty) const
{
    llvm::Type* type = nullptr;
    switch (ty.GetTypeKind()) {
        case CHIR::Type::TypeKind::TYPE_INT8:
        case CHIR::Type::TypeKind::TYPE_UINT8:
            type = llvm::Type::getInt8Ty(cgMod.GetLLVMContext());
            break;
        case CHIR::Type::TypeKind::TYPE_INT16:
        case CHIR::Type::TypeKind::TYPE_UINT16:
            type = llvm::Type::getInt16Ty(cgMod.GetLLVMContext());
            break;
        case CHIR::Type::TypeKind::TYPE_RUNE:
        case CHIR::Type::TypeKind::TYPE_INT32:
        case CHIR::Type::TypeKind::TYPE_UINT32:
            type = llvm::Type::getInt32Ty(cgMod.GetLLVMContext());
            break;
        case CHIR::Type::TypeKind::TYPE_INT64:
        case CHIR::Type::TypeKind::TYPE_UINT64:
            type = llvm::Type::getInt64Ty(cgMod.GetLLVMContext());
            break;
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE:
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE: {
            const int int64Width = 64;
            type = StaticCast<const CHIR::NumericType&>(ty).GetBitness() == int64Width
                ? llvm::Type::getInt64Ty(cgMod.GetLLVMContext())
                : llvm::Type::getInt32Ty(cgMod.GetLLVMContext());
            break;
        }
        case CHIR::Type::TypeKind::TYPE_FLOAT16:
            type = llvm::Type::getHalfTy(cgMod.GetLLVMContext());
            break;
        case CHIR::Type::TypeKind::TYPE_FLOAT32:
            type = llvm::Type::getFloatTy(cgMod.GetLLVMContext());
            break;
        case CHIR::Type::TypeKind::TYPE_FLOAT64:
            type = llvm::Type::getDoubleTy(cgMod.GetLLVMContext());
            break;
        case CHIR::Type::TypeKind::TYPE_BOOLEAN:
            type = llvm::Type::getInt1Ty(cgMod.GetLLVMContext());
            break;
        default:
            return 0;
    }
    return GetTypeSize(type);
}

uint64_t DIBuilder::GetTypeSize(llvm::Type* llvmType) const
{
    CJC_ASSERT(llvmType->isSized());
    uint64_t size = 8u * cgMod.GetLLVMModule()->getDataLayout().getTypeAllocSize(llvmType); // 1 Byte = 8 bit.
    return size;
}

// If DIType obtained from 'GetOrCreateType' method, using this API to avoid getting an invalid result.
uint64_t DIBuilder::GetSizeInBits(llvm::DIType* type) const
{
    if (type->getTag() == llvm::dwarf::DW_TAG_typedef) {
        CJC_ASSERT(llvm::isa<llvm::DIDerivedType>(type));
        type = llvm::cast<llvm::DIDerivedType>(type)->getBaseType();
    }
    return type->getSizeInBits();
}

llvm::DIType* DIBuilder::CreateDIType(const CHIR::Type& ty)
{
    if (!enabled) {
        return nullptr;
    }
    llvm::dwarf::TypeKind encoding = llvm::dwarf::DW_ATE_unsigned;
    auto boxTy = ty.IsBox() ? &ty : nullptr;
    auto baseTy = ty.IsBox() ? StaticCast<const CHIR::BoxType&>(ty).GetBaseType() : &ty;
    switch (baseTy->GetTypeKind()) {
        case CHIR::Type::TypeKind::TYPE_UNIT:
        case CHIR::Type::TypeKind::TYPE_NOTHING:
        case CHIR::Type::TypeKind::TYPE_VOID:
            return CreateUnitType();
        case CHIR::Type::TypeKind::TYPE_RUNE:
            encoding = llvm::dwarf::DW_ATE_UTF;
            break;
        case CHIR::Type::TypeKind::TYPE_UINT8:
        case CHIR::Type::TypeKind::TYPE_UINT16:
        case CHIR::Type::TypeKind::TYPE_UINT32:
        case CHIR::Type::TypeKind::TYPE_UINT64:
        case CHIR::Type::TypeKind::TYPE_UINT_NATIVE: {
            encoding = llvm::dwarf::DW_ATE_unsigned;
            break;
        }
        case CHIR::Type::TypeKind::TYPE_INT8:
        case CHIR::Type::TypeKind::TYPE_INT16:
        case CHIR::Type::TypeKind::TYPE_INT32:
        case CHIR::Type::TypeKind::TYPE_INT64:
        case CHIR::Type::TypeKind::TYPE_INT_NATIVE: {
            encoding = llvm::dwarf::DW_ATE_signed;
            break;
        }
        case CHIR::Type::TypeKind::TYPE_FLOAT16:
        case CHIR::Type::TypeKind::TYPE_FLOAT32:
        case CHIR::Type::TypeKind::TYPE_FLOAT64: {
            encoding = llvm::dwarf::DW_ATE_float;
            break;
        }
        case CHIR::Type::TypeKind::TYPE_BOOLEAN: {
            encoding = llvm::dwarf::DW_ATE_boolean;
            break;
        }
        case CHIR::Type::TypeKind::TYPE_FUNC:
            return CreateVarFuncType(StaticCast<const CHIR::FuncType&>(ty));
        case CHIR::Type::TypeKind::TYPE_TUPLE:
            return CreateTupleType(StaticCast<const CHIR::TupleType&>(*baseTy), boxTy);
        case CHIR::Type::TypeKind::TYPE_RAWARRAY:
            return CreateArrayType(StaticCast<const CHIR::RawArrayType&>(ty));
        case CHIR::Type::TypeKind::TYPE_VARRAY:
            return CreateVArrayType(StaticCast<const CHIR::VArrayType&>(ty));
        case CHIR::Type::TypeKind::TYPE_CPOINTER:
            return CreateCPointerType(StaticCast<const CHIR::CPointerType&>(ty));
        case CHIR::Type::TypeKind::TYPE_CSTRING:
            return CreateCStringType(StaticCast<const CHIR::CStringType&>(ty));
        case CHIR::Type::TypeKind::TYPE_CLASS:
            return CreateClassType(StaticCast<const CHIR::ClassType&>(ty));
        case CHIR::Type::TypeKind::TYPE_STRUCT:
            return CreateStructType(StaticCast<const CHIR::StructType&>(*baseTy), boxTy);
        case CHIR::Type::TypeKind::TYPE_ENUM:
            return CreateEnumType(StaticCast<const CHIR::EnumType&>(*baseTy), boxTy);
        case CHIR::Type::TypeKind::TYPE_GENERIC:
            return CreateGenericType(StaticCast<const CHIR::GenericType&>(ty));
        case CHIR::Type::TypeKind::TYPE_INVALID:
        default:
            return createNullPtrType();
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::string tyName = baseTy->ToString();
#endif
    uint64_t size = GetBasicTypeSize(*baseTy);
    auto diType = createBasicType(tyName, size, static_cast<unsigned>(encoding));
    auto typeDefType = createTypedef(diType, tyName, diCompileUnit->getFile(), 0, diCompileUnit);
    if (boxTy) {
        auto fwdDecl = createStructType(diNamespace, tyName, defaultFile, 0u, size + 64u, 0u, llvm::DINode::FlagZero,
            nullptr, getOrCreateArray({}), 0u, nullptr, ty.ToString());
        auto memberType =
            createMemberType(fwdDecl, "base", defaultFile, 0u, size, 0u, 64u, llvm::DINode::FlagZero, typeDefType);
        replaceArrays(fwdDecl, getOrCreateArray({memberType}));
        return fwdDecl;
    }
    return typeDefType;
}

llvm::DIType* DIBuilder::CreateUnitType()
{
    auto layOut = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(
        llvm::cast<llvm::StructType>(CGType::GetUnitCGType(cgMod)->GetLLVMType()));
    auto align = layOut->getAlignment().value();
    auto unitType = createStructType(diCompileUnit, "Unit", defaultFile, 0u, 8, static_cast<uint32_t>(align),
        llvm::DINode::FlagZero, nullptr, getOrCreateArray({}), 0, nullptr, UNIT_TYPE_STR);
    return unitType;
}

llvm::DIType* DIBuilder::CreatePointerType(llvm::DIType* diType, uint64_t sizeInBits)
{
    return createPointerType(diType, sizeInBits);
}

/**
 * Pure Function type is a pointer of function address.
 */
llvm::DIType* DIBuilder::CreateVarFuncType(const CHIR::FuncType& funcTy)
{
    auto size = CreateRefType()->getSizeInBits();
    auto name = GenerateFuncName(funcTy);
    auto tag = static_cast<unsigned>(llvm::dwarf::DW_TAG_class_type);
    llvm::DICompositeType* fwdDecl = createReplaceableCompositeType(
        tag, name, diNamespace, defaultFile, 0u, 0, 64u, 0u, llvm::DINode::FlagTypePassByReference, funcTy.ToString());
    fwdDecl = llvm::MDNode::replaceWithDistinct(llvm::TempDICompositeType(fwdDecl));
    llvm::DIType* funcDIType = CreateFuncType(&funcTy);
    auto funcPtrType = createMemberType(fwdDecl, "ptr", defaultFile, 0u, CreateRefType()->getSizeInBits(), 0u, 0u,
        llvm::DINode::FlagZero, CreatePointerType(funcDIType, size));
    replaceArrays(fwdDecl, getOrCreateArray({funcPtrType}));
    return fwdDecl;
}

llvm::DIType* DIBuilder::CreateTupleType(const CHIR::TupleType& tupleTy, const CHIR::Type* boxTy)
{
    auto tupleTypeName = GenerateTypeName(tupleTy);
    auto scope = diCompileUnit;
    auto diFile = scope->getFile();
    auto llvmType = CGType::GetOrCreate(cgMod, &tupleTy)->GetLLVMType();
    auto identifier = tupleTy.ToString();
    CodeGenDIVector16 elements; // SmallVector Size: 16.
    if (!CGType::GetOrCreate(cgMod, &tupleTy)->GetSize()) {
        auto fwdDecl = createStructType(
            scope, tupleTypeName, diFile, 0u, 64u, 0u, llvm::DINode::FlagZero, nullptr, {}, 0u, nullptr, identifier);
        auto memberType = CreateTypeInfoMember(fwdDecl, diFile);
        elements.push_back(memberType);
        replaceArrays(fwdDecl, getOrCreateArray(elements));
        return fwdDecl;
    }
    // Tuple type is a temporary type which do no have fixed definition file & line.
    uint64_t boxOffset = boxTy ? 64u : 0u;
    auto totalSize = GetTypeSize(llvmType) + boxOffset;
    llvm::DICompositeType* fwdDecl =
        createStructType(scope, tupleTypeName, diFile, 0u, totalSize, 0u, llvm::DINode::FlagZero, nullptr,
            getOrCreateArray(elements), 0, nullptr, "$" + std::to_string(boxOffset) + "_" + identifier);
    if (boxTy) {
        typeCache[boxTy] = llvm::TrackingMDRef(fwdDecl);
    }
    unsigned i = 0;
    auto layOut = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(llvm::cast<llvm::StructType>(llvmType));
    auto align = layOut->getAlignment().value();

    for (auto& ty : tupleTy.GetElementTypes()) {
        auto elemType = GetOrCreateType(*ty);
        auto bitInSize = GetTypeSize(llvmType->getStructElementType(i));
        auto offset = layOut->getElementOffsetInBits(i) + boxOffset;
        if (IsReferenceType(*ty, cgMod)) {
            elemType = CreatePointerType(elemType, CreateRefType()->getSizeInBits());
        }
        auto memberType = createMemberType(fwdDecl, "_" + std::to_string(i), diFile, 0u, bitInSize,
            static_cast<uint32_t>(align), offset, llvm::DINode::FlagZero, elemType);
        elements.push_back(memberType);
        i++;
    }
    replaceArrays(fwdDecl, getOrCreateArray(elements));
    return fwdDecl;
}

llvm::DIType* DIBuilder::CreateArrayType(const CHIR::RawArrayType& arrTy)
{
    auto identifier = arrTy.ToString();
    uint32_t dims = arrTy.GetDims();
    CJC_ASSERT(dims != 0);
    auto arrayTypeName = GenerateTypeName(arrTy);
    auto diFile = defaultFile;
    auto scope = diCompileUnit;

    CodeGenDIVector3 elements; // Array.Type has 3 elements.
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto elemTy = arrTy.GetElementType();
    auto elemType = GetOrCreateType(*elemTy);
    auto arrayLayout = dynamic_cast<CGArrayType*>(CGType::GetOrCreate(cgMod, &arrTy))->GetLayoutType();
    // when arrayLayout is nullptr, means this rawArray is not sized.
    auto arrayLayoutSize = arrayLayout ? GetTypeSize(arrayLayout) : 64u;
    // %ArrayBase is not include object now, so we should add 64bit for the object.
    uint64_t totalSize = 64u + arrayLayoutSize + GetSizeInBits(elemType);
    auto flag = llvm::DINode::FlagZero;
#endif
    // Array type is a temporary type which do no have fixed definition file nor line.
    // type struct: {klass*, i32 arrayLength, [0 x T]}, where 'klass*' is invisible for user.
    llvm::DICompositeType* fwdDecl = createStructType(
        scope, arrayTypeName, diFile, 0u, totalSize, 0u, flag, nullptr, getOrCreateArray({}), 0, nullptr, identifier);
    if (!arrayLayout) {
        auto memberType = CreateTypeInfoMember(fwdDecl, diFile);
        elements.push_back(memberType);
        replaceArrays(fwdDecl, getOrCreateArray(elements));
        return fwdDecl;
    }
    CreateArrayMemberType(arrTy, fwdDecl, elements, arrayLayout);
    return fwdDecl;
}

llvm::DIType* DIBuilder::CreateVArrayType(const CHIR::VArrayType& vArrTy)
{
    auto size = vArrTy.GetSize();
    auto elementType = GetOrCreateType(*vArrTy.GetElementType());
    auto name = GenerateTypeName(vArrTy);
    auto llvmType = CGType::GetOrCreate(cgMod, &vArrTy)->GetLLVMType();
    auto typeSize = GetTypeSize(llvmType);
    // Align in 64-bit.
    auto range = getOrCreateSubrange(0, size);
    auto diType = createArrayType(typeSize, 0, elementType, getOrCreateArray(range));
    return createTypedef(diType, name, diCompileUnit->getFile(), 0, diCompileUnit);
}

llvm::DIType* DIBuilder::CreateCPointerType(const CHIR::CPointerType& ptrTy)
{
    auto name = GenerateTypeName(ptrTy);
    auto diFile = defaultFile;
    llvm::DICompositeType* fwdDecl;
    auto uniqueName = ptrTy.ToString();
    auto sizeInBits = CreateRefType()->getSizeInBits();
    llvm::DIType* basicType = createBasicType("Int8", sizeInBits, static_cast<unsigned>(llvm::dwarf::DW_ATE_unsigned));
    if (!ptrTy.GetTypeArgs().empty() && ptrTy.GetTypeArgs()[0] && !ptrTy.GetTypeArgs()[0]->IsCPointer()) {
        basicType = GetOrCreateType(*ptrTy.GetTypeArgs()[0]);
    }
    fwdDecl = createStructType(
        diCompileUnit, name, diFile, 0, sizeInBits, 0u, llvm::DINode::FlagZero, nullptr, {}, 0u, nullptr, uniqueName);
    auto elemType = CreatePointerType(basicType, sizeInBits);
    auto eleType = createMemberType(
        fwdDecl, "ptr", diFile, 0, sizeInBits, 8, 0, llvm::DINode::FlagZero, elemType); // 8 is default align size.
    replaceArrays(fwdDecl, getOrCreateArray({eleType}));
    return fwdDecl;
}

llvm::DIType* DIBuilder::CreateCStringType(const CHIR::CStringType& cStringType)
{
    auto name = GenerateTypeName(cStringType);
    auto uniqueName = cStringType.ToString();
    auto diFile = defaultFile;
    auto refSizeInBits = CreateRefType()->getSizeInBits();
    // Generates the CString struct.
    auto fwdDecl = createStructType(diCompileUnit, name, diFile, 0, refSizeInBits, 0u, llvm::DINode::FlagZero, nullptr,
        {}, 0u, nullptr, uniqueName);
    // Generates a member 'chars' of the 'Int8 *' type in CString struct.
    auto elemSizeInBits = GetTypeSize(llvm::Type::getInt8Ty(cgMod.GetLLVMContext()));
    llvm::DIType* basicType =
        createBasicType("char", elemSizeInBits, static_cast<unsigned>(llvm::dwarf::DW_ATE_unsigned_char));
    auto elemType = CreatePointerType(basicType, elemSizeInBits);
    auto eleType = createMemberType(fwdDecl, "chars", diFile, 0, elemSizeInBits, 8, 0, llvm::DINode::FlagZero,
        elemType); // 8 is default align size.
    replaceArrays(fwdDecl, getOrCreateArray({eleType}));
    return fwdDecl;
}

llvm::DIType* DIBuilder::CreateTypeInfoMember(llvm::DICompositeType* fwdDecl, llvm::DIFile* diFile)
{
    return createMemberType(
        fwdDecl, "$ti", diFile, 0u, 64u, static_cast<uint32_t>(0u), 0u, llvm::DINode::FlagZero, CreateRefType());
}

void DIBuilder::CreateInheritedInterface(
    CodeGenDIVector16& elements, llvm::DICompositeType* fwdDecl, const CHIR::CustomType& customTy)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    for (auto interfaceTy : const_cast<CHIR::CustomType&>(customTy).GetImplementedInterfaceTysWithoutExtend(
             cgMod.GetCGContext().GetCHIRBuilder())) {
        auto interfaceDiType = GetOrCreateType(*interfaceTy);
        auto interfaceType = createInheritance(fwdDecl, interfaceDiType, 0u, 0u, llvm::DINode::FlagZero);
        elements.push_back(interfaceType);
    }
    for (auto extendDef : customTy.GetCustomTypeDef()->GetExtends()) {
        for (auto interfaceTy : extendDef->GetImplementedInterfaceTys()) {
            auto interfaceDiType = GetOrCreateType(*interfaceTy);
            auto interfaceType = createInheritance(fwdDecl, interfaceDiType, 0u, 0u, llvm::DINode::FlagZero);
            elements.push_back(interfaceType);
        }
    }
#endif
}

void DIBuilder::CreateMethodType(
    const CHIR::CustomTypeDef& customDef, CodeGenDIVector16& elements, llvm::DICompositeType* fwdDecl)
{
    auto allMethods = customDef.GetType()->GetDeclareAndExtendMethods(cgMod.GetCGContext().GetCHIRBuilder());
    if (allMethods.empty()) {
        return;
    }
    auto diFile = GetOrCreateFile(customDef.GetDebugLocation());
    for (auto method : allMethods) {
        if (method->TestAttr(CHIR::Attribute::NO_DEBUG_INFO)) {
            continue;
        }
        bool hasThis = !method->TestAttr(CHIR::Attribute::STATIC);
        // the name of parameter init function is useless for debug, and may cause duplicate identifier error
        auto name = IsParaInitFunc(*method)
            ? "" 
            : GenerateGenericFuncName(method->GetSrcCodeIdentifier(), method->GetOriginalGenericTypeParams());
        auto subprogramTy = CreateFuncType(StaticCast<CHIR::FuncType*>(method->GetType()), false, hasThis);
        auto& position = method->GetDebugLocation();
        llvm::DINode::DIFlags flags = llvm::DINode::FlagPrototyped;
        flags |= method->TestAttr(CHIR::Attribute::PUBLIC) ? llvm::DINode::FlagPublic : llvm::DINode::FlagZero;
        flags |= method->TestAttr(CHIR::Attribute::STATIC) ? llvm::DINode::FlagStaticMember : llvm::DINode::FlagZero;
        flags |= method->TestAttr(CHIR::Attribute::PROTECTED) ? llvm::DINode::FlagProtected : llvm::DINode::FlagZero;
        llvm::DISubprogram::DISPFlags spFlags = method->TestAttr(CHIR::Attribute::ABSTRACT)
            ? llvm::DISubprogram::SPFlagPureVirtual
            : llvm::DISubprogram::SPFlagZero;
        llvm::DISubprogram* sp = createMethod(fwdDecl, name, method->GetIdentifierWithoutPrefix(), diFile,
            position.GetBeginPos().line, subprogramTy, 0, 0, nullptr, flags, spFlags);
        elements.push_back(sp);
    }
}

llvm::DIType* DIBuilder::CreateClassType(const CHIR::ClassType& classTy)
{
    auto classDef = classTy.GetClassDef();
    auto identifier = classTy.ToString();
    if (!CGType::GetOrCreate(cgMod, &classTy)->GetSize()) {
        return CreateGenericClassType(classTy);
    }

    if (IsCoreObject(*classDef)) {
        return GetOrCreateObjectClassType();
    }
    if (classDef->IsInterface()) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        return CreateInterfaceType(classTy);
#endif
    }
    if (classTy.IsAutoEnvBase()) {
        return CreateClosureType(classTy);
    } else if (IsClosureConversionEnvClass(*classDef)) {
        return CreateCapturedVars(classTy);
    }
    auto classLayout = StaticCast<CGClassType*>(CGType::GetOrCreate(cgMod, &classTy))->GetLayoutType();
    CJC_ASSERT(!classLayout->isOpaque() && "The body is not set for classLayout.");
    auto& position = classDef->GetDebugLocation();
    std::string name;
    if (IsCapturedClass(*classDef)) {
        name = "$Captured_";
        auto capturedTy = DeRef(*classDef->GetDirectInstanceVar(0).type);
        if (capturedTy->IsCustomType()) {
            name += GenerateCustomTypeName(StaticCast<const CHIR::CustomType>(*capturedTy), false);
        } else {
            name += GenerateTypeName(*capturedTy);
        }
    } else {
        name = GenerateCustomTypeName(classTy, false);
    }
    auto diFile = GetOrCreateFile(position);
    auto classSize = GetClassSize(classLayout) + 64u;
    auto superDiType = GetOrCreateType(
        *const_cast<CHIR::ClassType&>(classTy).GetSuperClassTy(&cgMod.GetCGContext().GetCHIRBuilder()));
    auto defPackage = createNameSpace(diCompileUnit, classDef->GetPackageName(), false);
    auto tag = static_cast<unsigned>(llvm::dwarf::DW_TAG_class_type);
    llvm::DICompositeType* fwdDecl = createReplaceableCompositeType(tag, name, defPackage, diFile,
        position.GetBeginPos().line, 0, classSize, 0u, llvm::DINode::FlagTypePassByReference, identifier);
    fwdDecl = llvm::MDNode::replaceWithDistinct(llvm::TempDICompositeType(fwdDecl));
    typeCache[&classTy] = llvm::TrackingMDRef(fwdDecl);
    CodeGenDIVector16 elements;
    auto inheritType = createInheritance(fwdDecl, superDiType, 0u, 0u, llvm::DINode::FlagZero);
    elements.push_back(inheritType);
    CreateInheritedInterface(elements, fwdDecl, classTy);
    CreateClassMemberType(classTy, classLayout, elements, fwdDecl);
    CreateMethodType(*classDef, elements, fwdDecl);
    replaceArrays(fwdDecl, getOrCreateArray(elements));
    return fwdDecl;
}

llvm::DIType* DIBuilder::CreateGenericClassType(const CHIR::ClassType& classTy)
{
    auto classDef = classTy.GetClassDef();
    auto identifier = classTy.ToString();
    auto& position = classDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto defPackage = createNameSpace(diCompileUnit, classDef->GetPackageName(), false);
    auto name = GenerateCustomTypeName(classTy, false);
    auto tag = static_cast<unsigned>(llvm::dwarf::DW_TAG_class_type);
    auto size = 64 + classDef->GetDirectInstanceVars().size() * 64;
    llvm::DICompositeType* fwdDecl = createReplaceableCompositeType(tag, name, defPackage, diFile,
        position.GetBeginPos().line, 0, size, 0u, llvm::DINode::FlagTypePassByReference, identifier);
    fwdDecl = llvm::MDNode::replaceWithDistinct(llvm::TempDICompositeType(fwdDecl));
    typeCache[&classTy] = llvm::TrackingMDRef(fwdDecl);
    CodeGenDIVector16 elements;
    auto superDiType = GetOrCreateType(
        *const_cast<CHIR::ClassType&>(classTy).GetSuperClassTy(&cgMod.GetCGContext().GetCHIRBuilder()));
    auto inheritType = createInheritance(fwdDecl, superDiType, 0u, 0u, llvm::DINode::FlagZero);
    elements.push_back(inheritType);
    CreateInheritedInterface(elements, fwdDecl, classTy);
    CreateCustomTypeMember(classDef->GetDirectInstanceVars(), elements, fwdDecl, diFile);
    CreateMethodType(*classDef, elements, fwdDecl);
    CreateGetGenericFunc(classTy, fwdDecl, elements);
    replaceArrays(fwdDecl, getOrCreateArray(elements));
    return fwdDecl;
}

llvm::DIType* DIBuilder::CreateCapturedVars(const CHIR::ClassType& classTy)
{
    auto classLayout = StaticCast<CGClassType*>(CGType::GetOrCreate(cgMod, &classTy))->GetLayoutType();
    auto classSize = GetClassSize(classLayout) + 64u;
    auto tag = static_cast<unsigned>(llvm::dwarf::DW_TAG_class_type);
    llvm::DICompositeType* fwdDecl = createReplaceableCompositeType(tag, "$CapturedVars", diNamespace, defaultFile, 0,
        0, classSize, 0u, llvm::DINode::FlagTypePassByReference, classTy.ToString());
    fwdDecl = llvm::MDNode::replaceWithDistinct(llvm::TempDICompositeType(fwdDecl));
    CodeGenDIVector16 elements;
    auto superDiType = GetOrCreateObjectClassType();
    auto inheritType = createInheritance(fwdDecl, superDiType, 0u, 0u, llvm::DINode::FlagZero);
    elements.push_back(inheritType);
    typeCache[&classTy] = llvm::TrackingMDRef(fwdDecl);
    CreateClassMemberType(classTy, classLayout, elements, fwdDecl);
    replaceArrays(fwdDecl, getOrCreateArray(elements));
    return fwdDecl;
}

void DIBuilder::CreateCustomTypeMember(std::vector<CHIR::MemberVarInfo> members, CodeGenDIVector16& elements,
    llvm::DICompositeType* fwdDecl, llvm::DIFile* diFile)
{
    auto memberType = CreateTypeInfoMember(fwdDecl, diFile);
    elements.push_back(memberType);
    uint64_t offset = 0;
    for (auto& it : members) {
        auto elementTy = it.type;
        auto elemType = GetOrCreateType(*elementTy);
        auto bitInSize = GetSizeInBits(elemType);
        offset += bitInSize;
        auto align = 0u;
        memberType = createMemberType(fwdDecl, it.name, diFile, it.loc.GetBeginPos().line, bitInSize,
            static_cast<uint32_t>(align), offset, llvm::DINode::FlagZero, elemType);
        elements.push_back(memberType);
    }
}

void DIBuilder::CreateGetGenericFunc(
    const CHIR::CustomType& customTy, llvm::DICompositeType* fwdDecl, CodeGenDIVector16& elements)
{
    if (!customTy.IsGenericRelated()) {
        return;
    }
    auto defType = GetOrCreateType(*customTy.GetCustomTypeDef()->GetType());
    // SmallVector Size: 8.
    llvm::SmallVector<llvm::Metadata*, 8> eltTys;
    eltTys.push_back(defType);
    llvm::DISubroutineType* subroutineType = createSubroutineType(getOrCreateTypeArray(eltTys));
    auto funcElement = createFunction(fwdDecl, "$GetGenericDef", "$GetGenericDef", defaultFile, 0u, subroutineType, 0u,
        llvm::DINode::FlagPrototyped, llvm::DISubprogram::SPFlagZero);
    elements.emplace_back(funcElement);
    finalizeSubprogram(funcElement);
}

llvm::DIType* DIBuilder::CreateStructType(const CHIR::StructType& structTy, const CHIR::Type* boxTy)
{
    auto structDef = structTy.GetStructDef();
    auto identifier = structTy.ToString();
    if (!CGType::GetOrCreate(cgMod, &structTy)->GetSize()) {
        auto& position = structDef->GetDebugLocation();
        auto diFile = GetOrCreateFile(position);
        auto defPackage = createNameSpace(diCompileUnit, structDef->GetPackageName(), false);
        auto name = GenerateCustomTypeName(structTy, false);
        auto size = 64 + structDef->GetAllInstanceVars().size() * 64;
        auto fwdDecl = createStructType(defPackage, name, diFile, position.GetBeginPos().line, size, 0u,
            llvm::DINode::FlagZero, nullptr, {}, 0u, nullptr, identifier);
        typeCache[&structTy] = llvm::TrackingMDRef(fwdDecl);
        CodeGenDIVector16 elements;
        CreateInheritedInterface(elements, fwdDecl, structTy);
        CreateCustomTypeMember(structDef->GetAllInstanceVars(), elements, fwdDecl, diFile);
        CreateMethodType(*structDef, elements, fwdDecl);
        CreateGetGenericFunc(structTy, fwdDecl, elements);
        replaceArrays(fwdDecl, getOrCreateArray(elements));
        return fwdDecl;
    }
    auto name = GenerateCustomTypeName(structTy, false);
    auto& position = structDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto cgType = CGType::GetOrCreate(cgMod, &structTy)->GetLLVMType();
    uint64_t boxOffset = boxTy ? 64u : 0u;
    auto defPackage = createNameSpace(diCompileUnit, structDef->GetPackageName(), false);
    auto fwdDecl =
        createStructType(defPackage, name, diFile, position.GetBeginPos().line, GetTypeSize(cgType) + boxOffset, 0u,
            llvm::DINode::FlagZero, nullptr, {}, 0u, nullptr, "$" + std::to_string(boxOffset) + "_" + identifier);
    if (boxTy) {
        typeCache[boxTy] = llvm::TrackingMDRef(fwdDecl);
    } else {
        typeCache[&structTy] = llvm::TrackingMDRef(fwdDecl);
    }
    CodeGenDIVector16 elements;
    CreateInheritedInterface(elements, fwdDecl, structTy);
    CreateStructMemberType(structTy, cgType, elements, fwdDecl, boxTy);
    CreateMethodType(*structDef, elements, fwdDecl);
    replaceArrays(fwdDecl, getOrCreateArray(elements));
    return fwdDecl;
}

void DIBuilder::CreateStructMemberType(const CHIR::StructType& structTy, llvm::Type* cgType,
    CodeGenDIVector16& elements, llvm::DICompositeType* fwdDecl, const CHIR::Type* boxTy)
{
    auto structDef = structTy.GetStructDef();
    auto& position = structDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto layOut = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(llvm::cast<llvm::StructType>(cgType));
    unsigned i = 0;
    CHIR::CHIRBuilder& chirBuilder = cgMod.GetCGContext().GetCHIRBuilder();
    auto& nonConstStructTy = const_cast<CHIR::StructType&>(structTy);
    uint64_t boxOffset = boxTy ? 64u : 0u;
    CJC_ASSERT(nonConstStructTy.GetInstantiatedMemberTys(chirBuilder).size() == structDef->GetAllInstanceVars().size());
    for (auto& it : structDef->GetAllInstanceVars()) {
        auto elementTy = nonConstStructTy.GetInstantiatedMemberTys(chirBuilder)[i];
        auto elemTy = DeRef(*elementTy);
        auto elemType = GetOrCreateType(*elemTy, it.TestAttr(CHIR::Attribute::READONLY));
        if (IsReferenceType(*elemTy, cgMod) && !IsOption(*elementTy)) {
            elemType = CreatePointerType(elemType);
        }
        auto cgElemType = cgType->getStructElementType(i);
        auto bitInSize = GetTypeSize(cgElemType);
        CJC_NULLPTR_CHECK(layOut);
        auto offset = layOut->getElementOffsetInBits(i) + boxOffset;
        auto align = layOut->getAlignment().value();
        size_t line = 1; // the position info should obtain from MemberVarInfo.
        auto memberType = createMemberType(fwdDecl, it.name, diFile, line, bitInSize, static_cast<uint32_t>(align),
            offset, llvm::DINode::FlagZero, elemType);
        elements.push_back(memberType);
        i++;
    }
}

llvm::DIType* DIBuilder::CreateEnumType(const CHIR::EnumType& enumTy, const CHIR::Type* boxTy)
{
    auto enumDef = enumTy.GetEnumDef();
    // Set small vector as 4 elements.
    CodeGenDIVector16 enumMembers;

    llvm::DICompositeType* fwdDecl = nullptr;
    auto identifier = enumTy.ToString();
    auto cgType = StaticCast<const CGEnumType*>(CGType::GetOrCreate(cgMod, &enumTy));
    if (!CGType::GetOrCreate(cgMod, &enumTy)->GetSize()) {
        auto& position = enumDef->GetDebugLocation();
        auto diFile = GetOrCreateFile(position);
        auto defPackage = createNameSpace(diCompileUnit, enumDef->GetPackageName(), false);
        CJC_ASSERT(cgType->IsOptionLike() || cgType->IsAllAssociatedValuesAreNonRef());
        // There are just two cases where we cannot calculate enum size: OptionLike and EnumWithNonRef.
        // Ugly implementation, needs to be refactored
        auto prefix = "E3$";
        if (cgType->IsOptionLike()) {
            prefix = enumTy.IsOption() ? "" : "E2$";
        }
        auto name = prefix + GenerateCustomTypeName(enumTy,false);
        auto size = 64 + enumDef->GetAllInstanceVars().size() * 64;
        size = cgType->IsOptionLike() ? size + 32 : size; // The size of Int32 is 32-bit.
        fwdDecl = createStructType(defPackage, name, diFile, position.GetBeginPos().line, size, 0u,
            llvm::DINode::FlagZero, nullptr, {}, 0u, nullptr, identifier);
        typeCache[&enumTy] = llvm::TrackingMDRef(fwdDecl);
        CodeGenDIVector16 elements;
        CreateInheritedInterface(elements, fwdDecl, enumTy);
        if (cgType->IsOptionLike()) {
            auto constructorType = GetOrCreateEnumCtorType(enumTy);
            auto ctorSize = cgType->IsOptionLike() ? 8u : 32u;
            auto ctorType = createMemberType(
                fwdDecl, "constructor", diFile, 0u, ctorSize, 0u, 64u, llvm::DINode::FlagZero, constructorType);
            elements.push_back(ctorType);
        }
        CreateMethodType(*enumDef, elements, fwdDecl);
        CreateGetGenericFunc(enumTy, fwdDecl, elements);
        replaceArrays(fwdDecl, getOrCreateArray(elements));
        return fwdDecl;
    }
    bool isOption =
        enumTy.IsOption() && !const_cast<CHIR::EnumType*>(&enumTy)->IsBoxed(cgMod.GetCGContext().GetCHIRBuilder());
    if (enumDef->IsAllCtorsTrivial()) {
        fwdDecl = CreateTrivial(enumTy, enumMembers);
    } else if (isOption || cgType->IsOptionLike()) {
        fwdDecl = CreateEnumOptionType(enumTy, enumMembers, boxTy);
    } else if (cgType->IsAllAssociatedValuesAreNonRef()) {
        fwdDecl = CreateEnumWithNonRefArgsType(enumTy, enumMembers);
    } else {
        fwdDecl = CreateEnumWithArgsType(enumTy, enumMembers, boxTy);
    }
    typeCache[&enumTy] = llvm::TrackingMDRef(fwdDecl);
    CreateInheritedInterface(enumMembers, fwdDecl, enumTy);
    CreateMethodType(*enumDef, enumMembers, fwdDecl);
    replaceArrays(fwdDecl, getOrCreateArray(enumMembers));
    return fwdDecl;
}

llvm::DICompositeType* DIBuilder::GetOrCreateEnumCtorType(const CHIR::EnumType& enumTy)
{
    auto ref = enumCtorCache.find(&enumTy);
    if (ref != enumCtorCache.end()) {
        return llvm::cast<llvm::DICompositeType>(ref->second);
    }
    auto enumDef = enumTy.GetEnumDef();
    auto name = GenerateCustomTypeName(enumTy, false);
    auto& position = enumDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    CodeGenDIVector16 ctors;
    uint32_t fieldIdx = 0;
    auto cgType = StaticCast<const CGEnumType*>(CGType::GetOrCreate(cgMod, &enumTy));
    bool isOption =
        enumTy.IsOption() && !const_cast<CHIR::EnumType*>(&enumTy)->IsBoxed(cgMod.GetCGContext().GetCHIRBuilder());
    bool isOptionLike = isOption || cgType->IsOptionLike();
    if (isOptionLike) {
        for (size_t index = 0; index < enumDef->GetCtors().size(); index++) {
            size_t nonArgIndex = cgType->IsAntiOptionLike() ? 0 : 1;
            auto ctorName = nonArgIndex == index && !enumTy.IsOption() ? "N$_" + enumDef->GetCtors()[index].name
                                                                       : enumDef->GetCtors()[index].name;
            auto enumDITy = createEnumerator(ctorName, fieldIdx);
            ctors.push_back(enumDITy);
            fieldIdx++;
        }
        if (isOption) {
            name = "E2$" + name;
        }
    } else {
        for (auto& it : enumDef->GetCtors()) {
            auto enumDITy = createEnumerator(it.name, fieldIdx);
            ctors.push_back(enumDITy);
            fieldIdx++;
        }
    }
    auto size = isOptionLike ? 8u : 32u; // The size of Int32 is 32-bit.
    auto basicType = createBasicType("Int32", size, static_cast<unsigned>(llvm::dwarf::DW_ATE_signed));
    auto enumCtorType = createEnumerationType(diNamespace, name, diFile, position.GetBeginPos().line, size, 0u,
        getOrCreateArray(ctors), basicType, enumTy.ToString(), true);
    // Cache it.
    enumCtorCache[&enumTy] = llvm::TrackingMDRef(enumCtorType);
    return enumCtorType;
}

llvm::DICompositeType* DIBuilder::CreateTrivial(const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers)
{
    auto enumDef = enumTy.GetEnumDef();
    auto defPackage = createNameSpace(diCompileUnit, enumDef->GetPackageName(), false);
    auto& position = enumDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto typeName = GenerateCustomTypeName(enumTy, false);
    auto fwdDecl = createStructType(defPackage, "E0$" + typeName, diFile, position.GetBeginPos().line, 32u, 0u,
        llvm::DINode::FlagZero, nullptr, {}, 0u, nullptr, "$" + enumTy.ToString());
    auto constructorType = GetOrCreateEnumCtorType(enumTy);
    auto ctorType =
        createMemberType(fwdDecl, "constructor", diFile, 0u, 32u, 0u, 0u, llvm::DINode::FlagZero, constructorType);
    typeCache[&enumTy] = llvm::TrackingMDRef(fwdDecl);
    enumMembers.push_back(ctorType);
    return fwdDecl;
}

llvm::DICompositeType* DIBuilder::CreateEnumWithNonRefArgsType(
    const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers)
{
    /**
     *  The Enum is stored as
     *  32-bits // constructor
     *  X-bits // X is max size of union
     */
    auto enumDef = enumTy.GetEnumDef();
    auto defPackage = createNameSpace(diCompileUnit, enumDef->GetPackageName(), false);
    auto& position = enumDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto typeName = GenerateCustomTypeName(enumTy, false);
    auto cgType = StaticCast<const CGEnumType*>(CGType::GetOrCreate(cgMod, &enumTy));
    auto fwdDecl = createStructType(defPackage, "E3$" + typeName, diFile, position.GetBeginPos().line,
        cgType->GetsizeOfConstructorUnion() + 32, 0u, llvm::DINode::FlagZero, nullptr, {}, 0u, nullptr,
        "$" + enumTy.ToString());
    typeCache[&enumTy] = llvm::TrackingMDRef(fwdDecl);
    auto constructorType = GetOrCreateEnumCtorType(enumTy);
    uint32_t fieldIdx = 0;
    for (auto& ctor : enumTy.GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder())) {
        CodeGenDIVector4 enumLayer{};
        auto size = 32u;
        std::string ctorName = GenerateCustomTypeName(enumTy, false) + "_ctor_" + std::to_string(fieldIdx);
        // For the constructor without args.
        if (ctor.funcType->GetParamTypes().empty()) {
            auto subEnumType =
                createStructType(defPackage, ctorName, diFile, 0u, size, 0u, llvm::DINode::FlagZero, nullptr, {});
            auto enumId = createMemberType(
                subEnumType, "constructor", diFile, 0u, size, 0u, 0u, llvm::DINode::FlagZero, constructorType);
            enumLayer.push_back(enumId);
            replaceArrays(subEnumType, getOrCreateArray(enumLayer));
            auto inheritType = createInheritance(fwdDecl, subEnumType, 0u, 0u, llvm::DINode::FlagZero);
            enumMembers.push_back(inheritType);
            ++fieldIdx;
            continue;
        }
        std::vector<uint64_t> sizeOfCtors = std::vector<uint64_t>(ctor.funcType->GetParamTypes().size() + 1, 0);
        // The size of constructor is 32-bits.
        size_t totalSize = 32u;
        sizeOfCtors[0] = totalSize;
        for (uint32_t argIndex = 0; argIndex < ctor.funcType->GetParamTypes().size(); ++argIndex) {
            auto arg = ctor.funcType->GetParamTypes()[argIndex];
            auto argTy = GetOrCreateType(*arg);
            if (IsReferenceType(*arg, cgMod) || arg->IsRawArray()) {
                argTy = CreatePointerType(argTy, CreateRefType()->getSizeInBits());
            }
            auto argSize = GetSizeInBits(argTy);
            sizeOfCtors[argIndex + 1] = argSize;
            totalSize += argSize;
        }
        auto subEnumType =
            createStructType(defPackage, ctorName, diFile, 0u, totalSize, 0u, llvm::DINode::FlagZero, nullptr, {});
        auto enumId = createMemberType(
            subEnumType, "constructor", diFile, 0u, size, 0u, 0u, llvm::DINode::FlagZero, constructorType);
        enumLayer.push_back(enumId);
        size_t offsetIndex = 1;
        size_t offset = 0;
        for (uint32_t argIndex = 0; argIndex < ctor.funcType->GetParamTypes().size(); ++argIndex) {
            auto arg = ctor.funcType->GetParamTypes()[argIndex];
            auto argTy = GetOrCreateType(*arg);
            if (IsReferenceType(*arg, cgMod)) {
                argTy = CreatePointerType(argTy, CreateRefType()->getSizeInBits());
            }
            offset += sizeOfCtors[argIndex];
            auto align = 0u;
            auto argType = createMemberType(subEnumType, "arg_" + std::to_string(offsetIndex), diFile, 0u,
                GetSizeInBits(argTy), static_cast<uint32_t>(align), offset, llvm::DINode::FlagZero, argTy);
            enumLayer.push_back(argType);
            ++offsetIndex;
        }
        replaceArrays(subEnumType, getOrCreateArray(enumLayer));
        auto inheritType = createInheritance(fwdDecl, subEnumType, 0u, 0u, llvm::DINode::FlagZero);
        enumMembers.push_back(inheritType);
        ++fieldIdx;
    }
    return fwdDecl;
}

llvm::DICompositeType* DIBuilder::CreateEnumOptionType(
    const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers, const CHIR::Type* boxTy)
{
    auto argTy = GetOptionLikeArgTy(enumTy, cgMod);
    auto enumDef = enumTy.GetEnumDef();
    auto& position = enumDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    if (IsBoxSelf(*argTy, cgMod)) {
        return CreateBoxSelfOptionType(enumTy, enumMembers);
    }
    CJC_ASSERT(argTy);
    llvm::DICompositeType* fwdDecl = nullptr;
    bool argTypeIsOption = IsOptionOrOptionLike(*argTy);
    bool isRefArg = IsReferenceType(*argTy, cgMod) && !argTypeIsOption;
    // Processing constructor for reference type that is not core/Option.
    if (isRefArg) {
        auto argType = GetOrCreateType(*argTy);
        auto size = enumTy.IsOption()? CreateRefType()->getSizeInBits() : GetSizeInBits(argType);
        argType = enumTy.IsOption()? CreatePointerType(argType, size) : argType;
        // It might have been generated when generating argType.
        auto ref = typeCache.find(&enumTy);
        if (ref != typeCache.end()) {
            fwdDecl = llvm::cast<llvm::DICompositeType>(ref->second);
            CodeGenDIVector16 vec(fwdDecl->getElements().begin(), fwdDecl->getElements().end());
            // Update enumMember from generated fwdDecl.
            enumMembers.assign(vec.begin(), vec.end());
            return fwdDecl;
        }
        uint64_t boxOffset = boxTy ? 64u : 0u;
        auto defPackage = createNameSpace(diCompileUnit, enumDef->GetPackageName(), false);
        auto name = enumTy.IsOption() ? GenerateCustomTypeName(enumTy, false)
                                      : "E2$" + GenerateCustomTypeName(enumTy, false);
        fwdDecl = createStructType(defPackage, name, diFile, position.GetBeginPos().line,
            size + boxOffset, argType->getAlignInBits(), llvm::DINode::FlagZero, nullptr,
            getOrCreateArray({}), 0u, nullptr, "$" + std::to_string(boxOffset) + "_" + enumTy.ToString());
        if (boxTy) {
            typeCache[boxTy] = llvm::TrackingMDRef(fwdDecl);
        }
        auto offsetBits = argTy->IsRawArray() ? size : 0u;
        auto valueType = createMemberType(fwdDecl, "val", diFile, 0u, size, 0u,
            offsetBits + boxOffset, llvm::DINode::FlagZero, argType);
        // For optionLike, should generate constructor.
        if (!enumTy.IsOption()) {
            auto constructorType = GetOrCreateEnumCtorType(enumTy);
            auto ctorType = createMemberType(
                fwdDecl, "constructor_R", diFile, 0u, 8u, 0u, 64u, llvm::DINode::FlagZero, constructorType);
            enumMembers.push_back(ctorType);
        }
        // Reference type wrapped by option does not have the constructor Some or None.
        enumMembers.push_back(valueType);
        return fwdDecl;
    } else if (argTypeIsOption) { // Processing constructor for option type.
        return CreateNestedOptionType(enumTy, enumMembers, boxTy);
    } else {
        return CreateNonRefOptionType(enumTy, enumMembers, boxTy);
    }
}

llvm::DICompositeType* DIBuilder::CreateBoxSelfOptionType(const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers)
{
    auto enumDef = enumTy.GetEnumDef();
    auto& position = enumDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto defPackage = createNameSpace(diCompileUnit, enumDef->GetPackageName(), false);
    auto name = "E2$" + GenerateCustomTypeName(enumTy, false);
    auto fwdDecl = createStructType(defPackage, name, diFile, position.GetBeginPos().line, 128u, 0u,
        llvm::DINode::FlagZero, nullptr, getOrCreateArray({}), 0u, nullptr, enumTy.ToString());
    auto constructorType = GetOrCreateEnumCtorType(enumTy);
    auto ctorType =
        createMemberType(fwdDecl, "constructor_R", diFile, 0u, 8u, 0u, 64u, llvm::DINode::FlagZero, constructorType);
    auto valType =
        createMemberType(fwdDecl, "val", diFile, 0u, 64u, 0u, 64u, llvm::DINode::FlagZero, CreatePointerType(fwdDecl));
    enumMembers.push_back(ctorType);
    enumMembers.push_back(valType);
    return fwdDecl;
}

llvm::DICompositeType* DIBuilder::CreateNestedOptionType(
    const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers, const CHIR::Type* boxTy)
{
    // Generate Option<Option<T2>> type.
    auto enumDef = enumTy.GetEnumDef();
    auto& position = enumDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    llvm::DICompositeType* fwdDecl = nullptr;
    auto optionTy = StaticCast<CHIR::EnumType*>(GetOptionLikeArgTy(enumTy, cgMod));
    auto argTy = GetOptionLikeArgTy(*optionTy, cgMod);
    auto constructorType = GetOrCreateEnumCtorType(enumTy);
    bool hasCoreOption = false;
    // case enumTy = Option<Option<T2>> where T2 = Option<T3>
    if (argTy->IsEnum()) {
        hasCoreOption = IsOptionOrOptionLike(*argTy);
    }
    // ArgType is a reference option type.
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    constexpr uint32_t enumFieldAlignment = 64uL;
#endif
    if (IsReferenceType(*optionTy, cgMod) && !hasCoreOption) {
        auto defPackage = createNameSpace(diCompileUnit, enumDef->GetPackageName(), false);
        auto name = enumTy.IsOption() ? GenerateCustomTypeName(enumTy, false)
                                      : "E2$" + GenerateCustomTypeName(enumTy, false);
        uint64_t boxOffset = boxTy ? 64u : 0u;
        fwdDecl = createStructType(defPackage, name, diFile, position.GetBeginPos().line,
            64u + enumFieldAlignment + boxOffset, 0u, llvm::DINode::FlagZero, nullptr, getOrCreateArray({}), 0u,
            nullptr, "$" + std::to_string(boxOffset) + "_" + enumTy.ToString());
        if (boxTy) {
            typeCache[boxTy] = llvm::TrackingMDRef(fwdDecl);
        } else {
            typeCache[&enumTy] = llvm::TrackingMDRef(fwdDecl);
        }
        auto argType = GetOrCreateType(*optionTy);
        if (!optionTy->IsOption()) {
            argType = CreatePointerType(argType);
        }
        CJC_NULLPTR_CHECK(argType);
        auto ctorType = createMemberType(
            fwdDecl, "constructor", diFile, 0u, 8u, 0u, boxOffset, llvm::DINode::FlagZero, constructorType);
        auto valueType = createMemberType(fwdDecl, "val", diFile, 0u, GetSizeInBits(argType), 0u,
            enumFieldAlignment + boxOffset, llvm::DINode::FlagZero, argType);
        enumMembers.push_back(ctorType);
        enumMembers.push_back(valueType);
    } else { // Processing the case that argType is nested option
        fwdDecl = CreateNonRefOptionType(enumTy, enumMembers, boxTy);
    }
    return fwdDecl;
}

llvm::DICompositeType* DIBuilder::CreateNonRefOptionType(
    const CHIR::EnumType& enumTy, CodeGenDIVector16& enumMembers, const CHIR::Type* boxTy)
{
    // Generate Option<NonRef> type.
    auto enumDef = enumTy.GetEnumDef();
    auto& position = enumDef->GetDebugLocation();
    auto diFile = GetOrCreateFile(position);
    auto argTy = GetOptionLikeArgTy(enumTy, cgMod);
    auto argType = GetOrCreateType(*argTy);
    auto constructorType = GetOrCreateEnumCtorType(enumTy);

    auto cgType = CGType::GetOrCreate(cgMod, &enumTy)->GetLLVMType();
    auto totalSize = GetTypeSize(cgType);
    auto defPackage = createNameSpace(diCompileUnit, enumDef->GetPackageName(), false);
    auto name = enumTy.IsOption() ? GenerateCustomTypeName(enumTy, false)
                                  : "E2$" + GenerateCustomTypeName(enumTy, false);
    uint64_t boxOffset = boxTy ? 64u : 0u;
    auto fwdDecl = createStructType(defPackage, name, diFile, position.GetBeginPos().line, totalSize + boxOffset,
        argType->getAlignInBits(), llvm::DINode::FlagZero, nullptr, getOrCreateArray({}), 0u, nullptr,
        "$" + std::to_string(boxOffset) + "_" + enumTy.ToString());
    auto layOut = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(llvm::cast<llvm::StructType>(cgType));
    CJC_NULLPTR_CHECK(layOut);
    auto align = layOut->getAlignment().value();
    auto ctorCodeGenType = cgType->getStructElementType(0);
    auto ctorSize = GetTypeSize(ctorCodeGenType);
    auto ctorType = createMemberType(fwdDecl, "constructor", diFile, 0u, ctorSize, static_cast<uint32_t>(align),
        boxOffset, llvm::DINode::FlagZero, constructorType);

    auto valCodeGenType = cgType->getStructElementType(1);
    auto valSize = GetTypeSize(valCodeGenType);
    auto valOffset = layOut->getElementOffsetInBits(1) + boxOffset;
    auto valName = "val";
    auto valType = createMemberType(fwdDecl, valName, diFile, 0u, valSize, static_cast<uint32_t>(align), valOffset,
        llvm::DINode::FlagZero, argType);
    enumMembers.push_back(ctorType);
    enumMembers.push_back(valType);
    return fwdDecl;
}

llvm::DIType* DIBuilder::CreateGenericType(const CHIR::GenericType& genericTy)
{
    auto name = GenerateTypeName(genericTy);
    auto genericSize = 64u;
    auto genericType = createStructType(diCompileUnit, name, defaultFile, 0u, genericSize, 0u, llvm::DINode::FlagZero,
        nullptr, getOrCreateArray({}), 0, nullptr, genericTy.GetIdentifier());
    auto ti = CreateTypeInfoMember(genericType, defaultFile);
    CodeGenDIVector4 elements{ti};
    for (auto upperBound : genericTy.GetUpperBounds()) {
        auto upperBoundType = GetOrCreateType(*upperBound);
        auto inheritType = createInheritance(genericType, upperBoundType, 0u, 0u, llvm::DINode::FlagZero);
        elements.emplace_back(inheritType);
    }
    replaceArrays(genericType, getOrCreateArray(elements));

    return genericType;
}
} // namespace CodeGen
} // namespace Cangjie
