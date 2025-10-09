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

bool IsPossiblyOverflowOperator(const VirtualFuncInfo& info)
{
    auto& name = info.srcCodeIdentifier;
    auto& type = *info.typeInfo.sigType;
    auto params = type.GetParamTypes();
    if (params.size() == 1 && IsOverflowOperator(name)) {
        return CanBeIntegerType(*params[0]) && CanBeIntegerType(*info.typeInfo.returnType);
    }
    if (params.size() == 0 && name == "-") {
        return CanBeIntegerType(*info.typeInfo.returnType);
    }
    return false;
}

bool IsBuiltinOverflowOperator(const CustomTypeDef& def, const VirtualFuncInfo& info)
{
    auto defType = def.GetType();
    auto builtinDefType = DynamicCast<BuiltinType*>(defType);
    if (!builtinDefType || !builtinDefType->IsInteger()) {
        return false;
    }
    auto& name = info.srcCodeIdentifier;
    auto& type = *info.typeInfo.sigType;
    auto params = type.GetParamTypes();
    if (params.size() == 1 && params[0] == defType && params[0]->IsInteger() &&
        params[0] == info.typeInfo.returnType) {
        return IsOverflowOperator(name);
    }
    if (params.size() == 0 && info.typeInfo.returnType->IsInteger()) {
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
    auto& vtable = def.GetVTable();
    for (auto& pair : vtable) {
        if (pair.first->GetClassDef() != &def) {
            continue;
        }
        for (size_t i{0}; i < pair.second.size(); ++i) {
            auto& entry = pair.second[i];
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
    ClassType& infType, CustomTypeDef& user, const VirtualFuncInfo& funcInfo, size_t index)
{
    static std::vector<std::string> SPLIT_OPERATOR_NAME_PREFIX = {"&", "~", "%"};
    if (IsBuiltinOverflowOperator(user, funcInfo)) {
        // for builtin overflow operator, insert compiler generated functions into vtable
        auto& extDef = StaticCast<ExtendDef>(user);
        for (size_t j{0}; j < SPLIT_OPERATOR_NAME_PREFIX.size(); ++j) {
            auto name = SPLIT_OPERATOR_NAME_PREFIX[j] + funcInfo.srcCodeIdentifier;
            auto ovfFunc = GenerateBuiltinOverflowOperatorFunc(funcInfo.srcCodeIdentifier, OVF_STRATEGIES[j], extDef,
                funcInfo.typeInfo.sigType->GetNumOfParams() == 1);
            extDef.AddMethod(ovfFunc);
            if (j == SPLIT_OPERATOR_NAME_PREFIX.size() - 1) {
                // check again to prevent incorrect rewrite
                auto& vt = user.GetVTable().at(&infType)[index];
                CJC_ASSERT(vt.srcCodeIdentifier == funcInfo.srcCodeIdentifier);
                CJC_ASSERT(vt.typeInfo.sigType == funcInfo.typeInfo.sigType);
                // reuse the vtable entry to keep the vector index
                user.UpdateVtableItem(infType, index, ovfFunc, extDef.GetExtendedType(), std::move(name));
            } else {
                auto newFuncInfo = VirtualFuncInfo{std::move(name), ovfFunc, funcInfo.attr, funcInfo.typeInfo};
                newFuncInfo.typeInfo.parentType = extDef.GetExtendedType();
                newFuncInfo.typeInfo.originalType = ovfFunc->GetFuncType();
                user.AddVtableItem(infType, std::move(newFuncInfo));
            }
        }
    } else {
        // otherwise for normal overflow operator, repeat the user defined function in vtable
        for (size_t j{0}; j < SPLIT_OPERATOR_NAME_PREFIX.size(); ++j) {
            auto name = SPLIT_OPERATOR_NAME_PREFIX[j] + funcInfo.srcCodeIdentifier;
            if (j == SPLIT_OPERATOR_NAME_PREFIX.size() - 1) {
                // check again to prevent incorrect rewrite
                auto& vt = user.GetVTable().at(&infType)[index];
                CJC_ASSERT(vt.srcCodeIdentifier == funcInfo.srcCodeIdentifier);
                CJC_ASSERT(vt.typeInfo.sigType == funcInfo.typeInfo.sigType);
                user.UpdateVtableItem(infType, index, funcInfo.instance, nullptr, std::move(name));
            } else {
                user.AddVtableItem(infType, {std::move(name), funcInfo.instance, funcInfo.attr, funcInfo.typeInfo});
            }
        }
    }
}

void UpdateOperatorVTable::RewriteVtableEntryRec(
    const ClassDef& inf, CustomTypeDef& user, const RewriteVtableInfo& info)
{
    auto& vtable = user.GetVTable();
    // use the checked vtable indices, rather than check them again
    // check subclass vtable is inaccurate for generic interface inherited by non generic class def.
    // e.g. interface B<T> { operator func+(other: Int8): T } // this is possibly overflow operator
    // struct C <: B<C> { public operator func+(_: Int8): C } // while this is not
    for (auto& pair : vtable) {
        if (pair.first->GetClassDef() != &inf) {
            continue;
        }
        for (auto i : info.ov) {
            // copy it, as funcInfo changes during rewrite
            auto funcInfo = pair.second[i];
            RewriteOneVtableEntry(*pair.first, user, funcInfo, i);
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
        for (const auto& it : def->GetVTable()) {
            auto parentDef = it.first->GetClassDef();
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
