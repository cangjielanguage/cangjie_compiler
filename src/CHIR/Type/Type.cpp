// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Type class in CHIR.
 */

#include "cangjie/CHIR/Type/Type.h"

#include <iostream>

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie::CHIR;

static size_t HashArgTypes(const std::vector<Type*>& argTys)
{
    std::size_t hashVal = 0;
    for (auto argTy : argTys) {
        HashCombine(hashVal, argTy);
    }
    return hashVal;
}

static const std::string PrintArgTys(const std::vector<Type*>& argTys)
{
    std::stringstream ss;
    if (argTys.size() == 0) {
        return "";
    }
    ss << "<" << argTys[0]->ToString();
    for (size_t loop = 1; loop < argTys.size(); loop++) {
        ss << "," << argTys[loop]->ToString();
    }
    ss << ">";
    return ss.str();
}

Type::Type(TypeKind kind) : kind(kind)
{
    for (auto ty : argTys) {
        CJC_NULLPTR_CHECK(ty);
    }
}

bool Type::IsAutoEnv() const
{
    return kind == TYPE_CLASS && IsClosureConversionEnvClass(*StaticCast<const ClassType*>(this)->GetClassDef());
}

bool Type::IsAutoEnvBase() const
{
    if (kind != TYPE_CLASS) {
        return false;
    }
    auto classDef = StaticCast<const ClassType*>(this)->GetClassDef();
    return IsClosureConversionEnvClass(*classDef) && classDef->TestAttr(Attribute::ABSTRACT);
}

bool Type::IsAutoEnvInstBase() const
{
    return IsAutoEnvBase() && argTys.empty();
}

bool Type::IsAutoEnvGenericBase() const
{
    return IsAutoEnvBase() && !argTys.empty();
}

bool Type::IsString() const
{
    if (IsStruct()) {
        auto ty = StaticCast<const StructType*>(this);
        auto structDef = ty->GetStructDef();
        return CheckCustomTypeDefIsExpected(*structDef, CORE_PACKAGE_NAME, STD_LIB_STRING);
    }
    return false;
}

bool Type::IsClassRef([[maybe_unused]] bool nullable) const
{
    if (!IsRef()) {
        return false;
    }
    auto& rt = StaticCast<const CHIR::RefType&>(*this);
    const auto tmpTy = rt.GetBaseType();
    return tmpTy->IsClass();
}

bool Type::IsConstant() const
{
    if (!IsTuple()) {
        return IsInteger() || IsFloat() || IsRune() || IsBoolean() || IsUnit() || IsString();
    }
    auto tupleType = static_cast<const TupleType*>(this);
    return std::all_of(tupleType->argTys.begin(), tupleType->argTys.end(),
        [](const Type* type) { return type->IsConstant(); });
}

bool Type::IsStructArray() const
{
    if (IsStruct()) {
        auto ty = StaticCast<const StructType*>(this);
        auto structDef = ty->GetStructDef();
        return CheckCustomTypeDefIsExpected(*structDef, CORE_PACKAGE_NAME, STD_LIB_ARRAY);
    }
    return false;
}

bool Type::IsBuiltinType() const
{
    return (kind >= CHIR::Type::TypeKind::TYPE_INT8 && kind <= CHIR::Type::TypeKind::TYPE_VOID) ||
        (kind >= CHIR::Type::TypeKind::TYPE_RAWARRAY && kind <= CHIR::Type::TypeKind::TYPE_CSTRING);
}

std::string Type::ToString() const
{
    auto ite = TYPEKIND_TO_STRING.find(kind);
    return ite == TYPEKIND_TO_STRING.end() ? "UnknownType" : ite->second;
}

Type* Type::StripAllRefs() const
{
    Type* baseTy = const_cast<Type*>(this);
    while (baseTy->IsRef()) {
        baseTy = StaticCast<RefType*>(baseTy)->GetBaseType();
    }
    return baseTy;
}

bool Type::IsReferenceTypeWithRefDims(size_t dims) const
{
    if (refDims != dims) {
        return false;
    }
    return StripAllRefs()->IsReferenceType();
}

bool Type::IsValueOrGenericTypeWithRefDims(size_t dims) const
{
    if (refDims != dims) {
        return false;
    }
    auto strippedTy = StripAllRefs();
    return strippedTy->IsValueType() || strippedTy->IsGeneric();
}

bool Type::IsAny() const
{
    if (kind != TYPE_CLASS) {
        return false;
    }
    return IsCoreAny(*static_cast<const ClassType*>(this)->GetClassDef());
}

bool Type::IsCJFunc() const
{
    if (kind != TYPE_FUNC) {
        return false;
    }
    // is func, but not CFunc
    return !static_cast<const FuncType*>(this)->isCFunc;
}

bool Type::IsCFunc() const
{
    if (kind != TYPE_FUNC) {
        return false;
    }
    return static_cast<const FuncType*>(this)->isCFunc;
}

bool Type::IsCType() const
{
    if (kind != TYPE_CLASS) {
        return false;
    }
    auto def = static_cast<const ClassType*>(this)->GetClassDef();
    return CheckCustomTypeDefIsExpected(*def, CORE_PACKAGE_NAME, CTYPE_NAME);
}

std::vector<FuncBase*> Type::GetDeclareAndExtendMethods([[maybe_unused]] CHIRBuilder& builder) const
{
#ifdef NDEBUG
    return {};
#else
    CJC_ABORT();
#endif
}

void Type::Dump() const
{
    std::cout << ToString() << std::endl;
}

size_t Type::Hash() const
{
    std::size_t hashVal = HashArgTypes(argTys);
    HashValue(hashVal, kind);
    return hashVal;
}

bool Type::operator==(const Type& other) const
{
    return kind == other.kind && argTys == other.argTys;
}

const std::vector<ExtendDef*>& Type::GetExtends(CHIRBuilder* builder) const
{
#ifndef NDEBUG
    (void)builder;
    CJC_ABORT();
#else
    // just for release cjc, it's not expected to go here
    return StaticCast<const BuiltinType*>(this)->GetExtends(builder);
#endif
}

std::vector<ClassType*> Type::GetSuperTypesRecusively([[maybe_unused]] CHIRBuilder& builder,
    [[maybe_unused]] std::set<std::pair<const Type*, const Type*>>* visited)
{
    CJC_ABORT();
    return std::vector<ClassType*>{};
}

bool Type::IsBoxRefTypeOf(const Type& baseType) const
{
    if (!this->IsRef()) {
        return false;
    }
    auto boxType = StaticCast<const RefType*>(this)->GetBaseType();
    if (!boxType->IsBox()) {
        return false;
    }
    return StaticCast<BoxType*>(boxType)->GetBaseType() == &baseType;
}

IntType::IntType(TypeKind kind) : NumericType(kind)
{
    CJC_ASSERT(kind >= TYPE_INT8 && kind <= TYPE_UINT_NATIVE);
}

FloatType::FloatType(TypeKind kind) : NumericType(kind)
{
    CJC_ASSERT(kind >= TYPE_FLOAT16 && kind <= TYPE_FLOAT64);
}

std::string FuncType::ToString() const
{
    std::stringstream ss;
    ss << "(";
    auto paramTys = GetParamTypes();
    for (unsigned i = 0; i < paramTys.size(); i++) {
        if (i > 0) {
            ss << ", ";
        }
        ss << paramTys[i]->ToString();
    }
    if (IsCFunc() && HasVarArg()) {
        ss << "...";
    }
    ss << ")";
    ss << " -> " << GetReturnType()->ToString();
    return isCFunc ? "CFunc<" + ss.str() + ">" : ss.str();
}

size_t FuncType::Hash() const
{
    std::size_t hashVal = HashArgTypes(argTys);
    HashValue(hashVal, isCFunc);
    HashValue(hashVal, hasVarArg);
    HashValue(hashVal, kind);
    return hashVal;
}

bool FuncType::operator==(const Type& other) const
{
    auto it = DynamicCast<const FuncType*>(&other);
    return it && argTys == it->argTys && (isCFunc == it->isCFunc) && (hasVarArg == it->hasVarArg);
}

CustomType::CustomType(TypeKind kind, CustomTypeDef* def, const std::vector<Type*>& typeArgs = {})
    : Type(kind), def(def)
{
    CJC_NULLPTR_CHECK(def);
    CJC_ASSERT((kind == TYPE_CLASS && def->GetCustomKind() == CustomDefKind::TYPE_CLASS) ||
        (kind == TYPE_STRUCT && def->GetCustomKind() == CustomDefKind::TYPE_STRUCT) ||
        (kind == TYPE_ENUM && def->GetCustomKind() == CustomDefKind::TYPE_ENUM));
    this->argTys = typeArgs;
}

CustomTypeDef* CustomType::GetCustomTypeDef() const
{
    CJC_ASSERT(def != nullptr);
    return def;
}

std::vector<ClassType*> CustomType::GetSuperTypesRecusively(CHIRBuilder& builder,
    std::set<std::pair<const Type*, const Type*>>* visited)
{
    std::vector<ClassType*> inheritanceList;
    for (auto interface : GetImplementedInterfaceTys(&builder)) {
        GetAllInstantiatedParentType(*interface, builder, inheritanceList, visited);
    }
    if (auto classTy = DynamicCast<ClassType*>(this)) {
        auto superClass = classTy->GetSuperClassTy(&builder);
        if (superClass != nullptr) {
            GetAllInstantiatedParentType(*superClass, builder, inheritanceList, visited);
        }
    }
    return inheritanceList;
}

void CustomType::GetInstMap(std::unordered_map<const GenericType*, Type*>& instMap, CHIRBuilder& builder) const
{
    // how to cache the result
    auto customTypeDef = GetCustomTypeDef();
    auto customTypeDefGenericParams = customTypeDef->GetGenericTypeParams();
    auto customTypeGenericArgs = GetGenericArgs();
    if (!customTypeDefGenericParams.empty()) {
        CJC_ASSERT(customTypeDefGenericParams.size() == customTypeGenericArgs.size());
    }
    for (size_t i = 0; i < customTypeDefGenericParams.size(); ++i) {
        instMap.emplace(customTypeDefGenericParams[i], customTypeGenericArgs[i]);
    }

    for (auto parentInterfaceTy : customTypeDef->GetImplementedInterfaceTys()) {
        auto parentInterfaceDef = parentInterfaceTy->GetCustomTypeDef();
        auto parentInterfaceDefGenericParams = parentInterfaceDef->GetGenericTypeParams();
        auto instParentInterfaceTy =
            StaticCast<ClassType*>(ReplaceRawGenericArgType(*parentInterfaceTy, instMap, builder));
        auto parentInterfaceTypeGenericArgs = instParentInterfaceTy->GetGenericArgs();
        if (!customTypeDefGenericParams.empty()) {
            CJC_ASSERT(parentInterfaceDefGenericParams.size() == parentInterfaceTypeGenericArgs.size());
        }
        for (size_t i = 0; i < parentInterfaceDefGenericParams.size(); ++i) {
            instMap.emplace(parentInterfaceDefGenericParams[i], parentInterfaceTypeGenericArgs[i]);
        }
        parentInterfaceTy->GetInstMap(instMap, builder);
    }

    if (DynamicCast<ClassType*>(this)) {
        auto classTypeDef = StaticCast<ClassDef*>(customTypeDef);
        auto parentClassTy = classTypeDef->GetSuperClassTy();
        if (parentClassTy) {
            auto parentClassDef = parentClassTy->GetCustomTypeDef();
            auto parentClassDefGenericParams = parentClassDef->GetGenericTypeParams();
            auto instparentClassTy = StaticCast<ClassType*>(ReplaceRawGenericArgType(*parentClassTy, instMap, builder));
            auto parentClassTypeGenericArgs = instparentClassTy->GetGenericArgs();
            if (!parentClassDefGenericParams.empty()) {
                CJC_ASSERT(parentClassDefGenericParams.size() == parentClassTypeGenericArgs.size());
            }
            for (size_t i = 0; i < parentClassDefGenericParams.size(); ++i) {
                instMap.emplace(parentClassDefGenericParams[i], parentClassTypeGenericArgs[i]);
            }
            parentClassTy->GetInstMap(instMap, builder);
        }
    }
}

std::vector<Type*> CustomType::GetGenericArgs() const
{
    return argTys;
}

const std::vector<ExtendDef*>& CustomType::GetExtends([[maybe_unused]]CHIRBuilder* builder) const
{
    return GetCustomTypeDef()->GetExtends();
}

std::pair<Type*, bool> CustomType::GetInstMemberTypeByPathCheckingReadOnly(
    const std::vector<uint64_t>& path, CHIRBuilder& builder) const
{
    if (path.empty()) {
#ifdef NDEBUG
        return {const_cast<CustomType*>(this), false};
#else
        CJC_ABORT();
#endif
    }
    auto customTypeDef = GetCustomTypeDef();
    std::unordered_map<const GenericType*, Type*> instMap;
    GetInstMap(instMap, builder);
    auto currentIdx = path.front();
    auto member = customTypeDef->GetAllInstanceVars()[currentIdx];
    // if one member is readonly in path, the result is readonly
    bool isReadOnly = member.TestAttr(Attribute::READONLY);
    auto memberTy = ReplaceRawGenericArgType(*member.type, instMap, builder);
    if (path.size() > 1) {
        auto pureMemberTy = memberTy;
        while (pureMemberTy->IsRef()) {
            pureMemberTy = StaticCast<RefType*>(pureMemberTy)->GetBaseType();
        }
        if (pureMemberTy->IsNominal()) {
            auto subPath = path;
            subPath.erase(subPath.begin());
            auto [memberType, isMemberReadOnly] =
                StaticCast<CustomType*>(pureMemberTy)->GetInstMemberTypeByPathCheckingReadOnly(subPath, builder);
            return {memberType, isReadOnly || isMemberReadOnly};
        } else if (pureMemberTy->IsGeneric()) {
            auto subPath = path;
            subPath.erase(subPath.begin());
            auto [memberType, isMemberReadOnly] =
                StaticCast<GenericType*>(pureMemberTy)->GetInstMemberTypeByPathCheckingReadOnly(subPath, builder);
            return {memberType, isReadOnly || isMemberReadOnly};
        } else {
            CJC_ABORT();
        }
    }
    return {memberTy, isReadOnly};
}

std::vector<Type*> CustomType::CalculateCurDefInstantiatedMemberTys(CHIRBuilder& builder)
{
    auto genericTypes = def->GetGenericTypeParams();
    if (!genericTypes.empty()) {
        CJC_ASSERT(argTys.size() == genericTypes.size());
    }
    std::unordered_map<const GenericType*, Type*> replaceTable;
    for (size_t i = 0; i < genericTypes.size(); ++i) {
        replaceTable.emplace(genericTypes[i], argTys[i]);
    }

    std::vector<Type*> ret;
    for (auto& var : def->GetDirectInstanceVars()) {
        ret.emplace_back(ReplaceRawGenericArgType(*var.type, replaceTable, builder));
    }
    return ret;
}

std::vector<Type*> CustomType::GetInstantiatedMemberTys(CHIRBuilder& builder)
{
    std::vector<Type*> ret;
    if (auto classTy = DynamicCast<ClassType*>(this); classTy && classTy->GetSuperClassTy(&builder) != nullptr) {
        ret = classTy->GetSuperClassTy(&builder)->GetInstantiatedMemberTys(builder);
    }

    std::unique_lock<std::mutex> lock(setInstMemberTyMtx);
    if (hasSetInstMemberTy) {
        ret.insert(ret.end(), instantiatedMemberTys.begin(), instantiatedMemberTys.end());
        return ret;
    } else {
        instantiatedMemberTys = CalculateCurDefInstantiatedMemberTys(builder);
        ret.insert(ret.end(), instantiatedMemberTys.begin(), instantiatedMemberTys.end());
        hasSetInstMemberTy = true;
        return ret;
    }
}

std::vector<FuncType*> CustomType::GetInstMethodTypes(CHIRBuilder& builder) const
{
    std::vector<FuncType*> instFuncTypes;
    auto typeArgs = GetGenericArgs();
    auto paramArgs = def->GetGenericTypeParams();
    std::unordered_map<const GenericType*, Type*> instMap;
    CJC_ASSERT(typeArgs.size() == paramArgs.size());
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        instMap.emplace(paramArgs[i], typeArgs[i]);
    }
    auto methods = def->GetMethods();
    for (auto method : methods) {
        instFuncTypes.emplace_back(
            StaticCast<FuncType*>(ReplaceRawGenericArgType(*method->GetType(), instMap, builder)));
    }
    return instFuncTypes;
}

std::vector<FuncBase*> CustomType::GetDeclareAndExtendMethods(CHIRBuilder& builder) const
{
    auto allMethods = def->GetMethods();
    for (auto extendDef : def->GetExtends()) {
        if (!IsEqualOrInstantiatedTypeOf(*extendDef->GetExtendedType(), builder)) {
            continue;
        }
        auto m = extendDef->GetMethods();
        allMethods.insert(allMethods.end(), m.begin(), m.end());
    }
    return allMethods;
}

size_t CustomType::Hash() const
{
    std::size_t hashVal = HashArgTypes(argTys);
    HashValue(hashVal, def);
    return hashVal;
}

bool CustomType::operator==(const Type& other) const
{
    auto it = DynamicCast<const CustomType*>(&other);
    return it && argTys == it->argTys && (def == it->def);
}

ClassType::ClassType(ClassDef* classDef, const std::vector<Type*>& genericArgs)
    : CustomType(TypeKind::TYPE_CLASS, classDef, genericArgs)
{
}

std::string ClassType::ToString() const
{
    std::stringstream ss;
    ss << "Class";
    if (def != nullptr) {
        ss << "-" << def->GetIdentifierWithoutPrefix();
    }
    ss << PrintArgTys(argTys);
    return ss.str();
}

ClassDef* ClassType::GetClassDef() const
{
    CJC_ASSERT(def != nullptr);
    return StaticCast<ClassDef*>(def);
}

ClassType* ClassType::CalculateSuperClassTy(CHIRBuilder& builder)
{
    auto genericTypes = def->GetGenericTypeParams();
    if (!genericTypes.empty()) {
        CJC_ASSERT(argTys.size() == genericTypes.size());
    }
    std::unordered_map<const GenericType*, Type*> replaceTable;
    for (size_t i = 0; i < genericTypes.size(); ++i) {
        replaceTable.emplace(genericTypes[i], argTys[i]);
    }
    ClassType* result{nullptr};
    if (auto sup = StaticCast<ClassDef*>(def)->GetSuperClassTy()) {
        result = StaticCast<ClassType*>(ReplaceRawGenericArgType(*sup, replaceTable, builder));
    }
    return result;
}

ClassType* ClassType::GetSuperClassTy(CHIRBuilder* builder)
{
    std::unique_lock<std::mutex> lock(setSuperClassMtx);
    if (hasSetSuperClass) {
        return superClassTy;
    } else {
        CJC_NULLPTR_CHECK(builder);
        superClassTy = CalculateSuperClassTy(*builder);
        hasSetSuperClass = true;
        return superClassTy;
    }
}

std::vector<AbstractMethodInfo> ClassType::GetInstAbstractMethodTypes(CHIRBuilder& builder) const
{
    std::vector<AbstractMethodInfo> instMethodInfos;
    auto typeArgs = GetGenericArgs();
    auto paramArgs = def->GetGenericTypeParams();
    std::unordered_map<const GenericType*, Type*> instMap;
    CJC_ASSERT(typeArgs.size() == paramArgs.size());
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        instMap.emplace(paramArgs[i], typeArgs[i]);
    }
    auto methods = StaticCast<const ClassDef*>(def)->GetAbstractMethods();
    for (auto& method : methods) {
        method.methodTy = ReplaceRawGenericArgType(*method.methodTy, instMap, builder);
        for (auto& paramInfo : method.paramInfos) {
            paramInfo.type = ReplaceRawGenericArgType(*paramInfo.type, instMap, builder);
        }
    }
    return methods;
}

static void CollectGenericReplaceTable(
    Type& mayBeGeneric, Type& instType, std::unordered_map<const GenericType*, Type*>& replaceTable)
{
    if (auto genericType = Cangjie::DynamicCast<const GenericType*>(&mayBeGeneric)) {
        replaceTable.emplace(genericType, &instType);
    } else {
        auto genericTypeArgs = mayBeGeneric.GetTypeArgs();
        auto instTypeArgs = instType.GetTypeArgs();
        if (genericTypeArgs.size() != instTypeArgs.size()) {
            /*
             * need to choose right extend type, not all extend is suitable
             * example:
             * a enum type:
             *   public enum OP<T>
             * with extends as follow:
             *   extend<T> OP<OP<T>>
             *   extend<T> OP<T> <: I2<T>
             *
             * if we have inst type OP<Int64>, when traverse to the first extend, the args size are not same.
             */
            return;
        }
        for (size_t i = 0; i < genericTypeArgs.size(); ++i) {
            CollectGenericReplaceTable(*genericTypeArgs[i], *instTypeArgs[i], replaceTable);
        }
    }
}

// return false if `extendedType` and `instCustomType` are not matched
static bool CollectReplaceTableForExtendedType(Type& extendedType, Type& instCustomType,
    std::unordered_map<const GenericType*, Type*>& replaceTable)
{
    auto instTypeArgs = instCustomType.GetTypeArgs();
    auto genericTypeArgs = extendedType.GetTypeArgs();
    if (instTypeArgs.size() != genericTypeArgs.size()) {
        return false;
    }
    for (size_t i = 0; i < genericTypeArgs.size(); ++i) {
        if (auto genericTy = Cangjie::DynamicCast<GenericType*>(genericTypeArgs[i])) {
            auto res = replaceTable.emplace(genericTy, instTypeArgs[i]);
            // if generic type has been stored, but instantiated types are different, this extend def isn't expected
            if (!res.second && res.first->second != instTypeArgs[i]) {
#ifdef NDEBUG
                return false;
#else
                CJC_ABORT();
#endif
            }
        } else {
            auto genericArgTy = Cangjie::DynamicCast<CustomType*>(genericTypeArgs[i]);
            auto instArgTy = Cangjie::DynamicCast<CustomType*>(instTypeArgs[i]);
            if (genericArgTy != nullptr && instArgTy == nullptr) {
                return false;
            }
            if (genericArgTy == nullptr && instArgTy != nullptr) {
                return false;
            }
            if (genericArgTy != nullptr && instArgTy != nullptr &&
                genericArgTy->GetCustomTypeDef() != instArgTy->GetCustomTypeDef()) {
                return false;
            }
            auto match = CollectReplaceTableForExtendedType(*genericTypeArgs[i], *instTypeArgs[i], replaceTable);
            if (!match) {
                return false;
            }
        }
    }
    return true;
}

std::pair<FuncBase*, bool> CustomType::GetExpectedFunc(const std::string& funcName, FuncType& funcType, bool isStatic,
    std::vector<Type*>& funcInstTypeArgs, CHIR::CHIRBuilder& builder, bool checkAbstractMethod)
{
    std::unordered_map<const GenericType*, Type*> replaceTable;
    auto classInstArgs = this->GetTypeArgs();
    auto classGenericArgs = this->GetCustomTypeDef()->GetGenericTypeParams();
    if (!classGenericArgs.empty()) {
        CJC_ASSERT(classInstArgs.size() == classGenericArgs.size());
    }
    for (size_t i = 0; i < classGenericArgs.size(); ++i) {
        replaceTable.emplace(classGenericArgs[i], classInstArgs[i]);
    }

    // visiting func must in order:
    // 1. func declared in current def and extend def
    // 2. func declared in super class def
    // 3. func declared in super interface, including extend def's super interface

    // current def
    
    if (auto [func, done] = GetCustomTypeDef()->GetExpectedFunc(
        funcName, funcType, isStatic, replaceTable, funcInstTypeArgs, builder, checkAbstractMethod); done) {
        return {func, done};
    }

    // extend def
    for (auto ex : GetCustomTypeDef()->GetExtends()) {
        auto match = CollectReplaceTableForExtendedType(*ex->GetExtendedType(), *this, replaceTable);
        if (!match) {
            continue;
        }
        auto [func, done] = ex->GetExpectedFunc(
            funcName, funcType, isStatic, replaceTable, funcInstTypeArgs, builder, checkAbstractMethod);
        if (done) {
            return {func, done};
        }
    }

    // super class
    if (auto classTy = DynamicCast<ClassType*>(this); classTy &&
        classTy->GetClassDef()->GetSuperClassDef() != nullptr) {
        auto superClassTy = classTy->GetSuperClassTy(&builder);
        auto [func, done] = superClassTy->GetExpectedFunc(
            funcName, funcType, isStatic, funcInstTypeArgs, builder, checkAbstractMethod);
        if (done) {
            return {func, done};
        }
    }

    // super interface
    for (auto superInterfaceTy : this->GetImplementedInterfaceTys(&builder)) {
        auto [func, done] = superInterfaceTy->GetExpectedFunc(
            funcName, funcType, isStatic, funcInstTypeArgs, builder, checkAbstractMethod);
        if (done) {
            return {func, done};
        }
    }

    // extend def's super interface
    for (auto ex : GetCustomTypeDef()->GetExtends()) {
        for (auto ty : ex->GetImplementedInterfaceTys()) {
            auto superInterfaceTy = ReplaceRawGenericArgType(*ty, replaceTable, builder);
            auto [func, done] = StaticCast<ClassType*>(superInterfaceTy)->GetExpectedFunc(
                funcName, funcType, isStatic, funcInstTypeArgs, builder, checkAbstractMethod);
            if (done) {
                return {func, done};
            }
        }
    }
    return {nullptr, false};
}

Type* CustomType::GetExactParentType(
    const std::string& funcName, FuncType& funcType, bool isStatic,
    std::vector<Type*>& funcInstTypeArgs, CHIRBuilder& builder, bool checkAbstractMethod)
{
    std::unordered_map<const GenericType*, Type*> replaceTable;
    auto classInstArgs = this->GetTypeArgs();
    auto classGenericArgs = this->GetCustomTypeDef()->GetGenericTypeParams();
    if (!classGenericArgs.empty()) {
        CJC_ASSERT(classInstArgs.size() == classGenericArgs.size());
    }
    for (size_t i = 0; i < classGenericArgs.size(); ++i) {
        replaceTable.emplace(classGenericArgs[i], classInstArgs[i]);
    }

    // visiting func must in order:
    // 1. func declared in current def and extend def
    // 2. func declared in super class def
    // 3. func declared in super interface, including extend def's super interface
    
    // current def
    // when it's an abstract method, `func` is nullptr
    if (auto [func, done] = GetCustomTypeDef()->GetExpectedFunc(
        funcName, funcType, isStatic, replaceTable, funcInstTypeArgs, builder, checkAbstractMethod);
        done && (func == nullptr || func->Get<WrappedRawMethod>() == nullptr)) {
        return this;
    }

    // extend def
    for (auto ex : GetCustomTypeDef()->GetExtends()) {
        auto match = CollectReplaceTableForExtendedType(*ex->GetExtendedType(), *this, replaceTable);
        if (!match) {
            continue;
        }
        auto [func, done] = ex->GetExpectedFunc(
            funcName, funcType, isStatic, replaceTable, funcInstTypeArgs, builder, checkAbstractMethod);
        // when it's an abstract method, `func` is nullptr
        if (done && (func == nullptr || func->Get<WrappedRawMethod>() == nullptr)) {
            return ReplaceRawGenericArgType(*ex->GetExtendedType(), replaceTable, builder);
        }
    }

    // super class
    if (auto classTy = DynamicCast<ClassType*>(this); classTy &&
        classTy->GetClassDef()->GetSuperClassDef() != nullptr) {
        Type* superClassTy = classTy->GetClassDef()->GetSuperClassTy();
        superClassTy = ReplaceRawGenericArgType(*superClassTy, replaceTable, builder);
        auto exactParentType = StaticCast<ClassType*>(superClassTy)->GetExactParentType(
            funcName, funcType, isStatic, funcInstTypeArgs, builder, checkAbstractMethod);
        if (exactParentType != nullptr) {
            return exactParentType;
        }
    }

    // super interface
    for (auto ty : GetCustomTypeDef()->GetImplementedInterfaceTys()) {
        auto superInterfaceTy = ReplaceRawGenericArgType(*ty, replaceTable, builder);
        auto exactParentType = StaticCast<ClassType*>(superInterfaceTy)->GetExactParentType(
            funcName, funcType, isStatic, funcInstTypeArgs, builder, checkAbstractMethod);
        if (exactParentType != nullptr) {
            return exactParentType;
        }
    }

    // extend def's super interface
    for (auto ex : GetCustomTypeDef()->GetExtends()) {
        for (auto ty : ex->GetImplementedInterfaceTys()) {
            auto superInterfaceTy = ReplaceRawGenericArgType(*ty, replaceTable, builder);
            auto result = StaticCast<ClassType*>(superInterfaceTy)->GetExactParentType(
                funcName, funcType, isStatic, funcInstTypeArgs, builder, checkAbstractMethod);
            if (result != nullptr) {
                return result;
            }
        }
    }

    return nullptr;
}

std::vector<VTableSearchRes> CustomType::GetFuncIndexInVTable(
    const FuncCallType& funcCallType, bool isStatic, CHIRBuilder& builder) const
{
    std::unordered_map<const GenericType*, Type*> replaceTable;
    auto classInstArgs = this->GetTypeArgs();
    auto classGenericArgs = this->GetCustomTypeDef()->GetGenericTypeParams();
    if (!classGenericArgs.empty()) {
        CJC_ASSERT(classInstArgs.size() == classGenericArgs.size());
    }
    for (size_t i = 0; i < classGenericArgs.size(); ++i) {
        replaceTable.emplace(classGenericArgs[i], classInstArgs[i]);
    }

    auto result = GetCustomTypeDef()->GetFuncIndexInVTable(funcCallType, isStatic, replaceTable, builder);
    if (!result.empty()) {
        return result;
    }

    for (auto ex : GetCustomTypeDef()->GetExtends()) {
        replaceTable.clear();
        auto extendedTyGenericArgs = ex->GetExtendedType()->GetTypeArgs();
        CJC_ASSERT(classInstArgs.size() == extendedTyGenericArgs.size());
        for (size_t i = 0; i < classInstArgs.size(); ++i) {
            CollectGenericReplaceTable(*extendedTyGenericArgs[i], *classInstArgs[i], replaceTable);
        }
        result = ex->GetFuncIndexInVTable(funcCallType, isStatic, replaceTable, builder);
        if (!result.empty()) {
            return result;
        }
    }
    return result;
}

std::vector<ClassType*> CustomType::CalculateExtendImplementedInterfaceTys(CHIRBuilder& builder,
    std::set<std::pair<const Type*, const Type*>>* visited) const
{
    std::vector<ClassType*> allParents;
    for (auto extendDef : def->GetExtends()) {
        if (!IsEqualOrInstantiatedTypeOf(*extendDef->GetExtendedType(), builder, visited)) {
            continue;
        }
        auto res = extendDef->GetExtendedType()->CalculateGenericTyMapping(*this);
        CJC_ASSERT(res.first);
        for (auto p : extendDef->GetImplementedInterfaceTys()) {
            auto instParent = StaticCast<ClassType*>(ReplaceRawGenericArgType(*p, res.second, builder));
            allParents.emplace_back(instParent);
        }
    }
    return allParents;
}

std::vector<ClassType*> CustomType::CalculateImplementedInterfaceTys(CHIRBuilder& builder,
    std::set<std::pair<const Type*, const Type*>>* visited)
{
    auto genericTypes = def->GetGenericTypeParams();
    if (!genericTypes.empty()) {
        CJC_ASSERT(argTys.size() == genericTypes.size());
    }
    auto instSuperInterfaceTys = CalculateExtendImplementedInterfaceTys(builder, visited);
    std::unordered_map<const GenericType*, Type*> replaceTable;
    for (size_t i = 0; i < genericTypes.size(); ++i) {
        replaceTable.emplace(genericTypes[i], argTys[i]);
    }
    for (auto ty : def->GetImplementedInterfaceTys()) {
        instSuperInterfaceTys.emplace_back(
            StaticCast<ClassType*>(ReplaceRawGenericArgType(*ty, replaceTable, builder)));
    }
    return instSuperInterfaceTys;
}

std::vector<ClassType*> CustomType::GetImplementedInterfaceTys(CHIRBuilder* builder,
    std::set<std::pair<const Type*, const Type*>>* visited)
{
    std::unique_lock<std::recursive_mutex> lock(setSuperInterfaceMtx);
    if (hasSetSuperInterface) {
        return implementedInterfaceTys;
    } else {
        CJC_NULLPTR_CHECK(builder);
        implementedInterfaceTys = CalculateImplementedInterfaceTys(*builder, visited);
        hasSetSuperInterface = true;
        return implementedInterfaceTys;
    }
}

std::vector<ClassType*> CustomType::GetImplementedInterfaceTysWithoutExtend(CHIRBuilder& builder)
{
    auto genericTypes = def->GetGenericTypeParams();
    if (!genericTypes.empty()) {
        CJC_ASSERT(argTys.size() == genericTypes.size());
    }
    std::vector<ClassType*> instSuperInterfaceTys;
    std::unordered_map<const GenericType*, Type*> replaceTable;
    for (size_t i = 0; i < genericTypes.size(); ++i) {
        replaceTable.emplace(genericTypes[i], argTys[i]);
    }
    for (auto ty : def->GetImplementedInterfaceTys()) {
        instSuperInterfaceTys.emplace_back(
            StaticCast<ClassType*>(ReplaceRawGenericArgType(*ty, replaceTable, builder)));
    }
    return instSuperInterfaceTys;
}

StructType::StructType(StructDef* structDef, const std::vector<Type*>& genericArgs)
    : CustomType(TypeKind::TYPE_STRUCT, structDef, genericArgs)
{
}

StructDef* StructType::GetStructDef() const
{
    CJC_ASSERT(def != nullptr);
    return StaticCast<StructDef*>(def);
}

std::string StructType::ToString() const
{
    std::stringstream ss;
    ss << "Struct";
    if (def != nullptr) {
        ss << "-" << def->GetIdentifierWithoutPrefix();
    }
    ss << PrintArgTys(argTys);
    return ss.str();
}

EnumType::EnumType(EnumDef* enumDef, const std::vector<Type*>& genericArgs)
    : CustomType(TypeKind::TYPE_ENUM, enumDef, genericArgs)
{
}

bool EnumType::IsOption() const
{
    return IsCoreOption(*GetEnumDef());
}

EnumDef* EnumType::GetEnumDef() const
{
    CJC_ASSERT(def != nullptr);
    return StaticCast<EnumDef*>(def);
}

std::vector<EnumCtorInfo> EnumType::GetConstructorInfos(CHIRBuilder& builder) const
{
    auto ctors = GetEnumDef()->GetCtors();
    auto genericTypeParams = GetEnumDef()->GetGenericTypeParams();
    CJC_ASSERT(genericTypeParams.size() == argTys.size());
    std::unordered_map<const GenericType*, Type*> replaceTable;
    for (size_t i = 0; i < genericTypeParams.size(); ++i) {
        replaceTable.emplace(genericTypeParams[i], argTys[i]);
    }
    for (auto& ctor : ctors) {
        ctor.funcType = StaticCast<FuncType*>(ReplaceRawGenericArgType(*ctor.funcType, replaceTable, builder));
    }
    return ctors;
}

std::string EnumType::ToString() const
{
    std::stringstream ss;
    ss << "Enum";
    if (def != nullptr) {
        ss << "-" << def->GetIdentifierWithoutPrefix();
    }
    ss << PrintArgTys(argTys);
    return ss.str();
}

bool EnumType::CheckIsBoxed(
    const EnumType& original, Type& curType, CHIRBuilder& builder, bool doCheck, std::unordered_set<Type*>& visited)
{
    if (doCheck && &curType == &original) {
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
                if (CheckIsBoxed(original, *ty, builder, true, visited)) {
                    return true;
                }
            }
        }
    } else if (curType.IsStruct()) {
        auto& structType = StaticCast<StructType&>(curType);
        for (auto ty : structType.GetInstantiatedMemberTys(builder)) {
            if (CheckIsBoxed(original, *ty, builder, true, visited)) {
                return true;
            }
        }
    } else if (curType.IsTuple()) {
        auto& tupleType = StaticCast<TupleType&>(curType);
        for (auto ty : tupleType.GetElementTypes()) {
            if (CheckIsBoxed(original, *ty, builder, true, visited)) {
                return true;
            }
        }
    }
    return false;
}

bool EnumType::IsBoxed(CHIRBuilder& builder)
{
    std::unordered_set<Type*> visited;
    return CheckIsBoxed(*this, *this, builder, false, visited);
}

std::string TupleType::ToString() const
{
    std::stringstream ss;
    ss << "Tuple";
    ss << PrintArgTys(argTys);
    return ss.str();
}

std::string RawArrayType::ToString() const
{
    CJC_ASSERT(!argTys.empty());
    auto elemTy = argTys[0];
    CJC_NULLPTR_CHECK(elemTy);
    std::stringstream ss;
    std::string prefix;
    std::string suffix;
    for (unsigned int i = 0; i < dims; i++) {
        prefix += "RawArray<";
        suffix += ">";
    }
    ss << prefix;
    ss << elemTy->ToString();
    ss << suffix;
    return ss.str();
}

size_t RawArrayType::Hash() const
{
    std::size_t hashVal = HashArgTypes(argTys);
    HashValue(hashVal, dims);
    HashValue(hashVal, kind);
    return hashVal;
}

bool RawArrayType::operator==(const Type& other) const
{
    auto it = DynamicCast<const RawArrayType*>(&other);
    return it && argTys == it->argTys && (dims == it->dims);
}

std::string VArrayType::ToString() const
{
    std::stringstream ss;
    ss << "VArray<" << argTys[0]->ToString() << ", $" << std::to_string(size) << ">";
    return ss.str();
}

size_t VArrayType::Hash() const
{
    std::size_t hashVal = HashArgTypes(argTys);
    HashValue(hashVal, size);
    HashValue(hashVal, kind);
    return hashVal;
}

bool VArrayType::operator==(const Type& other) const
{
    auto it = DynamicCast<const VArrayType*>(&other);
    return it && argTys == it->argTys && (size == it->size);
}

const std::vector<ExtendDef*>& CPointerType::GetExtends(CHIRBuilder* builder) const
{
    if (!GetElementType()->IsUnit()) {
        CJC_NULLPTR_CHECK(builder);
        return builder->GetType<CPointerType>(builder->GetUnitTy())->GetExtends(builder);
    } else {
        return extends;
    }
}

std::string CPointerType::ToString() const
{
    std::stringstream ss;
    ss << "CPointer";
    ss << PrintArgTys(argTys);
    return ss.str();
}

std::string RefType::ToString() const
{
    std::stringstream ss;
    ss << argTys[0]->ToString() << "&";
    return ss.str();
}

std::string BoxType::ToString() const
{
    std::stringstream ss;
    ss << "Box<" << argTys[0]->ToString() << ">";
    return ss.str();
}

std::string ThisType::ToString() const
{
    return "This";
}

void GenericType::SetUpperBounds(const std::vector<Type*>& args)
{
    for (auto arg : args) {
        if (!orphanFlag) {
            CJC_ASSERT(!arg->StripAllRefs()->IsValueType() && "Generic type upper bound should NOT be value type!");
        }
    }
    upperBounds = args;
    std::sort(upperBounds.begin(), upperBounds.end(), [](auto l, auto r) {
        return l->ToString() < r->ToString();
    });
}

void GenericType::GetInstMap(std::unordered_map<const GenericType*, Type*>& instMap, CHIRBuilder& builder) const
{
    for (auto upperBound : upperBounds) {
        auto upperBoundCustomType = StaticCast<CustomType*>(upperBound->StripAllRefs());
        upperBoundCustomType->GetInstMap(instMap, builder);
    }
}

std::pair<Type*, bool> GenericType::GetInstMemberTypeByPathCheckingReadOnly(
    const std::vector<uint64_t>& path, CHIRBuilder& builder) const
{
    // Find the most child class type
    Type* concretType = nullptr;
    for (auto upperBound : upperBounds) {
        auto upperBoundCustomType = StaticCast<CustomType*>(upperBound->StripAllRefs());
        if (upperBoundCustomType->GetCustomTypeDef()->IsClassLike()) {
            if (concretType == nullptr) {
                concretType = upperBound;
            } else {
                if (upperBoundCustomType->IsEqualOrSubTypeOf(*concretType->StripAllRefs(), builder)) {
                    concretType = upperBound;
                }
            }
        }
    }
    CJC_NULLPTR_CHECK(concretType);
    return StaticCast<CustomType*>(
        concretType->StripAllRefs())->GetInstMemberTypeByPathCheckingReadOnly(path, builder);
}

size_t GenericType::Hash() const
{
    std::size_t hashVal{0};
    HashValue(hashVal, identifier);
    return hashVal;
}

bool GenericType::operator==(const Type& other) const
{
    auto it = DynamicCast<const GenericType*>(&other);
    return it && identifier == it->identifier;
}

std::string GenericType::ToString() const
{
    std::stringstream ss;
    if (orphanFlag) {
        ss << "(orphan)" << upperBounds[0]->ToString();
    } else {
        ss << "Generic-" << identifier;
    }
    return ss.str();
}

namespace Cangjie::CHIR {
Type* GetFieldOfType(Type& baseTy, uint64_t index, CHIRBuilder& builder)
{
    Type* type = nullptr;
    if (baseTy.IsRef()) {
        type = GetFieldOfType(*StaticCast<RefType&>(baseTy).GetBaseType(), index, builder);
    } else if (baseTy.IsTuple()) {
        auto& tupleTy = StaticCast<TupleType&>(baseTy);
        if (index < tupleTy.GetElementTypes().size()) {
            type = tupleTy.GetElementType(index);
        }
    } else if (baseTy.IsStruct()) {
        auto& structTy = StaticCast<StructType&>(baseTy);
        auto memberTys = structTy.GetInstantiatedMemberTys(builder);
        if (index < memberTys.size()) {
            type = memberTys[index];
        }
    } else if (baseTy.IsClass()) {
        auto& classTy = StaticCast<ClassType&>(baseTy);
        auto memberTys = classTy.GetInstantiatedMemberTys(builder);
        if (index < memberTys.size()) {
            type = memberTys[index];
        }
    } else if (baseTy.IsEnum()) {
        if (index == 0) {
            if (GetSelectorType(*StaticCast<EnumType&>(baseTy).GetEnumDef()) == Type::TypeKind::TYPE_BOOLEAN) {
                type = builder.GetBoolTy();
            } else {
                type = builder.GetUInt32Ty();
            }
        }
    } else if (baseTy.IsRawArray()) {
        type = StaticCast<RawArrayType&>(baseTy).GetElementType();
    }
    return type;
}

static bool CalculateGenericTyMappingImpl(
    const Type& instType, const Type& genericType, std::unordered_map<const GenericType*, Type*>& replaceTable)
{
    if (genericType.IsGeneric()) {
        auto& gType = StaticCast<const GenericType&>(genericType);
        auto it = replaceTable.find(&gType);
        if (it != replaceTable.end()) {
            return it->second == &instType;
        } else {
            replaceTable.emplace(&gType, const_cast<Type*>(&instType));
            return true;
        }
    }
    if (&instType == &genericType) {
        return true;
    }
    if (instType.GetTypeKind() != genericType.GetTypeKind()) {
        return false;
    }
    if (auto instCustomType = Cangjie::DynamicCast<const CustomType*>(&instType)) {
        if (instCustomType->GetCustomTypeDef() !=
            Cangjie::StaticCast<const CustomType&>(genericType).GetCustomTypeDef()) {
            return false;
        }
    }
    auto instTypeArgs = instType.GetTypeArgs();
    auto genericTypeArgs = genericType.GetTypeArgs();
    if (instTypeArgs.size() != genericTypeArgs.size()) {
        return false;
    }
    for (size_t i = 0; i < instTypeArgs.size(); ++i) {
        if (!CalculateGenericTyMappingImpl(*instTypeArgs[i], *genericTypeArgs[i], replaceTable)) {
            return false;
        }
    }
    return true;
}

std::pair<bool, std::unordered_map<const GenericType*, Type*>> Type::CalculateGenericTyMapping(
    const Type& targetTy) const
{
    std::unordered_map<const GenericType*, Type*> replaceTable;
    auto res = CalculateGenericTyMappingImpl(targetTy, *this, replaceTable);
    return {res, replaceTable};
}

void Type::VisitTypeRecursively(const std::function<bool(const Type&)>& visitor) const
{
    if (visitor(*this)) {
        for (auto ty : argTys) {
            ty->VisitTypeRecursively(visitor);
        }
    }
}

std::vector<ClassType*> BuiltinType::GetSuperTypesRecusively(CHIRBuilder& builder,
    std::set<std::pair<const Type*, const Type*>>* visited)
{
    std::vector<ClassType*> inheritanceList;
    for (auto extendDef : GetExtends(&builder)) {
        if (!IsEqualOrInstantiatedTypeOf(*extendDef->GetExtendedType(), builder, visited)) {
            continue;
        }
        for (auto interface : extendDef->GetImplementedInterfaceTys()) {
            GetAllInstantiatedParentType(*interface, builder, inheritanceList, visited);
        }
    }
    return inheritanceList;
}

const std::vector<ExtendDef*>& BuiltinType::GetExtends([[maybe_unused]]CHIRBuilder* builder) const
{
    return extends;
}

void BuiltinType::AddExtend(ExtendDef& extend)
{
    extends.emplace_back(&extend);
}

std::vector<FuncBase*> BuiltinType::GetExtendMethods() const
{
    std::vector<FuncBase*> methods;
    for (auto def : extends) {
        auto funcs = def->GetMethods();
        methods.insert(methods.end(), funcs.begin(), funcs.end());
    }
    return methods;
}

std::vector<FuncBase*> BuiltinType::GetDeclareAndExtendMethods([[maybe_unused]] CHIRBuilder& builder) const
{
    return GetExtendMethods();
}

static bool TupleTypeIsEqualOrSub(const TupleType& subType, const TupleType& parentType, CHIRBuilder& builder)
{
    auto thisTupleArgs = subType.GetElementTypes();
    auto parentTupleArgs = parentType.GetElementTypes();
    if (thisTupleArgs.size() != parentTupleArgs.size()) {
        return false;
    }
    for (size_t i = 0; i < thisTupleArgs.size(); ++i) {
        if (!thisTupleArgs[i]->IsEqualOrSubTypeOf(*parentTupleArgs[i], builder)) {
            return false;
        }
    }
    return true;
}

static bool FuncTypeIsEqualOrSub(const FuncType& subType, const FuncType& parentType, CHIRBuilder& builder)
{
    if (subType.IsCFunc() != parentType.IsCFunc()) {
        return false;
    }
    if (subType.HasVarArg() != parentType.HasVarArg()) {
        return false;
    }
    auto thisParamTypes = subType.GetParamTypes();
    auto parentParamTypes = parentType.GetParamTypes();
    if (thisParamTypes.size() != parentParamTypes.size()) {
        return false;
    }
    for (size_t i = 0; i < thisParamTypes.size(); ++i) {
        if (!parentParamTypes[i]->IsEqualOrSubTypeOf(*thisParamTypes[i], builder)) {
            return false;
        }
    }
    return subType.GetReturnType()->IsEqualOrSubTypeOf(*parentType.GetReturnType(), builder);
}

static const ClassType* GetAutoEnvGenericBase(ClassType& type, CHIRBuilder& builder)
{
    CJC_ASSERT(type.IsAutoEnv());
    if (type.IsAutoEnvGenericBase()) {
        return &type;
    }
    return GetAutoEnvGenericBase(*type.GetSuperClassTy(&builder), builder);
}

static FuncType* ConvertClosureTypeToFuncType(ClassType& closureType, CHIRBuilder& builder)
{
    auto typeArgs = GetAutoEnvGenericBase(closureType, builder)->GetGenericArgs();
    CJC_ASSERT(!typeArgs.empty());
    auto retType = typeArgs.back();
    typeArgs.pop_back();
    return builder.GetType<FuncType>(typeArgs, retType);
}

static bool ClosureTypeIsEqualOrSub(ClassType& subType, ClassType& parentType, CHIRBuilder& builder)
{
    auto subFuncType = ConvertClosureTypeToFuncType(subType, builder);
    auto parentFuncType = ConvertClosureTypeToFuncType(parentType, builder);
    return FuncTypeIsEqualOrSub(*subFuncType, *parentFuncType, builder);
}

bool Type::SatisfyCType() const
{
    // same logical with `AST::Ty::IsMetCType`
    auto builtinCType = (kind >= TYPE_INT8 && kind <= TYPE_UNIT) || kind == TYPE_CPOINTER || kind == TYPE_CSTRING;
    auto isCStruct = (kind == TYPE_STRUCT) && StaticCast<const StructType*>(this)->GetStructDef()->IsCStruct();
    auto varrayMeetCondition =
        (kind == TYPE_VARRAY) && StaticCast<const VArrayType*>(this)->GetElementType()->SatisfyCType();
    return builtinCType || isCStruct || IsCFunc() || varrayMeetCondition;
}

bool Type::IsEqualOrSubTypeOf(const Type& parentType, CHIRBuilder& builder,
    std::set<std::pair<const Type*, const Type*>>* visited) const
{
    /**
     *  there may be an unresolvable inheritance relationship:
     *  interface I<T> {}
     *  abstract class C<T> {}
     *  extend<T> C<T> <: I<T> where T <: I<T> {}
     *  class D <: C<D> {}
     *  main() {0}
     *
     *  if you want to know if class D is sub type of I<D>, that will be a disaster, because
     *  parent type of class D is C<D>, and in `extend<T> C<T> <: I<T>`, interface I is class C's parent type,
     *  so you only need to know if `C<D> <: I<D>` satisfy generic constraints which `T <: I<T>`, then you need to
     *  compute if the `T` can be class D, that means you have to know if class D is sub type of I<D>, tail bites head.
     *  so we need to check if the sub type and parent type have been visited, if visited, we don't think they have a
     *  parent-child relationship and Sema does the same
     */
    std::set<std::pair<const Type*, const Type*>> empty;
    visited = visited ? visited : &empty;
    if (!visited->emplace(this, &parentType).second) {
        return false;
    }
    if (this->IsRef() && parentType.IsRef()) {
        auto subBaseType = StaticCast<const RefType*>(this)->GetBaseType();
        auto parentBaseType = StaticCast<const RefType&>(parentType).GetBaseType();
        return subBaseType->IsEqualOrSubTypeOf(*parentBaseType, builder, visited);
    }
    auto thisDeref = this->StripAllRefs();
    auto parentDeref = parentType.StripAllRefs();
    // we can't think class-A and class-A& is equal
    if ((this->IsRef() || parentType.IsRef()) && (thisDeref == parentDeref)) {
        return false;
    }
    // Nothing type is all types' sub type, Any type is all types' parent type
    if (thisDeref == parentDeref || thisDeref->IsNothing() || parentDeref->IsAny()) {
        return true;
    }
    // invalid type shouldn't be compared, just return false
    // if parent type is generic type, that means it can be any type, even if it has upper bounds
    if (thisDeref->IsInvalid() || parentDeref->IsInvalid() || parentDeref->IsGeneric()) {
        return false;
    }
    if (parentDeref->IsCType()) {
        return this->SatisfyCType();
    }
    if (thisDeref->IsGeneric()) {
        for (auto upperBound : StaticCast<GenericType*>(thisDeref)->GetUpperBounds()) {
            if (!upperBound->StripAllRefs()->IsEqualOrSubTypeOf(*parentDeref, builder, visited)) {
                return false;
            }
        }
        return true;
    }
    if (thisDeref->IsTuple()) {
        if (!parentDeref->IsTuple()) {
            return false;
        }
        return TupleTypeIsEqualOrSub(
            *StaticCast<TupleType*>(thisDeref), *StaticCast<TupleType*>(parentDeref), builder);
    }
    if (thisDeref->IsFunc()) {
        if (!parentDeref->IsFunc()) {
            return false;
        }
        return FuncTypeIsEqualOrSub(
            *StaticCast<FuncType*>(thisDeref), *StaticCast<FuncType*>(parentDeref), builder);
    }
    if (thisDeref->IsAutoEnv()) {
        if (!parentDeref->IsAutoEnv()) {
            return false;
        }
        return ClosureTypeIsEqualOrSub(
            *StaticCast<ClassType*>(thisDeref), *StaticCast<ClassType*>(parentDeref), builder);
    }
    std::vector<ClassType*> allParents;
    if (auto builtinType = DynamicCast<BuiltinType*>(thisDeref)) {
        allParents = builtinType->GetSuperTypesRecusively(builder, visited);
    } else if (auto customType = DynamicCast<CustomType*>(thisDeref)) {
        allParents = customType->GetSuperTypesRecusively(builder, visited);
    }
    for (auto pTy : allParents) {
        if (pTy == parentDeref) {
            return true;
        } else if (auto res = parentDeref->CalculateGenericTyMapping(*pTy); res.first) {
            // we need to remove this `if` after new ir checker merged
            return true;
        }
    }
    return false;
}

bool Type::IsEqualOrInstantiatedTypeOf(const Type& genericRelatedType, CHIRBuilder& builder,
    std::set<std::pair<const Type*, const Type*>>* visited) const
{
    if (this == &genericRelatedType) {
        return true;
    }
    auto res = genericRelatedType.CalculateGenericTyMapping(*this);
    if (!res.first) {
        return false;
    }
    for (auto& it : res.second) {
        if (!it.second->SatisfyGenericConstraints(*it.first, builder, res.second, visited)) {
            return false;
        }
    }
    return true;
}

bool Type::SatisfyGenericConstraints(const GenericType& type, CHIRBuilder& builder,
    const std::unordered_map<const GenericType*, Type*>& instMap,
    std::set<std::pair<const Type*, const Type*>>* visited) const
{
    for (auto upperBound : type.GetUpperBounds()) {
        if (!IsEqualOrSubTypeOf(*ReplaceRawGenericArgType(*upperBound, instMap, builder), builder, visited)) {
            return false;
        }
    }
    return true;
}
} // namespace Cangjie::CHIR
