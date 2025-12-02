// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the CHIRBuilder class in CHIR.
 */

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/CHIRContext.h"

#include "cangjie/Basic/Print.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/Mangle/CHIRMangler.h"

using namespace Cangjie::CHIR;

CHIRBuilder::CHIRBuilder(CHIRContext& context, size_t threadIdx) : context(context), threadIdx(threadIdx)
{
}

CHIRBuilder::~CHIRBuilder()
{
    MergeAllocatedInstance();
}

// ===--------------------------------------------------------------------=== //
// BlockGroup API
// ===--------------------------------------------------------------------=== //
BlockGroup* CHIRBuilder::CreateBlockGroup(Func& func)
{
    auto blockGroup = new BlockGroup(std::to_string(func.GenerateBlockGroupId()));
    this->allocatedBlockGroups.push_back(blockGroup);
    return blockGroup;
}

// ===--------------------------------------------------------------------===//
// Block API
// ===--------------------------------------------------------------------===//
Block* CHIRBuilder::CreateBlock(BlockGroup* parentGroup)
{
    CJC_NULLPTR_CHECK(parentGroup);
    auto func = parentGroup->GetTopLevelFunc();
    CJC_NULLPTR_CHECK(func);
    std::string idstr = "#" + std::to_string(func->GenerateBlockId());

    auto basicBlock = new Block(idstr, parentGroup);
    this->allocatedBlocks.push_back(basicBlock);
    if (markAsCompileTimeValue) {
        basicBlock->EnableAttr(Attribute::CONST);
    }
    return basicBlock;
}

// split one block to two blocks, and remove separator
std::pair<Block*, Block*> CHIRBuilder::SplitBlock(const Expression& separator)
{
    auto block1 = separator.GetParentBlock();
    auto block2 = CreateBlock(block1->GetParentBlockGroup());
    bool needMove = false;
    for (auto expr : block1->GetExpressions()) {
        if (expr == &separator) {
            needMove = true;
            expr->RemoveSelfFromBlock();
            auto term = CreateTerminator<GoTo>(block2, block1);
            block1->AppendExpression(term);
            continue;
        }
        if (needMove) {
            expr->MoveTo(*block2);
        }
    }
    return std::pair<Block*, Block*>{block1, block2};
}

// ===--------------------------------------------------------------------===//
// Value API
// ===--------------------------------------------------------------------===//

Parameter* CHIRBuilder::CreateParameter(Type* ty, const DebugLocation& loc, Func& parentFunc)
{
    auto id = parentFunc.GenerateLocalId();
    auto param = new Parameter(ty, "%" + std::to_string(id), &parentFunc);
    param->EnableAttr(Attribute::READONLY);
    param->SetDebugLocation(loc);
    this->allocatedValues.push_back(param);
    return param;
}

Parameter* CHIRBuilder::CreateParameter(Type* ty, const DebugLocation& loc, Lambda& parentLambda)
{
    CJC_NULLPTR_CHECK(parentLambda.GetTopLevelFunc());
    auto id = parentLambda.GetTopLevelFunc()->GenerateLocalId();
    auto param = new Parameter(ty, "%" + std::to_string(id), parentLambda);
    param->EnableAttr(Attribute::READONLY);
    param->SetDebugLocation(loc);
    this->allocatedValues.push_back(param);
    return param;
}

GlobalVar* CHIRBuilder::CreateGlobalVar(const DebugLocation& loc, RefType* ty, const std::string& mangledName,
    const std::string& srcCodeIdentifier, const std::string& rawMangledName, const std::string& packageName)
{
    GlobalVar* globalVar = new GlobalVar(ty, "@" + mangledName, srcCodeIdentifier, rawMangledName, packageName);
    globalVar->SetDebugLocation(loc);
    this->allocatedValues.push_back(globalVar);
    if (context.GetCurPackage() != nullptr) {
        context.GetCurPackage()->AddGlobalVar(globalVar);
    }
    return globalVar;
}

// ===--------------------------------------------------------------------===//
// Expression API
// ===--------------------------------------------------------------------===//

Func* CHIRBuilder::CreateFunc(const DebugLocation& loc, FuncType* funcTy, const std::string& mangledName,
    const std::string& srcCodeIdentifier, const std::string& rawMangledName, const std::string& packageName,
    const std::vector<GenericType*>& genericTypeParams)
{
    Func* func = new Func(funcTy, "@" + mangledName, srcCodeIdentifier, rawMangledName, packageName, genericTypeParams);
    this->allocatedValues.push_back(func);
    if (context.GetCurPackage() != nullptr) {
        context.GetCurPackage()->AddGlobalFunc(func);
    }
    func->SetDebugLocation(loc);
    return func;
}

// ===--------------------------------------------------------------------===//
// StructDef API
// ===--------------------------------------------------------------------===//
StructDef* CHIRBuilder::CreateStruct(const DebugLocation& loc, const std::string& srcCodeIdentifier,
    const std::string& mangledName, const std::string& pkgName, bool isImported)
{
    StructDef* ret = new StructDef(srcCodeIdentifier, "@" + mangledName, pkgName);
    this->allocatedStructs.push_back(ret);
    if (context.GetCurPackage() != nullptr) {
        if (isImported) {
            context.GetCurPackage()->AddImportedStruct(ret);
            ret->EnableAttr(Attribute::IMPORTED);
        } else {
            context.GetCurPackage()->AddStruct(ret);
        }
    }
    ret->SetDebugLocation(loc);
    return ret;
}
// ===--------------------------------------------------------------------===//
// ClassDef API
// ===--------------------------------------------------------------------===//
ClassDef* CHIRBuilder::CreateClass(const DebugLocation& loc,
    const std::string& srcCodeIdentifier, const std::string& mangledName, const std::string& pkgName, bool isClass,
    bool isImported)
{
    ClassDef* ret = new ClassDef(srcCodeIdentifier, "@" + mangledName, pkgName, isClass);
    this->allocatedClasses.push_back(ret);
    if (context.GetCurPackage() != nullptr) {
        if (isImported) {
            context.GetCurPackage()->AddImportedClass(ret);
            ret->EnableAttr(Attribute::IMPORTED);
        } else {
            context.GetCurPackage()->AddClass(ret);
        }
    }
    ret->SetDebugLocation(loc);
    return ret;
}
// ===--------------------------------------------------------------------===//
// EnumDef API
// ===--------------------------------------------------------------------===//
EnumDef* CHIRBuilder::CreateEnum(const DebugLocation& loc, const std::string& srcCodeIdentifier,
    const std::string& mangledName, const std::string& pkgName, bool isImported, bool isNonExhaustive)
{
    EnumDef* ret = new EnumDef(srcCodeIdentifier, "@" + mangledName, pkgName, isNonExhaustive);
    this->allocatedEnums.push_back(ret);
    if (context.GetCurPackage() != nullptr) {
        if (isImported) {
            context.GetCurPackage()->AddImportedEnum(ret);
            ret->EnableAttr(Attribute::IMPORTED);
        } else {
            context.GetCurPackage()->AddEnum(ret);
        }
    }
    ret->SetDebugLocation(loc);
    return ret;
}
// ===--------------------------------------------------------------------===//
// ExtendDef API
// ===--------------------------------------------------------------------===//
ExtendDef* CHIRBuilder::CreateExtend(const DebugLocation& loc, const std::string& mangledName,
    const std::string& pkgName, bool isImported, const std::vector<GenericType*> genericParams)
{
    ExtendDef* ret = new ExtendDef("@" + mangledName, pkgName, genericParams);
    this->allocatedExtends.emplace_back(ret);
    if (context.GetCurPackage() != nullptr) {
        if (isImported) {
            context.GetCurPackage()->AddImportedExtend(ret);
            ret->EnableAttr(Attribute::IMPORTED);
        } else {
            context.GetCurPackage()->AddExtend(ret);
        }
    }
    ret->SetDebugLocation(loc);
    return ret;
}
// ===--------------------------------------------------------------------===//
// Package API
// ===--------------------------------------------------------------------===//
Package* CHIRBuilder::CreatePackage(const std::string& name)
{
    Package* pkg = new Package(name);
    context.SetCurPackage(pkg);
    return pkg;
}

Package* CHIRBuilder::GetCurPackage() const
{
    return context.GetCurPackage();
}

std::unordered_set<CustomType*> CHIRBuilder::GetAllCustomTypes() const
{
    std::unordered_set<CustomType*> result;
    for (auto ty : context.dynamicAllocatedTys) {
        if (auto customTy = DynamicCast<CustomType*>(ty); customTy) {
            result.emplace(customTy);
        }
    }
    for (auto ty : context.constAllocatedTys) {
        if (auto customTy = DynamicCast<CustomType*>(ty); customTy) {
            result.emplace(customTy);
        }
    }
    return result;
}

std::unordered_set<GenericType*> CHIRBuilder::GetAllGenericTypes() const
{
    std::unordered_set<GenericType*> result;
    for (auto ty : context.dynamicAllocatedTys) {
        if (auto genericTy = DynamicCast<GenericType*>(ty); genericTy) {
            result.emplace(genericTy);
        }
    }
    for (auto ty : context.constAllocatedTys) {
        if (auto genericTy = DynamicCast<GenericType*>(ty); genericTy) {
            result.emplace(genericTy);
        }
    }
    return result;
}

void CHIRBuilder::EnableIRCheckerAfterPlugin()
{
    enableIRCheckerAfterPlugin = true;
}

void CHIRBuilder::DisableIRCheckerAfterPlugin()
{
    enableIRCheckerAfterPlugin = false;
}

bool CHIRBuilder::IsEnableIRCheckerAfterPlugin() const
{
    return enableIRCheckerAfterPlugin;
}
