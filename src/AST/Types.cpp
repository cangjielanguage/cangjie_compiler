// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Type related classes.
 */

#include "cangjie/AST/Types.h"

#include <string>
#include <unordered_set>

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/Utils.h"

namespace Cangjie::AST {
namespace {

const std::unordered_map<TypeKind, std::string> TYPEKIND_TO_STRING_MAP{
#define TYPE_KIND(KIND, TYPE, NAME) {TypeKind::KIND, NAME},
#include "cangjie/AST/TypeKind.inc"
#undef TYPE_KIND
};

template <typename T1, typename T2> T1 GetGenericTy(T2 decl)
{
    return decl && decl->generic ? static_cast<T1>(decl->GetTy().get()) : nullptr;
}

template <typename TypeDeclT> size_t HashNominalTy(const TypeDeclT& ty)
{
    size_t ret = 0;
    ret = hash_combine<std::string>(ret, ty.name);
    for (auto typeArg : ty.typeArgs) {
        ret = hash_combine<ModalTy>(ret, typeArg);
    }
    ret = hash_combine<Ptr<const Decl>>(ret, ty.declPtr);
    return ret;
}

bool IsJavaGeneric(const GenericParamDecl& gpd)
{
    if (gpd.outerDecl && HasJavaAttr(*gpd.outerDecl)) {
        return true;
    }
    if (auto funcDecl = DynamicCast<FuncDecl*>(gpd.outerDecl); funcDecl && funcDecl->funcBody &&
        funcDecl->funcBody->parentClassLike && HasJavaAttr(*funcDecl->funcBody->parentClassLike)) {
        return true;
    }
    return false;
}

bool TyArgsEqual(const std::vector<ModalTy>& lhs, const std::vector<ModalTy>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (!(lhs[i] == rhs[i])) {
            return false;
        }
    }
    return true;
}
} // namespace

size_t Ty::Hash() const
{
    size_t ret = 0;
    ret = hash_combine<Ptr<const Ty>>(ret, this);
    return ret;
}

size_t ArrayTy::Hash() const
{
    size_t ret = 0;
    ret = hash_combine<ModalTy>(ret, typeArgs.empty() ? ModalTy{} : typeArgs[0]);
    ret = hash_combine<size_t>(ret, dims);
    ret = hash_combine<TypeKind>(ret, TypeKind::TYPE_ARRAY);
    return ret;
}

size_t VArrayTy::Hash() const
{
    size_t ret = 0;
    ret = hash_combine<ModalTy>(ret, typeArgs.empty() ? ModalTy{} : typeArgs[0]);
    ret = hash_combine<int64_t>(ret, size);
    ret = hash_combine<TypeKind>(ret, TypeKind::TYPE_VARRAY);
    return ret;
}

size_t PointerTy::Hash() const
{
    size_t ret = 0;
    ret = hash_combine<ModalTy>(ret, typeArgs.empty() ? ModalTy{} : typeArgs[0]);
    ret = hash_combine<TypeKind>(ret, TypeKind::TYPE_POINTER);
    return ret;
}

size_t TupleTy::Hash() const
{
    size_t ret = 0;
    ret = hash_combine<bool>(ret, isClosureTy);
    for (auto typeArg : typeArgs) {
        ret = hash_combine<ModalTy>(ret, typeArg);
    }
    ret = hash_combine<TypeKind>(ret, TypeKind::TYPE_TUPLE);
    return ret;
}

size_t FuncTy::Hash() const
{
    size_t ret = 0;
    ret = hash_combine<ModalTy>(ret, retTy);
    for (auto paramTy : paramTys) {
        ret = hash_combine<ModalTy>(ret, paramTy);
    }
    ret = hash_combine<bool>(ret, isClosureTy);
    ret = hash_combine<bool>(ret, isC);
    ret = hash_combine<bool>(ret, hasVariableLenArg);
    ret = hash_combine<bool>(ret, noCast);
    ret = hash_combine<TypeKind>(ret, TypeKind::TYPE_FUNC);
    return ret;
}

size_t UnionTy::Hash() const
{
    size_t ret = 0;
    for (auto ty : tys) {
        ret = hash_combine<Ptr<const Ty>>(ret, ty);
    }
    ret = hash_combine<TypeKind>(ret, TypeKind::TYPE_UNION);
    return ret;
}

size_t IntersectionTy::Hash() const
{
    size_t ret = 0;
    for (auto ty : tys) {
        ret = hash_combine<Ptr<const Ty>>(ret, ty);
    }
    ret = hash_combine<TypeKind>(ret, TypeKind::TYPE_INTERSECTION);
    return ret;
}

size_t GenericsTy::Hash() const
{
    size_t ret = 0;
    ret = hash_combine<Ptr<const GenericParamDecl>>(ret, decl);
    return ret;
}

std::string Ty::String() const
{
    return KindName(kind);
}

bool ModalTy::IsLocalType() const
{
    return modal.local != Mode::NOT;
}

std::string ModalTy::String() const
{
    if (!dataTy) {
        return Ty::ToString(nullptr);
    }
    if (auto funcTy = dynamic_cast<FuncTy*>(dataTy.get())) {
        auto modalString = modal.AsTypeSuffixString();
        return funcTy->isC || modalString.empty() ? funcTy->String()
            : "(" + funcTy->String() + ")" + modalString; // only add () when it's not CFunc and has mode
    }
    return dataTy->String() + modal.AsTypeSuffixString();
}

bool ModalTy::operator<(const ModalTy& other) const
{
    return CmpTyByNameModal{}(*this, other);
}

bool ModalTy::IsCorrect() const
{
    return Ty::IsTyCorrect(Ptr<const AST::Ty>(dataTy));
}

bool ModalTy::operator==(const ModalTy& other) const
{
    return dataTy == other.dataTy && modal == other.modal;
}

bool ModalTy::operator!=(const ModalTy& other) const
{
    return !(*this == other);
}

std::string Ty::KindName(TypeKind k)
{
    auto iter = TYPEKIND_TO_STRING_MAP.find(k);
    return iter == TYPEKIND_TO_STRING_MAP.end() ? "UnknownType" : iter->second;
}

InterfaceTy::InterfaceTy(const std::string& name, InterfaceDecl& id, const std::vector<DataTy>& typeArgs)
    : ClassLikeTy(TypeKind::TYPE_INTERFACE, id), declPtr(&id), decl(&id)
{
    this->name = name;
    for (auto ty : typeArgs) {
        this->typeArgs.emplace_back(ty);
    }
    this->invalid = !Ty::AreTysCorrect(Utils::VecToSet(this->typeArgs));
    this->generic = Ty::ExistGeneric(this->typeArgs);
}

ClassTy::ClassTy(const std::string& name, ClassDecl& cd, const std::vector<DataTy>& typeArgs)
    : ClassLikeTy(TypeKind::TYPE_CLASS, cd), declPtr(&cd), decl(&cd)
{
    this->name = name;
    for (auto ty : typeArgs) {
        this->typeArgs.emplace_back(ty);
    }
    this->invalid = !Ty::AreTysCorrect(Utils::VecToSet(this->typeArgs));
    this->generic = Ty::ExistGeneric(this->typeArgs);
}

EnumTy::EnumTy(const std::string& name, EnumDecl& ed, const std::vector<DataTy>& typeArgs)
    : Ty(TypeKind::TYPE_ENUM), declPtr(&ed), decl(&ed)
{
    this->name = name;
    for (auto ty : typeArgs) {
        this->typeArgs.emplace_back(ty);
    }
    this->invalid = !Ty::AreTysCorrect(Utils::VecToSet(this->typeArgs));
    this->generic = Ty::ExistGeneric(this->typeArgs);
}

StructTy::StructTy(const std::string& name, StructDecl& sd, const std::vector<DataTy>& typeArgs)
    : Ty(TypeKind::TYPE_STRUCT), declPtr(&sd), decl(&sd)
{
    this->name = name;
    for (auto ty : typeArgs) {
        this->typeArgs.emplace_back(ty);
    }
    this->invalid = !Ty::AreTysCorrect(Utils::VecToSet(this->typeArgs));
    this->generic = Ty::ExistGeneric(this->typeArgs);
}

Ptr<ClassTy> ClassTy::GetGenericTy() const
{
    return ::Cangjie::AST::GetGenericTy<ClassTy*>(declPtr);
}

Ptr<InterfaceTy> InterfaceTy::GetGenericTy() const
{
    return ::Cangjie::AST::GetGenericTy<InterfaceTy*>(declPtr);
}

Ptr<EnumTy> EnumTy::GetGenericTy() const
{
    return ::Cangjie::AST::GetGenericTy<EnumTy*>(declPtr);
}

Ptr<StructTy> StructTy::GetGenericTy() const
{
    return ::Cangjie::AST::GetGenericTy<StructTy*>(declPtr);
}

size_t ClassTy::Hash() const
{
    size_t hash = HashNominalTy(*this);
    return hash_combine<bool>(hash, false);
}

bool ClassTy::operator==(const Ty& other) const
{
    // Since ClassThisTy is subtype of ClassTy, ensure the other is not ClassThisTy.
    return NominalTyEqualTo(*this, other) && !Is<const ClassThisTy>(other);
}

size_t ClassThisTy::Hash() const
{
    size_t hash = HashNominalTy(*this);
    return hash_combine<bool>(hash, true);
}

size_t InterfaceTy::Hash() const
{
    size_t hash = HashNominalTy(*this);
    return hash;
}

size_t StructTy::Hash() const
{
    size_t hash = HashNominalTy(*this);
    return hash;
}

size_t EnumTy::Hash() const
{
    size_t hash = HashNominalTy(*this);
    return hash;
}

bool EnumTy::operator==(const Ty& other) const
{
    // Since RefEnumTy is subtype of EnumTy, ensure the other is not RefEnumTy.
    return NominalTyEqualTo(*this, other) && !Is<const RefEnumTy>(other);
}

bool EnumTy::IsNonExhaustive() const
{
    return decl && decl->hasEllipsis;
}

size_t RefEnumTy::Hash() const
{
    size_t hash = HashNominalTy(*this);
    return hash_combine<bool>(hash, true);
}

size_t TypeAliasTy::Hash() const
{
    size_t hash = HashNominalTy(*this);
    return hash;
}

bool Ty::IsInteger() const
{
    return IsSignedInteger() || IsUnsignedInteger() || kind == TypeKind::TYPE_IDEAL_INT;
}

bool Ty::IsIntegerSubType() const
{
    if (IsInteger()) {
        return true;
    }
    if (!IsGeneric()) {
        return false;
    }
    auto genericTy = RawStaticCast<const GenericsTy*>(this);
    return genericTy->lowerBound && genericTy->lowerBound->IsInteger();
}

bool Ty::IsSignedInteger() const
{
    return kind >= TypeKind::TYPE_INT8 && kind <= TypeKind::TYPE_INT_NATIVE;
}

bool Ty::IsUnsignedInteger() const
{
    return kind >= TypeKind::TYPE_UINT8 && kind <= TypeKind::TYPE_UINT_NATIVE;
}

bool Ty::IsFloating() const
{
    return (kind >= TypeKind::TYPE_FLOAT16 && kind <= TypeKind::TYPE_FLOAT64) || kind == TypeKind::TYPE_IDEAL_FLOAT;
}

bool Ty::IsFloatingSubType() const
{
    if (IsFloating()) {
        return true;
    }
    if (!IsGeneric()) {
        return false;
    }
    auto genericTy = RawStaticCast<const GenericsTy*>(this);
    return genericTy->lowerBound && genericTy->lowerBound->IsFloating();
}

bool Ty::IsBoolean() const
{
    return kind == TypeKind::TYPE_BOOLEAN;
}

bool Ty::IsBooleanSubType() const
{
    if (IsBoolean()) {
        return true;
    }
    if (!IsGeneric()) {
        return false;
    }
    auto genericTy = RawStaticCast<const GenericsTy*>(this);
    return genericTy->lowerBound && genericTy->lowerBound->IsBoolean();
}

bool Ty::IsRune() const
{
    return kind == TypeKind::TYPE_RUNE;
}

bool Ty::IsIdeal() const
{
    return kind == TypeKind::TYPE_IDEAL_INT || kind == TypeKind::TYPE_IDEAL_FLOAT;
}

bool Ty::IsInvalid() const
{
    return kind == TypeKind::TYPE_INVALID || kind == TypeKind::TYPE_INITIAL;
}

bool Ty::IsUnit() const
{
    return kind == TypeKind::TYPE_UNIT;
}

bool Ty::IsUnitOrNothing() const
{
    return kind == TypeKind::TYPE_UNIT || kind == TypeKind::TYPE_NOTHING;
}

bool Ty::IsQuest() const
{
    return kind == TypeKind::TYPE_QUEST;
}

bool Ty::IsPrimitive() const
{
    return kind >= TypeKind::TYPE_UNIT && kind <= TypeKind::TYPE_BOOLEAN;
}

bool Ty::IsPrimitiveSubType() const
{
    if (IsPrimitive()) {
        return true;
    }
    if (!IsGeneric()) {
        return false;
    }
    auto genericTy = RawStaticCast<const GenericsTy*>(this);
    return genericTy->lowerBound && genericTy->lowerBound->IsPrimitive();
}

bool Ty::IsGeneric() const
{
    return kind == TypeKind::TYPE_GENERICS;
}

bool Ty::IsPlaceholder() const
{
    if (kind == TypeKind::TYPE_GENERICS) {
        return RawStaticCast<const GenericsTy*>(this)->isPlaceholder;
    }
    return false;
}

bool Ty::IsArray() const
{
    return kind == TypeKind::TYPE_ARRAY;
}

bool Ty::IsStructArray() const
{
    if (kind == TypeKind::TYPE_STRUCT) {
        auto ty = RawStaticCast<const StructTy*>(this);
        if (ty->declPtr && ty->declPtr->fullPackageName == CORE_PACKAGE_NAME && ty->declPtr->identifier == "Array") {
            return true;
        }
    }
    return false;
}

bool Ty::IsPointer() const
{
    return kind == TypeKind::TYPE_POINTER;
}

bool Ty::IsCString() const
{
    return kind == TypeKind::TYPE_CSTRING;
}

bool Ty::IsStruct() const
{
    return kind == TypeKind::TYPE_STRUCT;
}

bool Ty::IsEraseGeneric() const
{
    if (IsGeneric()) {
        auto ty = RawStaticCast<const GenericsTy*>(this);
        return ty->isEraseMode;
    }
    return false;
}

bool Ty::IsClassLike() const
{
    return kind == TypeKind::TYPE_CLASS || kind == TypeKind::TYPE_INTERFACE;
}

bool Ty::IsClass() const
{
    return kind == TypeKind::TYPE_CLASS;
}

bool Ty::IsInterface() const
{
    return kind == TypeKind::TYPE_INTERFACE;
}

bool Ty::IsEnum() const
{
    return kind == TypeKind::TYPE_ENUM;
}

bool Ty::IsCoreOptionType() const
{
    // Core's enum Option check, support for auto package Option.
    if (!this->IsEnum()) {
        return false;
    }
    auto& enumTy = *RawStaticCast<const EnumTy*>(this);
    return enumTy.declPtr && enumTy.declPtr->fullPackageName == CORE_PACKAGE_NAME &&
        enumTy.declPtr->identifier == STD_LIB_OPTION && enumTy.typeArgs.size() == 1;
}

bool Ty::IsFunc() const
{
    return kind == TypeKind::TYPE_FUNC;
}

bool Ty::IsTuple() const
{
    return kind == TypeKind::TYPE_TUPLE;
}

bool Ty::IsClosureType() const
{
    if (kind != TypeKind::TYPE_TUPLE) {
        return false;
    }
    return RawStaticCast<const AST::TupleTy*>(this)->isClosureTy;
}

bool Ty::IsIntersection() const
{
    return kind == TypeKind::TYPE_INTERSECTION;
}

bool Ty::IsUnion() const
{
    return kind == TypeKind::TYPE_UNION;
}

bool Ty::IsNominal() const
{
    return kind == TypeKind::TYPE_CLASS || kind == TypeKind::TYPE_INTERFACE || kind == TypeKind::TYPE_STRUCT ||
        kind == TypeKind::TYPE_ENUM;
}

bool Ty::IsObject() const
{
    if (kind == TypeKind::TYPE_CLASS) {
        auto ty = RawStaticCast<const ClassTy*>(this);
        if (ty->declPtr->fullPackageName == CORE_PACKAGE_NAME && ty->declPtr->identifier == "Object") {
            return true;
        }
    }
    return false;
}

bool Ty::IsAny() const
{
    if (kind == TypeKind::TYPE_INTERFACE) {
        auto ty = RawStaticCast<const InterfaceTy*>(this);
        if (ty->declPtr->fullPackageName == CORE_PACKAGE_NAME && ty->declPtr->identifier == "Any") {
            return true;
        }
    }
    return kind == TypeKind::TYPE_ANY;
}

bool Ty::IsCType() const
{
    if (kind == TypeKind::TYPE_INTERFACE) {
        auto ty = RawStaticCast<const InterfaceTy*>(this);
        if (ty->declPtr->fullPackageName == CORE_PACKAGE_NAME && ty->declPtr->identifier == CTYPE_NAME) {
            return true;
        }
    }
    return false;
}

bool Ty::IsNothing() const
{
    return kind == TypeKind::TYPE_NOTHING;
}

bool Ty::IsString() const
{
    if (IsStruct()) {
        auto ty = RawStaticCast<const StructTy*>(this);
        if (ty->declPtr->fullPackageName == CORE_PACKAGE_NAME && ty->name == "String") {
            return true;
        }
    }
    return false;
}

bool Ty::IsRange() const
{
    if (IsStruct()) {
        auto ty = RawStaticCast<const StructTy*>(this);
        if (ty->declPtr->fullPackageName == CORE_PACKAGE_NAME && ty->name == "Range" && ty->typeArgs.size() == 1) {
            return true;
        }
    }
    return false;
}

bool Ty::Contains(DataTy ty) const
{
    if (this == ty) {
        return true;
    }

    if (auto ity = DynamicCast<const IntersectionTy*>(this); ity) {
        return Utils::In(ity->tys, [ty](auto it) { return it && it->Contains(ty); });
    } else if (auto uty = DynamicCast<const UnionTy*>(this); uty) {
        return Utils::In(uty->tys, [ty](auto it) { return it && it->Contains(ty); });
    } else {
        return Utils::In(typeArgs, [ty](auto it) { return it && it->Contains(ty); });
    }
}

bool Ty::HasIdealTy() const
{
    if (IsIdeal()) {
        return true;
    }
    for (auto it : this->typeArgs) {
        if (it && it->HasIdealTy()) {
            return true;
        }
    }
    return false;
}

bool Ty::HasIdealModal() const
{
    for (auto it : this->typeArgs) {
        if (it && (it.IsIdealModal() || it->HasIdealModal())) {
            return true;
        }
    }
    return false;
}

bool Ty::HasQuestTy() const
{
    if (IsQuest()) {
        return true;
    }
    for (auto it : this->typeArgs) {
        if (it && it->HasQuestTy()) {
            return true;
        }
    }
    return false;
}

bool Ty::HasAliasTy() const
{
    if (kind == TypeKind::TYPE) {
        return true;
    }
    for (auto it : this->typeArgs) {
        if (it && it->HasAliasTy()) {
            return true;
        }
    }
    return false;
}

// 'invalid' and 'generic' attribute is widely used during sema process,
// so deciding their value in constructor, not in checking runtime.
bool Ty::HasInvalidTy() const
{
    return invalid; // 'invalid' attribute is checked and set in constructor.
}

bool Ty::HasGeneric() const
{
    return generic; // 'generic' attribute is checked and set in constructor.
}

bool Ty::HasIntersectionTy() const
{
    if (IsIntersection()) {
        return true;
    }
    for (auto typeArg : typeArgs) {
        if (typeArg && typeArg->HasIntersectionTy()) {
            return true;
        }
    }
    return false;
}

bool Ty::HasPlaceholder() const
{
    if (auto genTy = DynamicCast<GenericsTy*>(this)) {
        return genTy->isPlaceholder;
    }
    for (auto typeArg : typeArgs) {
        if (typeArg && typeArg->HasPlaceholder()) {
            return true;
        }
    }
    return false;
}

bool Ty::IsExtendable() const
{
    if (kind == TypeKind::TYPE) {
        auto typeAliasDecl = RawStaticCast<const TypeAliasTy*>(this)->declPtr;
        return typeAliasDecl && typeAliasDecl->type && typeAliasDecl->type->GetTy() &&
            !typeAliasDecl->TestAttr(Attribute::IN_REFERENCE_CYCLE) && typeAliasDecl->type->GetTy()->IsExtendable();
    }
    return kind == TypeKind::TYPE_CLASS || kind == TypeKind::TYPE_ENUM || kind == TypeKind::TYPE_STRUCT || IsArray() ||
        IsPointer() || IsPrimitive() || IsCString();
}

bool Ty::IsNumeric() const
{
    return kind >= TypeKind::TYPE_INT8 && kind <= TypeKind::TYPE_IDEAL_FLOAT;
}

bool Ty::IsNative() const
{
    return kind == TypeKind::TYPE_INT_NATIVE || kind == TypeKind::TYPE_UINT_NATIVE;
}

bool Ty::IsBuiltin() const
{
    return (kind >= TypeKind ::TYPE_UNIT && kind <= TypeKind::TYPE_BOOLEAN) ||
        (kind >= TypeKind::TYPE_ARRAY && kind <= TypeKind::TYPE_CSTRING);
}

bool Ty::IsImmutableType() const
{
    return (kind >= TypeKind::TYPE_UNIT && kind <= TypeKind::TYPE_FUNC) || IsString() || IsRange();
}

std::string ArrayTy::String() const
{
    std::string str = typeArgs[0]->String();
    std::string prefix;
    std::string suffix;
    for (int i = 0; i < static_cast<int>(dims); i++) {
        prefix += "RawArray<";
        suffix += ">";
    }
    return prefix + str + suffix;
}

Ptr<ClassTy> ClassTy::GetSuperClassTy() const
{
    if (!decl) {
        return nullptr;
    }
    for (auto& types : decl->inheritedTypes) {
        if (types && types->GetTy() && types->TyKind() == TypeKind::TYPE_CLASS) {
            return RawStaticCast<ClassTy*>(types->DataTy());
        }
    }
    return nullptr;
}

std::set<Ptr<InterfaceTy>> ClassTy::GetSuperInterfaceTys() const
{
    if (!decl) {
        return {};
    }
    return decl->GetSuperInterfaceTys();
}

std::set<Ptr<InterfaceTy>> StructTy::GetSuperInterfaceTys() const
{
    if (!decl) {
        return {};
    }
    return decl->GetSuperInterfaceTys();
}

std::set<Ptr<InterfaceTy>> InterfaceTy::GetSuperInterfaceTys() const
{
    if (!decl) {
        return {};
    }
    return decl->GetSuperInterfaceTys();
}

std::set<Ptr<InterfaceTy>> EnumTy::GetSuperInterfaceTys() const
{
    if (!decl) {
        return {};
    }
    return decl->GetSuperInterfaceTys();
}

std::string PointerTy::String() const
{
    return "CPointer<" + typeArgs[0].String() + ">";
}

std::string TupleTy::String() const
{
    std::string str{""};
    if (isClosureTy) {
        str += "Closure<";
    } else {
        str += "Tuple<";
    }
    for (auto& typeArg : typeArgs) {
        if (&typeArg == &typeArgs.back()) {
            str += typeArg.String();
        } else {
            str += typeArg.String() + ", ";
        }
    }
    return str + ">";
}

std::string FuncTy::String() const
{
    std::string str{"("};
    for (auto& paramTy : paramTys) {
        if (&paramTy == &paramTys.back()) {
            str += paramTy.String();
        } else {
            str += paramTy.String() + ", ";
        }
    }
    str = str + ") -> " + retTy.String();
    return isC ? "CFunc<" + str + ">" : str;
}

template std::string Ty::GetModalTypesToStableStr(const std::unordered_set<ModalTy>& tys, const std::string& delimiter);
template std::string Ty::GetModalTypesToStableStr(const std::set<ModalTy>& tys, const std::string& delimiter);
template <typename Container> std::string Ty::GetTypesToStableStr(const Container& tys, const std::string& delimiter)
{
    std::set<DataTy, CmpTyByName> sortedTys;
    sortedTys.insert(tys.begin(), tys.end());
    return GetTypesToStr(sortedTys, delimiter);
}
template std::string Ty::GetTypesToStableStr(const std::unordered_set<DataTy>& tys, const std::string& delimiter);
template std::string Ty::GetTypesToStableStr(const std::set<DataTy>& tys, const std::string& delimiter);
template <typename Container>
std::string Ty::GetModalTypesToStableStr(const Container& tys, const std::string& delimiter)
{
    std::set<ModalTy, CmpTyByNameModal> sortedTys;
    sortedTys.insert(tys.begin(), tys.end());
    return GetModalTypesToStr(sortedTys, delimiter);
}

std::string IntersectionTy::String() const
{
    return GetModalTypesToStableStr(tys, " & ");
}

std::string UnionTy::String() const
{
    return GetTypesToStableStr(tys, " | ");
}

GenericsTy::GenericsTy(const std::string& name, GenericParamDecl& gpd)
    : Ty(TypeKind::TYPE_GENERICS), decl(&gpd), isEraseMode(IsJavaGeneric(gpd))
{
    this->name = name;
    this->generic = true;
}

std::string GenericsTy::String() const
{
    return "Generics-" + name;
}

std::string EnumTy::String() const
{
    return "Enum-" + name + PrintTypeArgs();
}

std::string RefEnumTy::String() const
{
    return "Enum-" + name + PrintTypeArgs() + "(asRef)";
}

std::string ClassTy::String() const
{
    return "Class-" + name + PrintTypeArgs();
}

std::string InterfaceTy::String() const
{
    return "Interface-" + name + PrintTypeArgs();
}

std::string StructTy::String() const
{
    return "Struct-" + name + PrintTypeArgs();
}

std::string TypeAliasTy::String() const
{
    return "TypeAlias-" + name + PrintTypeArgs();
}

std::string Ty::PrintTypeArgs() const
{
    if (typeArgs.size() == 0) {
        return "";
    }
    return "<" + GetModalTypesToStr(typeArgs, ", ") + ">";
}

std::vector<Ptr<Ty>> Ty::TyArgs() const
{
    CJC_ASSERT(kind != TypeKind::TYPE_FUNC);
    std::vector<Ptr<Ty>> tys;
    for (auto& it : typeArgs) {
        CJC_ASSERT(it.IsDataType());
        tys.push_back(it.Ty());
    }
    return tys;
}

DataTy Ty::TyArg(size_t i) const
{
    CJC_ASSERT(kind != TypeKind::TYPE_FUNC);
    return typeArgs[i].Ty();
}

DataTy Ty::GetPrimitiveUpperBound(DataTy ty)
{
    if (auto genTy = DynamicCast<GenericsTy*>(ty)) {
        if (!genTy->isPlaceholder && genTy->lowerBound) {
            return RawStaticCast<const GenericsTy*>(ty)->lowerBound;
        }
    }
    return ty;
}

bool Ty::IsTyCorrect(Ptr<const Ty> ty) noexcept
{
    return ty != nullptr && !ty->invalid;
}

bool Ty::IsTyCorrect(Ptr<Ty> ty) noexcept
{
    return IsTyCorrect(Ptr<const Ty>(ty));
}

bool Ty::IsTyCorrect(const Ty* ty) noexcept
{
    return IsTyCorrect(Ptr<const Ty>(ty));
}

bool Ty::IsTyCorrect(ModalTy ty) noexcept
{
    return ty.IsCorrect();
}

bool Ty::AreTysCorrect(const std::vector<ModalTy>& tys)
{
    return std::all_of(tys.begin(), tys.end(), [](auto& ty) { return ty.IsCorrect(); });
}
bool Ty::AreTysCorrect(const std::set<ModalTy>& tys)
{
    return std::all_of(tys.begin(), tys.end(), [](auto& ty) { return ty.IsCorrect(); });
}

bool Ty::IsPrimitiveCType() const
{
    // Primitive CType.
    static std::unordered_set<TypeKind> primitiveCType = {TypeKind::TYPE_UNIT, TypeKind::TYPE_BOOLEAN,
        TypeKind::TYPE_INT8, TypeKind::TYPE_UINT8, TypeKind::TYPE_INT16, TypeKind::TYPE_UINT16, TypeKind::TYPE_INT32,
        TypeKind::TYPE_UINT32, TypeKind::TYPE_INT64, TypeKind::TYPE_UINT64, TypeKind::TYPE_INT_NATIVE,
        TypeKind::TYPE_UINT_NATIVE, TypeKind::TYPE_FLOAT32, TypeKind::TYPE_FLOAT64};
    return primitiveCType.find(kind) != primitiveCType.end();
}

bool Ty::IsCStructType() const
{
    if (kind != TypeKind::TYPE_STRUCT) {
        return false;
    }
    auto rty = RawStaticCast<const StructTy*>(this);
    return rty->declPtr && rty->declPtr->TestAttr(Attribute::C);
}

// Check if ty is CType.
bool Ty::IsMetCType() const
{
    if (Is<VArrayTy>(*this)) {
        CJC_ASSERT(!typeArgs.empty() && typeArgs[0].IsCorrect());
        return typeArgs[0]->IsMetCType();
    }
    bool isCType = IsPrimitiveCType() || IsPointer() || IsCString() || IsCFunc() || IsCStructType();
    // TYPE_FLOAT16 is used by AI and cannot be directly deleted.
    return isCType || kind == TypeKind::TYPE_QUEST;
}

bool Ty::IsCTypeConstraint() const
{
    if (IsMetCType() && !IsPrimitiveCType()) {
        return kind == TypeKind::TYPE_STRUCT && RawStaticCast<const StructTy*>(this)->name != "String";
    }
    return false;
}

bool Ty::ExistGeneric(const std::vector<ModalTy>& tySet)
{
    return std::any_of(tySet.begin(), tySet.end(), [](auto& ty) { return ty && ty->HasGeneric(); });
}

bool Ty::ExistGeneric(const std::vector<DataTy>& tySet)
{
    return std::any_of(tySet.begin(), tySet.end(), [](const auto& ty) { return ty && ty->HasGeneric(); });
}

bool Ty::IsTyArgsSizeEqual(const Ty& ty1, const Ty& ty2)
{
    return ty1.typeArgs.size() == ty2.typeArgs.size();
}

bool Ty::IsTyArgsSingleton() const
{
    return this->typeArgs.size() == 1;
}

std::set<ModalTy> Ty::GetGenericTyArgs()
{
    if (IsGeneric()) {
        return {{this}};
    }
    std::set<ModalTy> tys;
    for (auto& it : typeArgs) {
        if (it && it->HasGeneric()) {
            tys.merge(it->GetGenericTyArgs());
        }
    }
    return tys;
}

std::set<Ptr<GenericsTy>> Ty::GetGenericTyArgs(const std::set<Ptr<AST::GenericsTy>>& candidates)
{
    if (IsGeneric() && candidates.count(RawStaticCast<GenericsTy*>(this)) > 0) {
        return {RawStaticCast<GenericsTy*>(this)};
    }
    std::set<Ptr<GenericsTy>> tys;
    for (auto& it : typeArgs) {
        if (it && it->HasGeneric()) {
            tys.merge(it->GetGenericTyArgs(candidates));
        }
    }
    return tys;
}

std::string Ty::ToString(Ptr<const Ty> ty)
{
    return ty ? ty->String() : "Invalid";
}

/** Get ty's corresponding declaration, will be instantiated decl if exist. */
template Ptr<Decl> Ty::GetDeclOfTy(Ptr<const AST::Ty> ty);
template Ptr<InheritableDecl> Ty::GetDeclOfTy(Ptr<const AST::Ty> ty);
template Ptr<Decl> Ty::GetDeclOfTy(Ptr<AST::Ty> ty);
template Ptr<InheritableDecl> Ty::GetDeclOfTy(Ptr<AST::Ty> ty);
template Ptr<InterfaceDecl> Ty::GetDeclOfTy(Ptr<AST::Ty> ty);
template <typename T> Ptr<T> Ty::GetDeclOfTy(Ptr<const Ty> ty)
{
    if (!ty) {
        return nullptr;
    }
    Ptr<Decl> decl = nullptr;
    switch (ty->kind) {
        case TypeKind::TYPE_CLASS:
            decl = RawStaticCast<const ClassTy*>(ty)->decl;
            break;
        case TypeKind::TYPE_INTERFACE:
            decl = RawStaticCast<const InterfaceTy*>(ty)->decl;
            break;
        case TypeKind::TYPE_STRUCT:
            decl = RawStaticCast<const StructTy*>(ty)->decl;
            break;
        case TypeKind::TYPE_ENUM:
            decl = RawStaticCast<const EnumTy*>(ty)->decl;
            break;
        case TypeKind::TYPE:
            decl = RawStaticCast<const TypeAliasTy*>(ty)->declPtr;
            break;
        default:;
    }
    return DynamicCast<T*>(decl);
}

template <typename T> Ptr<T> Ty::GetDeclOfTy(Ptr<Ty> ty)
{
    return GetDeclOfTy<T>(Ptr<const Ty>(ty));
}
template <typename T> Ptr<T> Ty::GetDeclOfTy(ModalTy ty)
{
    return GetDeclOfTy<T>(Ptr<const Ty>(ty.Ty()));
}

/** Get ty's corresponding declaration, always be generic decl if has generic. */
template Ptr<Decl> Ty::GetDeclPtrOfTy(Ptr<const AST::Ty> ty);
template Ptr<InheritableDecl> Ty::GetDeclPtrOfTy(Ptr<const AST::Ty> ty);
template Ptr<Decl> Ty::GetDeclPtrOfTy(Ptr<AST::Ty> ty);
template Ptr<InheritableDecl> Ty::GetDeclPtrOfTy(Ptr<AST::Ty> ty);
template <typename T> Ptr<T> Ty::GetDeclPtrOfTy(Ptr<const Ty> ty)
{
    if (!ty) {
        return nullptr;
    }
    Ptr<Decl> decl = nullptr;
    switch (ty->kind) {
        case TypeKind::TYPE_CLASS:
            decl = RawStaticCast<const ClassTy*>(ty)->declPtr;
            break;
        case TypeKind::TYPE_INTERFACE:
            decl = RawStaticCast<const InterfaceTy*>(ty)->declPtr;
            break;
        case TypeKind::TYPE_STRUCT:
            decl = RawStaticCast<const StructTy*>(ty)->declPtr;
            break;
        case TypeKind::TYPE_ENUM:
            decl = RawStaticCast<const EnumTy*>(ty)->declPtr;
            break;
        case TypeKind::TYPE:
            decl = RawStaticCast<const TypeAliasTy*>(ty)->declPtr;
            break;
        default:;
    }
    // Map common type to specific decl if exist.
    if (decl && decl->specificImplementation) {
        decl = decl->specificImplementation;
    }
    return DynamicCast<T*>(decl);
}

template <typename T> Ptr<T> Ty::GetDeclPtrOfTy(ModalTy ty)
{
    return GetDeclPtrOfTy<T>(Ptr<const Ty>(ty.Ty()));
}

template Ptr<Decl> Ty::GetDeclOfTy(ModalTy ty);
template Ptr<InheritableDecl> Ty::GetDeclOfTy(ModalTy ty);
template Ptr<InterfaceDecl> Ty::GetDeclOfTy(ModalTy ty);
template Ptr<Decl> Ty::GetDeclPtrOfTy(ModalTy ty);
template Ptr<InheritableDecl> Ty::GetDeclPtrOfTy(ModalTy ty);

/** Get instantiated ty's corresponding generic ty. */
DataTy Ty::GetGenericTyOfInsTy(const Ty& ty)
{
    if (!Ty::IsTyCorrect(Ptr<const Ty>(&ty))) {
        return nullptr;
    }
    switch (ty.kind) {
        case TypeKind::TYPE_CLASS:
            return static_cast<const ClassTy&>(ty).GetGenericTy();
        case TypeKind::TYPE_INTERFACE:
            return static_cast<const InterfaceTy&>(ty).GetGenericTy();
        case TypeKind::TYPE_STRUCT:
            return static_cast<const StructTy&>(ty).GetGenericTy();
        case TypeKind::TYPE_ENUM:
            return static_cast<const EnumTy&>(ty).GetGenericTy();
        default:
            return nullptr;
    }
}

bool Ty::IsInitialTy(Ptr<const AST::Ty> ty)
{
    return !ty || ty->kind == TypeKind::TYPE_INITIAL;
}

DataTy Ty::GetInitialTy()
{
    static InitialTy initialTy = InitialTy();
    return &initialTy;
}

static int CompareDataTyByNames(Ptr<const Ty> ty1, Ptr<const Ty> ty2)
{
    if (ty1 == ty2) {
        return 0;
    }
    if (!ty1) {
        return -1;
    }
    if (!ty2) {
        return 1;
    }
    // 'ty1' is less than 'ty2' if 'ty1' and 'ty2' are both not null and ty1's name is less then ty2's name.
    // If the tys' names are same, and any of them do not have pointed declaration,
    // compare their address to ensure all different tys are counted, otherwise compare their declarations.
    std::string str1 = ty1->String();
    std::string str2 = ty2->String();
    if (str1 != str2) {
        return str1 < str2 ? -1 : 1;
    }
    auto decl1 = Ty::GetDeclPtrOfTy(ty1);
    auto decl2 = Ty::GetDeclPtrOfTy(ty2);
    if (!decl1 || !decl2) {
        if (ty1 == ty2) {
            return 0;
        }
        return ty1 < ty2 ? -1 : 1;
    }
    if (CompNodeByPos(decl1, decl2)) {
        return -1;
    }
    if (CompNodeByPos(decl2, decl1)) {
        return 1;
    }
    return 0;
}

bool CompTyByNames(Ptr<const Ty> ty1, Ptr<const Ty> ty2)
{
    return CompareDataTyByNames(ty1, ty2) < 0;
}

bool CompTyByNamesModal(const ModalTy& ty1, const ModalTy& ty2)
{
    int cmp = CompareDataTyByNames(ty1.Ty(), ty2.Ty());
    if (cmp != 0) {
        return cmp < 0;
    }
    return ToIndex(ty1.Mode()) < ToIndex(ty2.Mode());
}

bool CmpTyByNameModal::operator()(const ModalTy& ty1, const ModalTy& ty2) const
{
    return CompTyByNamesModal(ty1, ty2);
}

template <typename TypeDeclT> bool Ty::NominalTyEqualTo(const TypeDeclT& base, const Ty& other)
{
    auto q = dynamic_cast<const TypeDeclT*>(&other);
    return q && TyArgsEqual(base.typeArgs, q->typeArgs) && base.name == q->name &&
        base.declPtr == q->declPtr;
}

bool StructTy::operator==(const Ty& other) const
{
    return NominalTyEqualTo(*this, other);
}

bool InterfaceTy::operator==(const Ty& other) const
{
    return NominalTyEqualTo(*this, other);
}

bool ClassThisTy::operator==(const Ty& other) const
{
    return NominalTyEqualTo(*this, other);
}

bool TypeAliasTy::operator==(const Ty& other) const
{
    return NominalTyEqualTo(*this, other);
}

bool RefEnumTy::operator==(const Ty& other) const
{
    return NominalTyEqualTo(*this, other);
}

bool ArrayTy::operator==(const Ty& other) const
{
    auto q = dynamic_cast<const ArrayTy*>(&other);
    return q && typeArgs == q->typeArgs && dims == q->dims;
}

bool VArrayTy::operator==(const Ty& other) const
{
    auto q = dynamic_cast<const VArrayTy*>(&other);
    return q && typeArgs == q->typeArgs && size == q->size;
}

bool PointerTy::operator==(const Ty& other) const
{
    auto q = dynamic_cast<const PointerTy*>(&other);
    return q && typeArgs == q->typeArgs;
}

bool TupleTy::operator==(const Ty& other) const
{
    auto q = dynamic_cast<const TupleTy*>(&other);
    return q && isClosureTy == q->isClosureTy && TyArgsEqual(typeArgs, q->typeArgs);
}

bool FuncTy::operator==(const Ty& other) const
{
    auto q = dynamic_cast<const FuncTy*>(&other);
    return q && isC == q->isC && noCast == q->noCast && hasVariableLenArg == q->hasVariableLenArg &&
        isClosureTy == q->isClosureTy && retTy == q->retTy && TyArgsEqual(paramTys, q->paramTys);
}

bool UnionTy::operator==(const Ty& other) const
{
    auto q = dynamic_cast<const UnionTy*>(&other);
    return q && tys == q->tys;
}

bool IntersectionTy::operator==(const Ty& other) const
{
    auto q = dynamic_cast<const IntersectionTy*>(&other);
    return q && tys == q->tys;
}

bool GenericsTy::operator==(const Ty& other) const
{
    auto q = dynamic_cast<const GenericsTy*>(&other);
    return q && decl == q->decl;
}

Mode ToLocalModal(ASTMode mod)
{
    switch (mod) {
        case ASTMode::NONE:
            return Mode::NOT;
        case ASTMode::NOT:
            return Mode::NOT;
        case ASTMode::HALF:
            return Mode::HALF;
        case ASTMode::FULL:
            return Mode::FULL;
        default:
            CJC_ABORT();
            return Mode::NOT;
    }
}

std::string_view ToString(ASTMode local)
{
    switch (local) {
        case ASTMode::NONE:
            return "";
        case ASTMode::NOT:
            return "@~local";
        case ASTMode::HALF:
            return "@local?";
        case ASTMode::FULL:
            return "@local!";
        default:
            CJC_ABORT();
            return "";
    }
}

ModalInfo ASTModalInfo::ToModalInfo() const
{
    return ModalInfo(ToLocalModal(local));
}

bool ASTModalInfo::Empty() const
{
    return local == ASTMode::NONE;
}

ASTModalInfo::operator bool() const
{
    return !Empty();
}

namespace {
constexpr int MODAL_SUFFIX_LEN = 6;
}

bool ASTModalInfo::HasLocal() const
{
    return local != ASTMode::NONE;
}

Position ASTModalInfo::LocalEnd() const
{
    return localPos + MODAL_SUFFIX_LEN;
}

Position ASTModalInfo::End() const
{
    return LocalEnd();
}
} // namespace Cangjie::AST

size_t std::hash<Cangjie::AST::ModalTy>::operator()(Cangjie::AST::ModalTy modalTy) const
{
    size_t tyHash = modalTy.Ty() ? modalTy.Ty()->Hash() : 0;
    size_t modeHash =
        static_cast<size_t>(std::hash<int>{}(Cangjie::ToIndex(modalTy.Mode())));
    return tyHash ^ (modeHash + 0x9e3779b9 + (tyHash << 6) + (tyHash >> 2));
}
