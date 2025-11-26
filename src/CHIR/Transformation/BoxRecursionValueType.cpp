// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/BoxRecursionValueType.h"

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Visitor/Visitor.h"

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

bool StoreElementRefNeedBox(const StoreElementRef& ser, CHIRBuilder& builder)
{
    auto targetType = GetTargetType(*ser.GetLocation()->GetType(), ser.GetPath(), builder);
    auto srcType = ser.GetValue()->GetType();
    if (LeftIsBoxTypeOfRight(*targetType, *srcType)) {
        return true;
    } else {
        return false;
    }
}

bool GetElementRefNeedUnBox(const GetElementRef& ger, CHIRBuilder& builder)
{
    // If the base of the GetElementRef expression is enum, it must be the index on which we want to get enum.
    // Therefore, we do not need to box the targetType.
    auto baseType = ger.GetLocation()->GetType()->StripAllRefs();
    if (baseType->IsEnum()) {
        return false;
    }
    auto srcType = GetTargetType(*baseType, ger.GetPath(), builder);
    auto targetType = ger.GetResult()->GetType()->StripAllRefs();
    if (LeftIsBoxTypeOfRight(*srcType, *targetType)) {
        auto users = ger.GetResult()->GetUsers();
        CJC_ASSERT(users.size() == 1 && users[0]->GetExprKind() == CHIR::ExprKind::LOAD);
        return true;
    } else {
        return false;
    }
}

std::pair<bool, std::vector<size_t>> TupleNeedBox(const Tuple& tuple)
{
    std::pair<bool, std::vector<size_t>> retVal = {false, {}};
    auto tupleRes = tuple.GetResult();
    if (!tupleRes->GetType()->IsEnum()) {
        return retVal;
    }
    auto operands = tuple.GetOperands();
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
            retVal.first = true;
            retVal.second.emplace_back(i + 1);
        }
    }
    return retVal;
}

std::pair<std::pair<TypeCast*, std::vector<size_t>>, Field*> FieldAndTypeCastNeedUnBox(
    Field& field, const std::vector<std::pair<TypeCast*, std::vector<size_t>>>& collected, CHIRBuilder& builder)
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
     *  %3: Struct-S = UnBox(%2)
     */
    auto index = field.GetPath();
    if (index.size() != 1) {
        return {{nullptr, {}}, nullptr};
    }
    if (index[0] == 0) {
        return {{nullptr, {}}, nullptr};
    }
    auto base = field.GetBase();
    if (!base->IsLocalVar()) {
        return {{nullptr, {}}, nullptr};
    }
    auto baseExpr = StaticCast<LocalVar*>(base)->GetExpr();
    if (baseExpr->GetExprKind() != CHIR::ExprKind::TYPECAST) {
        return {{nullptr, {}}, nullptr};
    }
    auto typecast = StaticCast<TypeCast*>(baseExpr);
    auto srcType = typecast->GetSourceTy();
    if (!srcType->IsEnum()) {
        return {{nullptr, {}}, nullptr};
    }
    auto targetType = typecast->GetTargetTy();
    if (!targetType->IsTuple()) {
        return {{nullptr, {}}, nullptr};
    }
    for (auto& cast : collected) {
        if (cast.first != typecast) {
            continue;
        }
        for (auto i : cast.second) {
            if (i == index[0]) {
                return {{nullptr, {}}, &field};
            }
        }
        return {{nullptr, {}}, nullptr};
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
        return {{nullptr, {}}, nullptr};
    } else {
        return {{typecast, path}, fieldRes};
    }
}

bool FieldNeedUnBox(Field& field, CHIRBuilder& builder)
{
    auto baseType = field.GetBase()->GetType();
    if (baseType->IsEnum()) {
        return false;
    }
    auto targetType = GetTargetType(*baseType, field.GetPath(), builder);
    auto srcType = field.GetResult()->GetType();
    if (LeftIsBoxTypeOfRight(*targetType, *srcType)) {
        return true;
    } else {
        return false;
    }
}

void InsertBoxBeforeStoreElementRef(StoreElementRef& ser, CHIRBuilder& builder)
{
    auto parent = ser.GetParentBlock();
    auto srcValue = ser.GetValue();
    auto boxType = builder.GetType<RefType>(builder.GetType<BoxType>(srcValue->GetType()));
    auto boxExpr = builder.CreateExpression<Box>(boxType, srcValue, parent);
    boxExpr->MoveBefore(&ser);
    ser.ReplaceOperand(srcValue, boxExpr->GetResult());
}

void InsertUnBoxAfterGetElementRef(GetElementRef& ger, CHIRBuilder& builder)
{
    auto parent = ger.GetParentBlock();
    auto location = ger.GetLocation();
    auto gerLoc = ger.GetDebugLocation();
    auto& path = ger.GetPath();
    auto targetType = ger.GetResult()->GetType()->StripAllRefs();
    auto boxType = builder.GetType<RefType>(builder.GetType<BoxType>(targetType));
    auto gerResType = builder.GetType<RefType>(boxType);
    auto newGer = builder.CreateExpression<GetElementRef>(gerLoc, gerResType, location, path, parent);
    newGer->MoveBefore(&ger);

    auto load = ger.GetResult()->GetUsers()[0];
    auto loadLoc = load->GetDebugLocation();
    auto newLoad = builder.CreateExpression<Load>(loadLoc, boxType, newGer->GetResult(), parent);
    newLoad->MoveBefore(&ger);

    auto unbox = builder.CreateExpression<UnBox>(targetType, newLoad->GetResult(), parent);
    load->ReplaceWith(*unbox);
    ger.RemoveSelfFromBlock();
}

void InsertBoxBeforeTuple(Tuple& tuple, const std::vector<size_t>& path, CHIRBuilder& builder)
{
    auto parent = tuple.GetParentBlock();
    for (auto i : path) {
        auto srcValue = tuple.GetOperand(i);
        auto boxType = builder.GetType<RefType>(builder.GetType<BoxType>(srcValue->GetType()));
        auto boxExpr = builder.CreateExpression<Box>(boxType, srcValue, parent);
        boxExpr->MoveBefore(&tuple);
        tuple.ReplaceOperand(srcValue, boxExpr->GetResult());
    }
}

void TypeCastToBoxType(TypeCast& typecast, const std::vector<size_t>& path, CHIRBuilder& builder)
{
    auto targetType = StaticCast<TupleType*>(typecast.GetTargetTy());
    auto eleTypes = targetType->GetElementTypes();
    for (auto i : path) {
        auto boxType = builder.GetType<RefType>(builder.GetType<BoxType>(eleTypes[i]));
        CJC_ASSERT(i < eleTypes.size());
        eleTypes[i] = boxType;
    }
    auto newTargetType = builder.GetType<TupleType>(eleTypes);
    auto parent = typecast.GetParentBlock();
    auto loc = typecast.GetDebugLocation();
    auto newTypeCast = builder.CreateExpression<TypeCast>(loc, newTargetType, typecast.GetSourceValue(), parent);
    newTypeCast->MoveBefore(&typecast);
    auto newResult = newTypeCast->GetResult();
    newResult->Set<EnumCaseIndex>(typecast.GetResult()->Get<EnumCaseIndex>());
    typecast.ReplaceWith(*newTypeCast);
}

void InsertUnBoxAfterField(Field& field, CHIRBuilder& builder)
{
    auto resType = field.GetResult()->GetType();
    auto boxType = builder.GetType<RefType>(builder.GetType<BoxType>(resType));
    auto parent = field.GetParentBlock();
    auto loc = field.GetDebugLocation();
    auto newField = builder.CreateExpression<Field>(loc, boxType, field.GetBase(), field.GetPath(), parent);
    newField->MoveBefore(&field);
    auto unbox = builder.CreateExpression<UnBox>(resType, newField->GetResult(), parent);
    field.ReplaceWith(*unbox);
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
    CreateBoxTypeForRecursionEnum();
    CreateBoxTypeForRecursionStruct();
    for (auto type : builder.GetAllCustomTypes()) {
        type->ResetAllInstantiatedType();
    }
    InsertBoxAndUnboxExprForRecursionValueType();
}

void BoxRecursionValueType::InsertBoxAndUnboxExprForRecursionValueType()
{
    std::vector<StoreElementRef*> storeElementRefs;
    std::vector<GetElementRef*> getElementRefs;
    std::vector<std::pair<Tuple*, std::vector<size_t>>> tuples;
    std::vector<std::pair<TypeCast*, std::vector<size_t>>> typecasts;
    std::vector<Field*> fields;
    std::function<VisitResult(Expression&)> visitor = [&, this](Expression& e) {
        if (e.GetExprKind() == CHIR::ExprKind::LAMBDA) {
            auto& lambda = StaticCast<Lambda&>(e);
            Visitor::Visit(*lambda.GetBody(), visitor);
        } else if (e.GetExprKind() == CHIR::ExprKind::STORE_ELEMENT_REF) {
            auto& ser = StaticCast<StoreElementRef&>(e);
            if (StoreElementRefNeedBox(ser, builder)) {
                storeElementRefs.emplace_back(&ser);
            }
        } else if (e.GetExprKind() == CHIR::ExprKind::GET_ELEMENT_REF) {
            auto& ger = StaticCast<GetElementRef&>(e);
            if (GetElementRefNeedUnBox(ger, builder)) {
                getElementRefs.emplace_back(&ger);
            }
        } else if (e.GetExprKind() == CHIR::ExprKind::TUPLE) {
            auto& tuple = StaticCast<Tuple&>(e);
            if (auto res = TupleNeedBox(tuple); res.first) {
                tuples.emplace_back(&tuple, res.second);
            }
        } else if (e.GetExprKind() == CHIR::ExprKind::FIELD) {
            auto& field = StaticCast<Field&>(e);
            auto res = FieldAndTypeCastNeedUnBox(field, typecasts, builder);
            if (res.first.first != nullptr) {
                typecasts.emplace_back(res.first);
            }
            if (res.second != nullptr) {
                fields.emplace_back(res.second);
            }
            if (FieldNeedUnBox(field, builder)) {
                fields.emplace_back(&field);
            }
        }
        return VisitResult::CONTINUE;
    };
    for (auto func : pkg.GetGlobalFuncs()) {
        Visitor::Visit(*func, visitor);
    }
    for (auto e : storeElementRefs) {
        InsertBoxBeforeStoreElementRef(*e, builder);
    }
    for (auto e : getElementRefs) {
        InsertUnBoxAfterGetElementRef(*e, builder);
    }
    for (auto& e : tuples) {
        InsertBoxBeforeTuple(*e.first, e.second, builder);
    }
    for (auto& e : typecasts) {
        TypeCastToBoxType(*e.first, e.second, builder);
    }
    for (auto e : fields) {
        InsertUnBoxAfterField(*e, builder);
    }
}
