// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/BoxRecursionValueType.h"

#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"
#include "cangjie/Utils/ProfileRecorder.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

namespace {
bool IsExpectedValueType(const Type& expectedValueType,
    Type& curType, CHIRBuilder& builder, bool doCheck, std::unordered_set<Type*>& visited)
{
    if (doCheck && &curType == &expectedValueType) {
        return true;
    }
    auto [_, res] = visited.emplace(&curType);
    if (!res) {
        return false;
    }
    if (curType.IsEnum()) {
        auto& enumType = StaticCast<EnumType&>(curType);
        for (auto& ctorInfo : enumType.GetConstructorInfos(builder)) {
            for (auto ty : ctorInfo.funcType->GetParamTypes()) {
                if (IsExpectedValueType(expectedValueType, *ty, builder, true, visited)) {
                    return true;
                }
            }
        }
    } else if (curType.IsStruct()) {
        auto& structType = StaticCast<StructType&>(curType);
        for (auto ty : structType.GetInstantiatedMemberTys(builder)) {
            if (IsExpectedValueType(expectedValueType, *ty, builder, true, visited)) {
                return true;
            }
        }
    } else if (curType.IsTuple()) {
        auto& tupleType = StaticCast<TupleType&>(curType);
        for (auto ty : tupleType.GetElementTypes()) {
            if (IsExpectedValueType(expectedValueType, *ty, builder, true, visited)) {
                return true;
            }
        }
    }
    return false;
}

bool IsRecursionType(Type& specifiedType, CHIRBuilder& builder)
{
    std::unordered_set<Type*> visited;
    return IsExpectedValueType(specifiedType, specifiedType, builder, false, visited);
}

Type* GetTargetType(Type& baseType, const std::vector<uint64_t>& path, CHIRBuilder& builder)
{
    auto subType = &baseType;
    for (auto p : path) {
        subType = GetFieldOfType(*subType, p, builder);
        CJC_NULLPTR_CHECK(subType);
    }
    return subType;
}

bool LeftIsBoxTypeOfRight(const Type& left, const Type& right)
{
    std::function<bool(const Type&, const Type&)> leftIsGenericOrEqualToRight =
        [&leftIsGenericOrEqualToRight](const Type& left, const Type& right) {
            auto leftType = left.StripAllRefs();
            auto rightType = right.StripAllRefs();
            if (leftType == rightType) {
                return true;
            }
            if (leftType->IsModal()) {
                if (!rightType->IsModal()) {
                    return false;
                }
                auto leftModalInfo = leftType->GetModalInfo();
                auto rightModalInfo = rightType->GetModalInfo();
                if (leftModalInfo != rightModalInfo) {
                    return false;
                }
            }
            if (leftType->IsGeneric()) {
                return true;
            }
            if (leftType->GetTypeKind() != rightType->GetTypeKind()) {
                return false;
            }
            auto leftTypeArgs = leftType->GetTypeArgs();
            auto rightTypeArgs = rightType->GetTypeArgs();
            if (leftTypeArgs.size() != rightTypeArgs.size()) {
                return false;
            }
            if ((leftType->IsClass() || leftType->IsStruct() || leftType->IsEnum()) &&
                StaticCast<CustomType*>(leftType)->GetCustomTypeDef() !=
                StaticCast<CustomType*>(rightType)->GetCustomTypeDef()) {
                return false;
            }
            for (size_t i = 0; i < leftTypeArgs.size(); ++i) {
                if (!leftIsGenericOrEqualToRight(*leftTypeArgs[i], *rightTypeArgs[i])) {
                    return false;
                }
            }
            return true;
        };
    auto leftType = left.StripAllRefs();
    if (!leftType->IsBox()) {
        return false;
    }
    return leftIsGenericOrEqualToRight(*StaticCast<BoxType*>(leftType)->GetBaseType(), right);
}

void BoxStoreElementRefSrcValueIfNeed(StoreElementRef& ser, CHIRBuilder& builder)
{
    auto targetType = GetTargetType(*ser.GetBase()->GetType(), ser.GetPath(), builder);
    auto srcValue = ser.GetValue();
    auto srcType = srcValue->GetType();
    if (LeftIsBoxTypeOfRight(*targetType, *srcType)) {
        auto parent = ser.GetParentBlock();
        auto boxType = builder.GetType<RefType>(builder.GetType<BoxType>(srcType));
        auto boxExpr = builder.CreateExpression<Box>(boxType, srcValue, parent);
        boxExpr->MoveBefore(&ser);
        ser.ReplaceOperand(srcValue, boxExpr->GetResult());
    }
}

void UnBoxGetElementRefResIfNeed(GetElementRef& ger, CHIRBuilder& builder)
{
    // If the base of the GetElementRef expression is enum, it must be the index on which we want to get enum.
    // Therefore, we do not need to box the targetType.
    auto baseType = ger.GetBase()->GetType()->StripAllRefs();
    if (baseType->IsEnum()) {
        return;
    }
    auto srcType = GetTargetType(*baseType, ger.GetPath(), builder);
    auto targetType = ger.GetResult()->GetType()->StripAllRefs();
    if (LeftIsBoxTypeOfRight(*srcType, *targetType)) {
        auto users = ger.GetResult()->GetUsers();
        CJC_ASSERT(users.size() == 1 && users[0]->GetExprKind() == CHIR::ExprKind::LOAD);
        auto boxRefType = builder.GetType<RefType>(builder.GetType<BoxType>(targetType));
        auto gerResType = builder.GetType<RefType>(boxRefType);
        ger.GetResult()->SetType(*gerResType);

        auto load = ger.GetResult()->GetUsers()[0];
        CJC_ASSERT(load->GetExprKind() == CHIR::ExprKind::LOAD);
        auto loadRes = load->GetResult();
        loadRes->SetType(*boxRefType);

        auto loadResUsers = loadRes->GetUsers();
        auto unbox = builder.CreateExpression<UnBoxToValue>(targetType, loadRes, ger.GetParentBlock());
        unbox->MoveAfter(load);
        for (auto user : loadResUsers) {
            user->ReplaceOperand(loadRes, unbox->GetResult());
        }
    }
}

void BoxTupleOpIfNeed(Tuple& tuple, CHIRBuilder& builder)
{
    auto tupleRes = tuple.GetResult();
    if (!tupleRes->GetType()->IsEnum()) {
        return;
    }
    auto operands = tuple.GetElementValues();
    auto indexExpr = StaticCast<Constant*>(StaticCast<LocalVar*>(operands[0])->GetExpr());
    size_t index = 0;
    if (indexExpr->IsBoolLit()) {
        index = indexExpr->GetBoolLitVal() ? 1 : 0;
    } else {
        index = static_cast<size_t>(indexExpr->GetUnsignedIntLitVal());
    }
    operands.erase(operands.begin());
    auto enumDef = StaticCast<EnumType*>(tupleRes->GetType())->GetEnumDef();
    auto ctor = enumDef->GetCtor(index);
    auto paramTypes = ctor.funcType->GetParamTypes();
    CJC_ASSERT(operands.size() == paramTypes.size());
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        auto paramType = paramTypes[i];
        auto targetType = operands[i]->GetType();
        if (LeftIsBoxTypeOfRight(*paramType, *targetType)) {
            auto srcValue = tuple.GetOperand(i + 1);
            auto boxRefType = builder.GetType<RefType>(builder.GetType<BoxType>(srcValue->GetType()));
            auto boxExpr = builder.CreateExpression<Box>(boxRefType, srcValue, tuple.GetParentBlock());
            boxExpr->MoveBefore(&tuple);
            tuple.ReplaceOperand(i + 1, boxExpr->GetResult());
        }
    }
}

void InsertUnBoxAfterField(Field& field, CHIRBuilder& builder)
{
    auto fieldRes = field.GetResult();
    auto resType = fieldRes->GetType();
    auto boxRefType = builder.GetType<RefType>(builder.GetType<BoxType>(resType));
    fieldRes->SetType(*boxRefType);
    auto parent = field.GetParentBlock();
    auto oldUsers = fieldRes->GetUsers();
    auto unbox = builder.CreateExpression<UnBoxToValue>(resType, fieldRes, parent);
    unbox->MoveAfter(&field);
    for (auto user : oldUsers) {
        user->ReplaceOperand(fieldRes, unbox->GetResult());
    }
}

void CastEnumTypeToTupleWithBoxArg(TypeCast& typecast, const std::vector<size_t>& path, CHIRBuilder& builder)
{
    auto typecastRes = typecast.GetResult();
    auto targetType = StaticCast<TupleType*>(typecastRes->GetType()->StripAllRefs());
    auto eleTypes = targetType->GetElementTypes();
    for (auto i : path) {
        auto boxType = builder.GetType<RefType>(builder.GetType<BoxType>(eleTypes[i]));
        CJC_ASSERT(i < eleTypes.size());
        eleTypes[i] = boxType;
    }
    Type* newTargetType = builder.GetType<TupleType>(eleTypes);
    if (typecastRes->GetType()->StripAllRefs()->IsModal()) {
        newTargetType = builder.WithModal(newTargetType, typecastRes->GetType()->StripAllRefs()->GetModalInfo());
    }
    typecastRes->SetType(*newTargetType);
}

bool FieldAndTypeCastNeedUnBox(
    Field& field, std::unordered_map<TypeCast*, std::vector<size_t>>& collected, CHIRBuilder& builder)
{
    /** enum E {
     *      A | Box<Struct-S>&
     *  }
     *  %0: Enum-E = ...
     *  %1: Tuple<UInt64, Struct-S> = TypeCast(%0)
     *  %2: Struct-S = Field(%1, 1)
     *
     *  should be transform to:
     *  %0: Enum-E = ...
     *  %1: Tuple<UInt64, Box<Struct-S>&> = TypeCast(%0)
     *  %2: Box<Struct-S>& = Field(%1, 1)
     *  %3: Struct-S = UnBoxToValue(%2)
     */
    auto index = field.GetPath();
    if (index.size() != 1) {
        return false;
    }
    if (index[0] == 0) {
        return false;
    }
    auto base = field.GetBase();
    if (!base->IsLocalVar()) {
        return false;
    }
    auto baseExpr = StaticCast<LocalVar*>(base)->GetExpr();
    if (baseExpr->GetExprKind() != CHIR::ExprKind::CLASS_STATIC_CAST) {
        return false;
    }
    auto typecast = StaticCast<ClassStaticCast*>(baseExpr);
    auto srcType = typecast->GetSourceType();
    if (!srcType->IsEnum()) {
        return false;
    }
    auto targetType = typecast->GetTargetType();
    if (!targetType->IsTuple()) {
        return false;
    }
    if (auto it = collected.find(typecast); it != collected.end()) {
        for (auto i : it->second) {
            if (i == index[0]) {
                InsertUnBoxAfterField(field, builder);
                break;
            }
        }
        return true;
    }
    auto enumDef = StaticCast<EnumType*>(srcType)->GetEnumDef();
    auto tupleArgs = StaticCast<TupleType*>(targetType)->GetElementTypes();
    tupleArgs.erase(tupleArgs.begin());
    std::vector<size_t> path;
    Field* fieldRes = nullptr;
    size_t offset = 1;
    for (auto& ctor : enumDef->GetCtors()) {
        auto paramTypes = ctor.funcType->GetParamTypes();
        if (paramTypes.size() != tupleArgs.size()) {
            continue;
        }
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            auto paramType = paramTypes[i];
            auto argType = tupleArgs[i];
            if (LeftIsBoxTypeOfRight(*paramType, *argType)) {
                path.emplace_back(i + offset);
                if (i + offset == index[0]) {
                    fieldRes = &field;
                }
            } else if (!paramType->IsEqualOrSubTypeOf(*argType, builder)) {
                path.clear();
                fieldRes = nullptr;
                break;
            }
        }
        if (!path.empty()) {
            break;
        }
    }
    if (path.empty()) {
        return false;
    }
    collected.emplace(typecast, path);
    CastEnumTypeToTupleWithBoxArg(*typecast, path, builder);
    if (fieldRes != nullptr) {
        InsertUnBoxAfterField(*fieldRes, builder);
    }
    return true;
}

void UnBoxFieldResIfNeed(Field& field, CHIRBuilder& builder)
{
    auto baseType = field.GetBase()->GetType();
    if (baseType->IsEnum()) {
        return;
    }
    auto targetType = GetTargetType(*baseType, field.GetPath(), builder);
    auto srcType = field.GetResult()->GetType();
    if (LeftIsBoxTypeOfRight(*targetType, *srcType)) {
        InsertUnBoxAfterField(field, builder);
    }
}
}

BoxRecursionValueType::BoxRecursionValueType(Package& pkg, CHIRBuilder& builder)
    : pkg(pkg), builder(builder)
{
}

void BoxRecursionValueType::CreateBoxTypeForRecursionEnum()
{
    for (auto def : pkg.GetAllEnumDef()) {
        auto allCtors = def->GetCtors();
        for (auto& ctor : allCtors) {
            auto paramTypes = ctor.funcType->GetParamTypes();
            bool hasRecursionType = false;
            for (size_t i = 0; i < paramTypes.size(); ++i) {
                if (IsRecursionType(*paramTypes[i], builder)) {
                    hasRecursionType = true;
                    paramTypes[i] = builder.GetType<RefType>(builder.GetType<BoxType>(paramTypes[i]));
                }
            }
            if (hasRecursionType) {
                auto returnType = ctor.funcType->GetReturnType();
                ctor.funcType = builder.GetType<FuncType>(paramTypes, returnType);
            }
        }
        def->SetCtors(allCtors);
    }
}

void BoxRecursionValueType::CreateBoxTypeForRecursionStruct()
{
    for (auto def : pkg.GetAllStructDef()) {
        auto memberVars = def->GetDirectInstanceVars();
        for (auto& memberVar : memberVars) {
            if (IsRecursionType(*memberVar.type, builder)) {
                memberVar.type = builder.GetType<RefType>(builder.GetType<BoxType>(memberVar.type));
            }
        }
        def->SetDirectInstanceVars(memberVars);
    }
}

void BoxRecursionValueType::Run()
{
    Utils::ProfileRecorder recorder("Canonicalization", "BoxRecursionValueType");
    CreateBoxTypeForRecursionEnum();
    CreateBoxTypeForRecursionStruct();
    for (auto type : builder.GetAllCustomTypes()) {
        type->ResetAllInstantiatedType();
    }
    InsertBoxAndUnboxExprForRecursionValueType();
}

void BoxRecursionValueType::InsertBoxAndUnboxExprForRecursionValueType()
{
    std::unordered_map<TypeCast*, std::vector<size_t>> typecasts;
    auto visitor = [this, &typecasts](Expression& e) {
        if (e.GetExprKind() == CHIR::ExprKind::STORE_ELEMENT_REF) {
            auto& ser = StaticCast<StoreElementRef&>(e);
            BoxStoreElementRefSrcValueIfNeed(ser, builder);
        } else if (e.GetExprKind() == CHIR::ExprKind::GET_ELEMENT_REF) {
            auto& ger = StaticCast<GetElementRef&>(e);
            UnBoxGetElementRefResIfNeed(ger, builder);
        } else if (e.GetExprKind() == CHIR::ExprKind::TUPLE) {
            auto& tuple = StaticCast<Tuple&>(e);
            BoxTupleOpIfNeed(tuple, builder);
        } else if (e.GetExprKind() == CHIR::ExprKind::FIELD) {
            auto& field = StaticCast<Field&>(e);
            auto res = FieldAndTypeCastNeedUnBox(field, typecasts, builder);
            if (!res) {
                UnBoxFieldResIfNeed(field, builder);
            }
        }
        return VisitResult::CONTINUE;
    };
    for (auto func : pkg.GetGlobalFuncsWithBody()) {
        Visitor::Visit(*func, visitor);
    }
}
