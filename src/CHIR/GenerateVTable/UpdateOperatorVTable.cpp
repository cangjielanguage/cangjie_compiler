// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/GenerateVTable/UpdateOperatorVTable.h"

#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/ExtendDef.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

namespace {
constexpr OverflowStrategy OVF_STRATEGIES[]{
    OverflowStrategy::WRAPPING, OverflowStrategy::THROWING, OverflowStrategy::SATURATING};

bool IsPossiblyOverflowOperator(const VirtualMethodInfo& info)
{
    auto name = info.GetMethodName();
    auto paramTypes = info.GetMethodSigType()->GetParamTypes();
    if (paramTypes.size() == 1 && IsOverflowOperator(name)) {
        return CanBeIntegerType(*paramTypes[0]) && CanBeIntegerType(*info.GetMethodInstRetType());
    }
    if (paramTypes.size() == 0 && name == "-") {
        return CanBeIntegerType(*info.GetMethodInstRetType());
    }
    return false;
}

bool IsBuiltinOverflowOperator(const CustomTypeDef& def, const VirtualMethodInfo& info)
{
    auto defType = def.GetType();
    auto builtinDefType = DynamicCast<BuiltinType*>(defType);
    if (!builtinDefType || !builtinDefType->IsInteger()) {
        return false;
    }
    auto name = info.GetMethodName();
    auto paramTypes = info.GetMethodSigType()->GetParamTypes();
    if (paramTypes.size() == 1 && paramTypes[0] == defType && paramTypes[0]->IsInteger() &&
        paramTypes[0] == info.GetMethodInstRetType()) {
        return IsOverflowOperator(name);
    }
    if (paramTypes.size() == 0 && info.GetMethodInstRetType()->IsInteger()) {
        return name == "-";
    }
    return false;
}

CHIR::ExprKind ToExprKind(const std::string& name)
{
    if (name == "+") {
        return CHIR::ExprKind::ADD;
    }
    if (name == "-") {
        return CHIR::ExprKind::SUB;
    }
    if (name == "*") {
        return CHIR::ExprKind::MUL;
    }
    CJC_ASSERT(name == "/");
    return CHIR::ExprKind::DIV;
}
} // namespace

bool UpdateOperatorVTable::RewriteInfoOrdering::operator()(ClassDef* one, ClassDef* another) const
{
    return one->GetIdentifier() < another->GetIdentifier();
}

UpdateOperatorVTable::UpdateOperatorVTable(const Package& package, CHIRBuilder& builder)
    : package(package), builder(builder)
{
}

void UpdateOperatorVTable::CollectOverflowOperators()
{
    // collect all imported and source interfaces
    for (auto def : package.GetClasses()) {
        if (def->IsInterface()) {
            CollectOverflowOperatorsOnInterface(*def);
        }
    }
    for (auto def : package.GetImportedClasses()) {
        if (def->IsInterface()) {
            CollectOverflowOperatorsOnInterface(*def);
        }
    }
}

void UpdateOperatorVTable::AddRewriteInfo(ClassDef& def, size_t index)
{
    interRewriteInfo[&def].ov.insert(index);
}

void UpdateOperatorVTable::CollectOverflowOperatorsOnInterface(ClassDef& def)
{
    for (const auto& vtableIt : def.GetDefVTable().GetTypeVTables()) {
        if (vtableIt.GetSrcParentType()->GetClassDef() != &def) {
            continue;
        }
        for (size_t i{0}; i < vtableIt.GetMethodNum(); ++i) {
            const auto& entry = vtableIt.GetVirtualMethods()[i];
            if (IsPossiblyOverflowOperator(entry)) {
                AddRewriteInfo(def, i);
            }
        }
    }
}

Func* UpdateOperatorVTable::GenerateBuiltinOverflowOperatorFunc(
    const std::string& name, OverflowStrategy ovf, const ExtendDef& user, bool isBinary)
{
    auto type = StaticCast<BuiltinType*>(user.GetExtendedType());
    auto mangledName = CHIRMangling::GenerateOverflowOperatorFuncMangleName(name, ovf, isBinary, *type);
    if (auto it = cache.find(mangledName); it != cache.cend()) {
        return it->second;
    }
    auto rawMangledName = OverflowStrategyPrefix(ovf) + name;
    auto packageName = user.GetPackageName();
    auto funcType = isBinary ? builder.GetType<FuncType>(std::vector<Type*>{type, type}, type)
                             : builder.GetType<FuncType>(std::vector<Type*>{type}, type);
    auto func = builder.CreateFunc(
        INVALID_LOCATION, funcType, mangledName, std::move(rawMangledName), "", packageName);
    cache[std::move(mangledName)] = func;
    func->EnableAttr(Attribute::NO_REFLECT_INFO); // because it is in extend
    func->EnableAttr(Attribute::NO_DEBUG_INFO);
    func->EnableAttr(Attribute::COMPILER_ADD);
    func->EnableAttr(Attribute::OPERATOR);
    func->Set<LinkTypeInfo>(Linkage::INTERNAL);
    BlockGroup* body = builder.CreateBlockGroup(*func);
    func->InitBody(*body);

    auto block = builder.CreateBlock(body);
    body->SetEntryBlock(block);
    auto retValue =
        builder.CreateExpression<Allocate>(INVALID_LOCATION, builder.GetType<RefType>(type), type, block);
    block->AppendExpression(retValue);
    func->SetReturnValue(*retValue->GetResult());
    if (isBinary) {
        auto p1 = builder.CreateParameter(type, INVALID_LOCATION, *func);
        auto p2 = builder.CreateParameter(type, INVALID_LOCATION, *func);
        auto add = builder.CreateExpression<BinaryExpression>(
            INVALID_LOCATION, type, ToExprKind(name), p1, p2, ovf, block);
        block->AppendExpression(add);
        auto store = builder.CreateExpression<Store>(
            INVALID_LOCATION, builder.GetUnitTy(), add->GetResult(), retValue->GetResult(), block);
        block->AppendExpression(store);
    } else {
        auto p1 = builder.CreateParameter(type, INVALID_LOCATION, *func);
        CJC_ASSERT(name == "-");
        auto add = builder.CreateExpression<UnaryExpression>(
            INVALID_LOCATION, type, ExprKind::NEG, p1, ovf, block);
        block->AppendExpression(add);
        auto store = builder.CreateExpression<Store>(
            INVALID_LOCATION, builder.GetUnitTy(), add->GetResult(), retValue->GetResult(), block);
        block->AppendExpression(store);
    }
    auto exit = builder.CreateTerminator<Exit>(INVALID_LOCATION, block);
    block->AppendExpression(exit);
    func->EnableAttr(Attribute::VIRTUAL);
    func->EnableAttr(Attribute::FINAL);
    return func;
}

void UpdateOperatorVTable::RewriteOneVtableEntry(
    ClassType& infType, CustomTypeDef& user, const VirtualMethodInfo& funcInfo, size_t index)
{
    static std::vector<std::string> SPLIT_OPERATOR_NAME_PREFIX = {"&", "~", "%"};
    if (IsBuiltinOverflowOperator(user, funcInfo)) {
        // for builtin overflow operator, insert compiler generated functions into vtable
        auto& extDef = StaticCast<ExtendDef>(user);
        for (size_t j{0}; j < SPLIT_OPERATOR_NAME_PREFIX.size(); ++j) {
            auto name = SPLIT_OPERATOR_NAME_PREFIX[j] + funcInfo.GetMethodName();
            auto ovfFunc = GenerateBuiltinOverflowOperatorFunc(funcInfo.GetMethodName(), OVF_STRATEGIES[j], extDef,
                funcInfo.GetMethodSigType()->GetNumOfParams() == 1);
            extDef.AddMethod(ovfFunc);
            if (j == SPLIT_OPERATOR_NAME_PREFIX.size() - 1) {
                // check again to prevent incorrect rewrite
                const auto& vt = user.GetDefVTable().GetExpectedTypeVTable(infType).GetVirtualMethods()[index];
                CJC_ASSERT(vt.GetMethodName() == funcInfo.GetMethodName());
                CJC_ASSERT(vt.GetMethodSigType() == funcInfo.GetMethodSigType());
                // reuse the vtable entry to keep the vector index
                user.UpdateVtableItem(infType, index, ovfFunc, extDef.GetExtendedType(), std::move(name));
            } else {
                auto newFuncInfo = funcInfo;
                newFuncInfo.SetFuncName(std::move(name));
                newFuncInfo.SetVirtualMethod(ovfFunc);
                newFuncInfo.SetInstParentType(*extDef.GetExtendedType());
                newFuncInfo.SetOriginalFuncType(*ovfFunc->GetFuncType());
                user.AddVtableItem(infType, std::move(newFuncInfo));
            }
        }
    } else {
        // otherwise for normal overflow operator, repeat the user defined function in vtable
        for (size_t j{0}; j < SPLIT_OPERATOR_NAME_PREFIX.size(); ++j) {
            auto name = SPLIT_OPERATOR_NAME_PREFIX[j] + funcInfo.GetMethodName();
            if (j == SPLIT_OPERATOR_NAME_PREFIX.size() - 1) {
                // check again to prevent incorrect rewrite
                const auto& vt = user.GetDefVTable().GetExpectedTypeVTable(infType).GetVirtualMethods()[index];
                CJC_ASSERT(vt.GetMethodName() == funcInfo.GetMethodName());
                CJC_ASSERT(vt.GetMethodSigType() == funcInfo.GetMethodSigType());
                user.UpdateVtableItem(infType, index, funcInfo.GetVirtualMethod(), nullptr, std::move(name));
            } else {
                auto newFuncInfo = funcInfo;
                newFuncInfo.SetFuncName(std::move(name));
                user.AddVtableItem(infType, std::move(newFuncInfo));
            }
        }
    }
}

void UpdateOperatorVTable::RewriteVtableEntryRec(
    const ClassDef& inf, CustomTypeDef& user, const RewriteVtableInfo& info)
{
    // use the checked vtable indices, rather than check them again
    // check subclass vtable is inaccurate for generic interface inherited by non generic class def.
    // e.g. interface B<T> { operator func+(other: Int8): T } // this is possibly overflow operator
    // struct C <: B<C> { public operator func+(_: Int8): C } // while this is not
    for (auto& vtableIt : user.GetDefVTable().GetTypeVTables()) {
        if (vtableIt.GetSrcParentType()->GetClassDef() != &inf) {
            continue;
        }
        for (auto i : info.ov) {
            // copy it, as funcInfo changes during rewrite
            auto funcInfo = vtableIt.GetVirtualMethods()[i];
            RewriteOneVtableEntry(*vtableIt.GetSrcParentType(), user, funcInfo, i);
        }
    }
}

void UpdateOperatorVTable::RewriteVtable()
{
    for (auto& pair : interRewriteInfo) {
        auto& info = pair.second;
        auto inf = pair.first;
        auto it = vtableUsers.find(inf);
        if (it != vtableUsers.end()) {
            for (auto user : it->second) {
                RewriteVtableEntryRec(*inf, *user, info);
            }
        }
    }
}

void UpdateOperatorVTable::CollectVTableUsers()
{
    for (auto def : package.GetAllCustomTypeDef()) {
        if (def->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        for (const auto& vtableIt : def->GetDefVTable().GetTypeVTables()) {
            auto parentDef = vtableIt.GetSrcParentType()->GetClassDef();
            auto resIt = vtableUsers.find(parentDef);
            if (resIt == vtableUsers.end()) {
                vtableUsers[parentDef].emplace_back(def);
            } else if (std::find(resIt->second.begin(), resIt->second.end(), def) == resIt->second.end()) {
                resIt->second.emplace_back(def);
            }
        }
    }
}

void UpdateOperatorVTable::Update()
{
    CollectOverflowOperators();
    CollectVTableUsers();
    RewriteVtable();
}
