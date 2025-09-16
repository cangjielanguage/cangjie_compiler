// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "CGContext.h"

#include "CGContextImpl.h"
#include "CGModule.h"
#include "cangjie/Option/Option.h"

namespace Cangjie {
namespace CodeGen {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
CGContext::CGContext(const SubCHIRPackage& subCHIRPackage, CGPkgContext& cgPkgContext)
    : cgPkgContext(cgPkgContext), subCHIRPackage(subCHIRPackage), llvmContext(nullptr)
{
    llvmContext = new llvm::LLVMContext(); // This `llvmContext` will be released in the de-constructor of `CGModule`.
    llvmContext->setOpaquePointers(cgPkgContext.GetGlobalOptions().enableOpaque);
    impl = std::make_unique<CGContextImpl>();
}
#endif

CGContext::~CGContext() = default;

llvm::StructType* CGContext::GetCjStringType() const
{
    auto p1i8Type = llvm::Type::getInt8PtrTy(*llvmContext, 1u);
    auto int32Type = llvm::Type::getInt32Ty(*llvmContext);
    const std::string stringTypeStr = "record.std.core:String";
    if (auto stringType = llvm::StructType::getTypeByName(*llvmContext, stringTypeStr)) {
        return stringType;
    } else {
        return llvm::StructType::create(*llvmContext, {p1i8Type, int32Type, int32Type}, stringTypeStr);
    }
}

void CGContext::Add2CGTypePool(CGType* cgType)
{
    impl->cgTypePool.emplace_back(cgType);
}

void CGContext::Clear()
{
    impl->Clear();
    cjStrings.clear();
    generatedStructType.clear();
    globalsOfCompileUnit.clear();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvmUsedGVs.clear();
    subCHIRPackage.Clear();
    callBasesToInline.clear();
    callBasesToReplace.clear();
    debugLocOfRetExpr.clear();
#endif
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
llvm::Value* CGContext::GetBasePtrOf(llvm::Value* val) const
{
    if (auto it = impl->valueAndBasePtrMap.find(val); it != impl->valueAndBasePtrMap.end()) {
        return it->second;
    }
    return nullptr;
}

void CGContext::SetBasePtr(const llvm::Value* val, llvm::Value* basePtr)
{
    impl->valueAndBasePtrMap[val] = basePtr;
}
#endif

void CGContext::PushUnwindBlockStack(llvm::BasicBlock* unwindBlock)
{
    unwindBlockStack.push(unwindBlock);
}

std::optional<llvm::BasicBlock*> CGContext::TopUnwindBlockStack() const
{
    if (unwindBlockStack.empty() || unwindBlockStack.top() == nullptr) {
        return std::nullopt;
    } else {
        return unwindBlockStack.top();
    }
}

void CGContext::PopUnwindBlockStack()
{
    if (!unwindBlockStack.empty()) {
        unwindBlockStack.pop();
    }
}

void CGContext::AddGeneratedStructType(const std::string& structTypeName)
{
    CJC_ASSERT(!structTypeName.empty());
    generatedStructType.emplace(structTypeName);
}
const std::set<std::string>& CGContext::GetGeneratedStructType() const
{
    return generatedStructType;
}
bool CGContext::IsGeneratedStructType(const std::string& structTypeName)
{
    return generatedStructType.find(structTypeName) != generatedStructType.end();
}

void CGContext::AddGlobalsOfCompileUnit(const std::string& globalsName)
{
    globalsOfCompileUnit.emplace(globalsName);
}

bool CGContext::IsGlobalsOfCompileUnit(const std::string& globalsName)
{
    return globalsOfCompileUnit.find(globalsName) != globalsOfCompileUnit.end();
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void CGContext::AddNullableReference(llvm::Value* value)
{
    (void)impl->nullableReference.emplace(value);
}
#endif
} // namespace CodeGen
} // namespace Cangjie
