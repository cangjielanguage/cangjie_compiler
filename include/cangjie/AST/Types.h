// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Type related classes.
 */

#ifndef CANGJIE_AST_TYPES_H
#define CANGJIE_AST_TYPES_H

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cangjie/AST/ModalInfo.h"
#include "cangjie/Lex/Token.h"
#include "cangjie/Utils/SafePointer.h"

namespace Cangjie::AST {
struct Decl;
struct GenericParamDecl;
struct ClassDecl;
struct ClassLikeDecl;
struct StructDecl;
struct InterfaceDecl;
struct EnumDecl;
struct TypeAliasDecl;
struct GenericsTy;
struct ModalTy;

enum class TypeKind {
/**
 * User writable types are defined in TypeKind.inc.
 */
#define TYPE_KIND(KIND, TYPE, NAME) KIND,
#include "cangjie/AST/TypeKind.inc"
#undef TYPE_KIND
    /** NOTE: Following type kinds are only used during type checking, and will not appeared on user's code. */
    TYPE_ANY,            /**< Temporary Any type, will be replaced when start type checking. */
    TYPE_INTERSECTION,   /**< The intersection type. */
    TYPE_UNION,          /**< The union type. */
    TYPE_QUEST,          /**< The quest type. If type is not annotated) mark quest first. */
    TYPE_INITIAL,        /**< Initial type for any 'Ptr<Ty>' 's initialization. */
};

inline const std::map<TokenKind, TokenKind> COMPOUND_ASSIGN_EXPR_MAP = {{TokenKind::ADD_ASSIGN, TokenKind::ADD},
    {TokenKind::SUB_ASSIGN, TokenKind::SUB}, {TokenKind::MUL_ASSIGN, TokenKind::MUL},
    {TokenKind::EXP_ASSIGN, TokenKind::EXP}, {TokenKind::DIV_ASSIGN, TokenKind::DIV},
    {TokenKind::MOD_ASSIGN, TokenKind::MOD}, {TokenKind::AND_ASSIGN, TokenKind::AND},
    {TokenKind::OR_ASSIGN, TokenKind::OR}, {TokenKind::BITAND_ASSIGN, TokenKind::BITAND},
    {TokenKind::BITOR_ASSIGN, TokenKind::BITOR}, {TokenKind::BITXOR_ASSIGN, TokenKind::BITXOR},
    {TokenKind::LSHIFT_ASSIGN, TokenKind::LSHIFT}, {TokenKind::RSHIFT_ASSIGN, TokenKind::RSHIFT}};

/**
 * Base type.
 */
struct Ty {
    /** Represent the Sema type kind.
     * W: no.
     * R: ImportManager, Sema, GenericInstantiator, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    const TypeKind kind;
    /** Name of the user-defined type constructors. Primitive types do not have type constructors.
     * W: ImportManager.
     * R: Driver, ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string name;
    /** Type parameters for generic types or other element types.
     * W: Sema.
     * R: ImportManager, Sema, GenericInstantiator, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::vector<ModalTy> typeArgs{};

    /** Destructor.
     * U: no.
     */
    virtual ~Ty() = default;
    /** Return whether a ty is integer.
     * U: ImportManager, Sema, CHIR, HLIRCodeGen.
     */
    bool IsInteger() const;
    bool IsIntegerSubType() const;
    /** Return whether a ty is signed integer.
     * U: Sema, AST2CHIR, CHIR, HLIRCodeGen.
     */
    bool IsSignedInteger() const;
    /** Return whether a ty is unsigned integer.
     * U: Sema, AST2CHIR, CHIR, HLIRCodeGen.
     */
    bool IsUnsignedInteger() const;
    /** Return whether a ty is float.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen.
     */
    bool IsFloating() const;
    bool IsFloatingSubType() const;
    /** Return whether a ty is bool.
     * U: Sema, HLIRCodeGen.
     */
    bool IsBoolean() const;
    bool IsBooleanSubType() const;
    /** Return whether a ty is char.
     * U: Sema.
     */
    bool IsRune() const;
    /** Return whether a ty is ideal.
     * U: Sema.
     */
    bool IsIdeal() const;
    /** Return whether a ty is invalid.
     * U: Sema, AST2CHIR, HLIRCodeGen.
     */
    bool IsInvalid() const;
    /** Return whether a ty is unit.
     * U: Sema, HLIRCodeGen, LLVMCodeGen.
     */
    bool IsUnit() const;
    /** Return whether a ty is unit or nothing.
     * U: Sema, HLIRCodeGen, LLVMCodeGen.
     */
    bool IsUnitOrNothing() const;
    /** Return whether a ty is quest.
     * U: Sema.
     */
    bool IsQuest() const;
    /** Return whether a ty is primitive.
     * U: ImportManager, Sema, GenericInstantiator, HLIRCodeGen, LLVMCodeGen.
     */
    bool IsPrimitive() const;
    bool IsPrimitiveSubType() const;
    /** Return whether a ty can be extend.
     * U: Sema.
     */
    bool IsExtendable() const;
    /** Return whether a ty is numeric.
     * U: Sema.
     */
    bool IsNumeric() const;
    /** Return whether a ty is native.
     * U: Sema.
     */
    bool IsNative() const;
    /** Return whether a ty is builtin.
     * U: Sema, AST2CHIR.
     */
    bool IsBuiltin() const;
    /** Return whether a ty is immutable.
     * U: Sema.
     */
    bool IsImmutableType() const;
    /** Return whether a ty is generic.
     * U: Sema, GenericInstantiator, AST2CHIR, HLIRCodeGen.
     */
    bool IsGeneric() const;
    /** Return whether a ty is a placeholder type variable.
     * U: Sema.
     */
    bool IsPlaceholder() const;
    /** Return whether a ty is struct.
     * U: Sema, AST2CHIR, HLIRCodeGen.
     */
    bool IsStruct() const;
    /** Return whether a ty is enum.
     * U: Sema, GenericInstantiator, LLVMCodeGen.
     */
    bool IsEnum() const;
    /** Return whether a ty is option.
     * U: Sema, CodeGen
     */
    bool IsCoreOptionType() const;
    /** Return whether a ty is class.
     * U: Sema.
     */
    bool IsClass() const;
    /** Return whether a ty is interface.
     * U: Sema.
     */
    bool IsInterface() const;
    /** Return whether a ty is intersection.
     * U: Sema.
     */
    bool IsIntersection() const;
    /** Return whether a ty is union.
     * U: Sema.
     */
    bool IsUnion() const;
    /** Return whether a ty is nominal.
     * U: Sema.
     */
    bool IsNominal() const;
    /** Return whether a ty is built-in RawArray.
     * U: Sema, GenericInstantiator, HLIRCodeGen, LLVMCodeGen, Utils.
     */
    bool IsArray() const;
    /** Return whether a ty is struct Array that defined in core package.
     * U: Sema, GenericInstantiator, HLIRCodeGen, LLVMCodeGen, Utils.
     */
    bool IsStructArray() const;
    /** Return whether a ty is CPointer.
     * U: Sema, HLIRCodeGen, LLVMCodeGen, CHIR.
     */
    bool IsPointer() const;
    /** Return whether a ty is CFunc.
     * U: Sema, HLIRCodeGen, LLVMCodeGen.
     */
    virtual bool IsCFunc() const
    {
        return false;
    };
    /** Return whether a ty is C char *.
     * U: Sema, CodeGen, AST, CHIR
     */
    bool IsCString() const;
    /** Return whether a ty is erase generic.
     * U: Sema.
     */
    bool IsEraseGeneric() const;
    /** Return whether a ty is class like.
     * U: Sema, AST2CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    bool IsClassLike() const;
    /** Return whether a ty is func.
     * U: Sema, AST2CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    bool IsFunc() const;
    /** Return whether a ty is tuple.
     * U: Sema, LLVMCodeGen.
     */
    bool IsTuple() const;
    /** Return whether a ty is closure.
     * U: AST2CHIR, CodeGen
     */
    bool IsClosureType() const;
    /** Return whether a ty is object.
     * U: Sema.
     */
    bool IsObject() const;
    /** Return whether a ty is any.
     * U: Sema.
     */
    bool IsAny() const;
    /** Return whether a ty is CType interface.
     * U: Sema.
     */
    bool IsCType() const;
    /** Return whether a ty is nothing.
     * U: Sema.
     */
    bool IsNothing() const;
    /** Return whether a ty is string.
     * U: Sema, HLIRCodeGen.
     */
    bool IsString() const;

    /** Return whether a ty is range.
     * U: Sema.
     */
    bool IsRange() const;
    /** Return whether a ty contains given @p ty.
     * U: Sema.
     */
    bool Contains(Ptr<Ty> ty) const;
    /** Return whether a ty has InvalidTy.
     * U: Sema, GenericInstantiator.
     */
    bool HasInvalidTy() const;
    /** Return whether a ty has IdealTy.
     * U: Sema, GenericInstantiator.
     */
    bool HasIdealTy() const;
    /** Return whether this ty or any of its type arguments carries a pending IDEAL modal
     * (a literal's modal awaiting unification with the expected contextual modal, mirroring
     * IDEAL_INT/IDEAL_FLOAT). IDEAL is transient (Sema-only) and must be resolved before Sema
     * ends; this predicate is used by the Sema-end validator to guard against IDEAL leaking to
     * the back end.
     * U: Sema.
     */
    bool HasIdealModal() const;
    /** Return whether a ty has QuestTy.
     * U: Sema.
     */
    bool HasQuestTy() const;
    /** Return whether a ty has Generic.
     * U: Sema, GenericInstantiator, AST2CHIR.
     */
    bool HasGeneric() const;
    /** Return whether a ty has IntersectionTy.
     * U: Sema.
     */
    bool HasIntersectionTy() const;
    /** Return whether a ty has placeholder ty var.
     * U: Sema.
     */
    bool HasPlaceholder() const;
    /** Return whether a ty has alias ty.
     * U: ImportManager.
     */
    bool HasAliasTy() const;
    /** Return the upper bound of a primitive type.
     * U: Sema.
     */
    static Ptr<Ty> GetPrimitiveUpperBound(Ptr<Ty> ty);
    /** Return whether two tys has the same typeArgs number.
     * U: Sema.
     */
    static bool IsTyArgsSizeEqual(const Ty& ty1, const Ty& ty2);
    /** A correct type does not contain InvalidTy or nullptr.
     * U: Sema, GenericInstantiator, AST2CHIR.
     */
    static bool IsTyCorrect(Ptr<const Ty> ty) noexcept;
    static bool IsTyCorrect(Ptr<Ty> ty) noexcept;
    static bool IsTyCorrect(const Ty* ty) noexcept;
    static bool IsTyCorrect(ModalTy ty) noexcept;
    /** Return whether some tys are correct.
     * U: Sema.
     */
    static bool AreTysCorrect(const std::vector<Ptr<Ty>>& tys)
    {
        return std::all_of(tys.begin(), tys.end(), [](auto& ty) { return Ty::IsTyCorrect(ty); });
    }
    static bool AreTysCorrect(const std::set<Ptr<Ty>>& tys)
    {
        return std::all_of(tys.begin(), tys.end(), [](auto& ty) { return Ty::IsTyCorrect(ty); });
    }
    static bool AreTysCorrect(const std::vector<ModalTy>& tys);
    static bool AreTysCorrect(const std::set<ModalTy>& tys);
    /** APIs to check given @p ty 's category. */
    bool IsMetCType() const;
    // Check if ty is CType which is based Pointer, include CString, CPointer and CFunc.
    bool IsCTypeBasePointer() const { return IsPointer() || IsCString() || IsCFunc(); }
    bool IsCTypeConstraint() const;
    bool IsPrimitiveCType() const;
    bool IsCStructType() const;

    /** Return whether some tys has Generic.
     * U: GenericInstantiator.
     */
    static bool ExistGeneric(const std::vector<Ptr<Ty>>& tySet);
    static bool ExistGeneric(const std::vector<ModalTy>& tySet);
    /** Return the unique name of a ty if not null, otherwise return "Invalid".
     * U: Sema.
     */
    static std::string ToString(Ptr<const Ty> ty);
    /**
     * Connect all ty names in stable order with the given delimiter.
     */
    template <typename Container>
    static std::string GetTypesToStableStr(const Container& tys, const std::string& delimiter);
    /** Stable order for containers of ModalTy (CmpTyByName includes modal). */
    template <typename Container>
    static std::string GetModalTypesToStableStr(const Container& tys, const std::string& delimiter);
    template <typename Container> static std::string GetTypesToStr(const Container& tys, const std::string& delimiter)
    {
        std::string str;
        for (auto it = tys.begin(); it != tys.end(); it++) {
            if (it == tys.begin()) {
                str += ToString(*it);
            } else {
                str += delimiter + ToString(*it);
            }
        }
        return str;
    }

    template <typename Container>
    static std::string GetModalTypesToStr(const Container& tys, const std::string& delimiter)
    {
        std::string str;
        for (auto it = tys.begin(); it != tys.end(); it++) {
            if (it == tys.begin()) {
                str += it->String();
            } else {
                str += delimiter + it->String();
            }
        }
        return str;
    }
    /**
     * Get ty's corresponding declaration. The method will return instantiated decl if it exists.
     */
    template <typename T = Decl> static Ptr<T> GetDeclOfTy(Ptr<const Ty> ty);
    template <typename T = Decl> static Ptr<T> GetDeclOfTy(Ptr<Ty> ty);
    template <typename T = Decl> static Ptr<T> GetDeclOfTy(ModalTy ty);
    template <typename T = Decl> static Ptr<T> GetDeclOfTy(const Ty* ty) { return GetDeclOfTy(Ptr<const Ty>(ty)); }
    /**
     * Get ty's corresponding declaration, which always be generic decl if it has generic.
     */
    template <typename T = Decl> static Ptr<T> GetDeclPtrOfTy(Ptr<const Ty> ty);
    template <typename T = Decl> static Ptr<T> GetDeclPtrOfTy(Ptr<Ty> ty)
    {
        return GetDeclPtrOfTy<T>(Ptr<const Ty>(ty));
    }
    template <typename T = Decl> static Ptr<T> GetDeclPtrOfTy(ModalTy ty);
    template <typename T = Decl> static Ptr<T> GetDeclPtrOfTy(const Ty* ty)
    {
        return GetDeclPtrOfTy<T>(Ptr<const Ty>(ty));
    }
    /**
     * Get instantiated ty's corresponding generic ty.
     */
    static Ptr<Ty> GetGenericTyOfInsTy(const Ty& ty);
    static Ptr<Ty> GetInitialTy();

    template <typename TypeDeclT> static bool NominalTyEqualTo(const TypeDeclT& base, const Ty& other);
    /** Return whether this ty is an initial ty value. */
    static bool IsInitialTy(Ptr<const Ty> ty);
    /** Return generic typeArgs (among the candidates) of a ty.
     * U: Sema.
     */
    std::set<ModalTy> GetGenericTyArgs();
    std::set<Ptr<GenericsTy>> GetGenericTyArgs(const std::set<Ptr<GenericsTy>>& candidates);
    /** Return whether the typeArgs of a ty is only one.
     * U: Sema.
     */
    bool IsTyArgsSingleton() const;
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    virtual std::string String() const = 0;
    static std::string KindName(TypeKind k);
    /** Print typeArgs of a ty.
     * U: Sema.
     */
    std::string PrintTypeArgs() const;
    /**
     * Hash current ty.
     * For nominal type, hash with typeArgs' address and associated decl's address.
     * For other types, hash with typeArgs' address, members and typeKind.
     * For basic implementation, hash with current ty's address.
     */
    virtual size_t Hash() const;

    virtual bool operator==(const Ty& other) const
    {
        return this == &other;
    }

    std::vector<Ptr<Ty>> TyArgs() const;
    Ptr<Ty> TyArg(size_t i) const;

protected:
    /** Constructor.
     * U: Sema.
     */
    explicit Ty(TypeKind k) : kind(k) {}
    bool invalid{false}; // Type is invalid if any of element is invalid.
    bool generic{false}; // Type is generic if any of element is generic.
};

/// DataTy is used in most places where a Ty is expected.
using DataTy = Ptr<Ty>;
/// ModalTy is a combination of DataTy and ModalInfo. It is used where spec says modal type is allowed.
/// It is meant not to be allocated on heap.
struct ModalTy {
    ModalTy(DataTy ty, ModalInfo mode) : dataTy(ty), modal(mode) {}
    ModalTy(DataTy ty) : ModalTy(ty, {}) {}
    ModalTy() : dataTy{nullptr}, modal{} {}

    ModalTy(const ModalTy& other) = default;
    ModalTy(ModalTy&& other) = default;

    ModalTy& operator=(const ModalTy& other) = default;
    ModalTy& operator=(ModalTy&& other) = default;

    DataTy Ty() const { return dataTy; }
    TypeKind Kind() const { return dataTy->kind; }
    ModalInfo Mode() const { return modal; }
    ModalTy With(ModalInfo another) const { return {dataTy, another}; }

    bool IsDataType() const { return modal == ModalInfo{}; }
    bool IsLocalType() const;
    /// Whether this modal is the pending IDEAL modal (mirrors IDEAL_INT/IDEAL_FLOAT).
    bool IsIdealModal() const { return modal.local == Mode::IDEAL; }

    explicit operator bool() const { return dataTy; }
    bool operator!() const { return !dataTy; }
    bool operator==(const ModalTy& other) const;
    bool operator!=(const ModalTy& other) const;
    bool operator==(std::nullptr_t) const { return dataTy == nullptr; }
    bool operator!=(std::nullptr_t) const { return dataTy != nullptr; }
    bool operator<(const ModalTy& other) const;

    std::string String() const;
    bool IsCorrect() const;

    DataTy operator->() const { return dataTy; }
    struct Ty& operator*() const { return *dataTy; }
    struct Ty* get() const { return dataTy.get(); }

private:
    DataTy dataTy;
    ModalInfo modal;
};

/**
 * Initial type.
 */
struct InitialTy : Ty {
    InitialTy() noexcept : Ty(TypeKind::TYPE_INITIAL)
    {
        invalid = true; // Initial ty is used instead of nullptr, so also marked as invalid.
    }
    /**
     * Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override
    {
        return Ty::String();
    }
};

/**
 * Invalid type.
 */
struct InvalidTy : Ty {
    /** Constructor.
     * U: no.
     */
    InvalidTy() noexcept : Ty(TypeKind::TYPE_INVALID) { invalid = true; }
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override
    {
        return Ty::String();
    }
};

/**
 * If return type is uncertain, annotate a ? ty here.
 */
struct QuestTy : Ty {
    /** Constructor.
     * U: Sema.
     */
    QuestTy() noexcept : Ty(TypeKind::TYPE_QUEST) {}
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen. (But actually only can be called by Sema)
     */
    std::string String() const override
    {
        return Ty::String();
    }
};

/**
 * Primitive type. Contains integer, float, bool, char, unit and nothing.
 */
struct PrimitiveTy : Ty {
    // To decide actual type of IntNative.
    static constexpr uint64_t GetArchBitness()
    {
        // 1 byte = 8 bit
        return 8 * sizeof(void*);
    }
    /** To decide actual type of IntNative.
     * W: no.
     * R: Sema, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    uint64_t bitness = GetArchBitness();
    /** Constructor.
     * U: Sema.
     */
    explicit PrimitiveTy(TypeKind k) noexcept : Ty(k) {}
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override
    {
        return Ty::String();
    }
};

/**
 * Nothing type. Inherited from the 'PrimitiveTy' to accommodate all usages.
 */
struct NothingTy : PrimitiveTy {
    /** Constructor.
     * U: TypeManager.
     */
    explicit NothingTy() noexcept : PrimitiveTy(TypeKind::TYPE_NOTHING) {}
};

/**
 * Top type.
 */
struct AnyTy : Ty {
    /** Constructor.
     * U: no.
     */
    explicit AnyTy() noexcept : Ty(TypeKind::TYPE_ANY) {}
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override
    {
        return "Any";
    }
};

/**
 * Array type.
 */
struct ArrayTy : Ty {
    /** Array dimensions.
     * W: no.
     * R: ImportManager, Sema, GenericInstantiator, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    unsigned int dims{0};
    /** Constructor.
     * U: Sema.
     */
    ArrayTy(Ptr<Ty> elemTy, unsigned int dims) : Ty(TypeKind::TYPE_ARRAY), dims(dims)
    {
        name = "RawArray";
        typeArgs.emplace_back(elemTy);
        invalid = !elemTy || elemTy->HasInvalidTy();
        generic = elemTy && elemTy->HasGeneric();
    }
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

/**
 * VArray type.
 */
struct VArrayTy : public Ty {
    /** VArray size.
     * W: Sema.
     * R: ImportManager, Sema, Chir, LLVMCodeGen, HLIRCodeGen.
     */
    int64_t size{0};
    /** Constructor.
     * U: Sema.
     */
    VArrayTy(Ptr<Ty> elemTy, int64_t size) : Ty(TypeKind::TYPE_VARRAY), size(size)
    {
        name = "VArray";
        typeArgs.emplace_back(elemTy);
        invalid = !elemTy || elemTy->HasInvalidTy();
        generic = elemTy && elemTy->HasGeneric();
    }
    /** Return the unique name of a ty.
     * U: Sema.
     */
    std::string String() const override
    {
        std::string ge = "<" + typeArgs[0].String() + ", $" + std::to_string(size) + ">";
        return name + ge;
    }
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

/**
 * Pointer type.
 */
struct PointerTy : Ty {
    /** Constructor.
     * U: Sema.
     */
    PointerTy(Ptr<Ty> elemTy) : Ty(TypeKind::TYPE_POINTER)
    {
        name = "CPointer";
        typeArgs.emplace_back(elemTy);
        invalid = !elemTy || elemTy->HasInvalidTy();
        generic = elemTy && elemTy->HasGeneric();
    }
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

struct CStringTy : Ty {
    /** Constructor.
     * U: Sema, Mangle
     */
    CStringTy() noexcept : Ty(TypeKind::TYPE_CSTRING) { name = "CString"; }
    /** Return the unique name of a ty.
     * U: Sema, Mangle
     */
    std::string String() const override
    {
        return "CString";
    }
};

/**
 * Tuple type.
 */
struct TupleTy : Ty {
    /**
     * Mark whether a function is a ClosureTy. This will only be used on CHIR node's ty, never appeared on AST node.
     */
    bool isClosureTy{false};
    /** Constructor.
     * U: Sema.
     */
    explicit TupleTy(std::vector<DataTy> elemTys, bool isClosureTy = false)
        : Ty(TypeKind::TYPE_TUPLE), isClosureTy(isClosureTy)
    {
        name = "Tuple";
        for (auto ty : elemTys) {
            typeArgs.emplace_back(ty);
        }
        invalid = !Ty::AreTysCorrect(typeArgs);
        generic = Ty::ExistGeneric(typeArgs);
    }
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

/**
 * Function type.
 */
struct FuncTy : Ty {
    /**
     * Function param types.
     * W: Sema.
     * R: ImportManager, Sema, GenericInstantiator, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    const std::vector<ModalTy> paramTys{};
    /**
     * Function return type.
     * W: no.
     * R: ImportManager, Sema, GenericInstantiator, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    ModalTy const retTy{nullptr};
    /**
     * Mark whether a function is C function.
     * W: no.
     * R: ImportManager, Sema, CHIR, HLIRCodeGen.
     */
    const bool isC{false};
    /**
     * Mark whether a function is a ClosureTy. This will only be used on CHIR node's ty, never appeared on AST node.
     */
    const bool isClosureTy{false};
    /**
     * Mark whether a C function has variable-length argument.
     * W: Sema.
     * R: Sema, CHIR, HLIRCodeGen, LLVMCodeGen, Modules.
     */
    const bool hasVariableLenArg{false};
    /**
     * Mark whether this type is only an upper-bound that helps type inference, and no expression's
     * type will ever be up-cast to it. Which means implicit boxing for variance should be allowed.
     */
    const bool noCast{false};
    /** Constructor.
     * U: Sema, CHIR, Modules.
     */
    struct Config {
        const bool isC{false};
        const bool isClosureTy{false};
        const bool hasVariableLenArg{false};
        const bool noCast{false};
    };
    FuncTy(std::vector<ModalTy> paramVector, ModalTy rType, const Config cfg = {false, false, false, false})
        : Ty(TypeKind::TYPE_FUNC),
          paramTys(std::move(paramVector)),
          retTy(rType),
          isC(cfg.isC),
          isClosureTy(cfg.isClosureTy),
          hasVariableLenArg(cfg.hasVariableLenArg),
          noCast(cfg.noCast)
    {
        typeArgs = paramTys;
        // Currently, only CFunc has variable length parameters.
        invalid = !Ty::AreTysCorrect(typeArgs) || !rType.IsCorrect() || (!isC && hasVariableLenArg);
        generic = Ty::ExistGeneric(typeArgs) || (rType && rType->HasGeneric());
        typeArgs.emplace_back(retTy);
    }
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    /**
     * Mark whether a function is C function.
     * W: no.
     * R: ImportManager, Sema, CHIR, HLIRCodeGen.
     */
    bool IsCFunc() const override
    {
        return isC;
    }
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

/**
 * Union type.
 * Note: not used as normal 'Node' 's type, so does not need to set 'invalid' and 'generic' value.
 */
struct UnionTy : Ty {
    /**
     * Union types member.
     * W: no.
     * R: Sema, CHIR.
     */
    std::set<Ptr<Ty>> tys;
    /** Constructor.
     * U: Sema.
     */
    UnionTy(std::set<Ptr<Ty>> tys) : Ty(TypeKind::TYPE_UNION) { this->tys = std::move(tys); }
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    UnionTy(const UnionTy&) = delete;
    UnionTy& operator=(const UnionTy&) = delete;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

/**
 * Intersection type.
 * Note: not used as normal 'Node' 's type, so does not need to set 'invalid' and 'generic' value.
 */
struct IntersectionTy : Ty {
    /**
     * Intersection types member.
     * W: no.
     * R: Sema, CHIR.
     */
    std::set<Ptr<Ty>> tys;
    /** Constructor.
     * U: Sema.
     */
    IntersectionTy(std::set<Ptr<Ty>> tys) : Ty(TypeKind::TYPE_INTERSECTION) { this->tys = std::move(tys); }
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    IntersectionTy(const IntersectionTy&) = delete;
    IntersectionTy& operator=(const IntersectionTy&) = delete;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

struct InterfaceTy;

/**
 * Base type for class and interface.
 * Note: middle inherited type, does not need to set 'invalid' and 'generic' value.
 */
struct ClassLikeTy : Ty {
    /**
     * Class like decl pointer.
     * W: ImportManager, Sema, GenericInstantiator.
     * R: ImportManager, Sema, AST2CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    Ptr<ClassLikeDecl> commonDecl{nullptr};
    /**
     * Direct subtypes declared by class, interface or extend.
     * For example, suppose that we have `interface I`, `class A <: I`, `interface J <: I`, and `extend Int64 <: I`,
     * then the `directSubtypes` of `I` is `{A, J, Int64}`.
     * This filed is used for the exhaustive checking of sealed classes/interfaces.
     * W: Sema
     * R: Sema
     */
    std::unordered_set<Ptr<Ty>> directSubtypes;
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override
    {
        return Ty::String();
    }
    /** Return all the super interface types.
     * U: Sema, GenericInstantiator, CHIR.
     */
    virtual std::set<Ptr<InterfaceTy>> GetSuperInterfaceTys() const = 0;

protected:
    /** Constructor.
     * U: no.
     */
    ClassLikeTy(TypeKind k, ClassLikeDecl& cld) : Ty(k), commonDecl(&cld) {}
};

/**
 * Interface type.
 */
struct InterfaceTy : ClassLikeTy {
    /**
     * Generic interface decl pointer. This ptr is used to distinguish tys with same name and same typeArguments. It
     * always points to generic decl in generic ty, which is used to obtain the same InterfaceTy.
     * W: ImportManager.
     * R: Sema.
     */
    Ptr<InterfaceDecl> const declPtr{nullptr};
    /**
     * Interface decl pointer.
     * W: ImportManager, Sema, GenericInstantiator.
     * R: Sema, GenericInstantiator, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen, Utils.
     */
    Ptr<InterfaceDecl> decl{nullptr};
    /** Constructor.
     * U: Sema.
     */
    InterfaceTy(const std::string& name, InterfaceDecl& id, const std::vector<Ptr<Ty>>& typeArgs);
    /** Return all the super interface types.
     * U: Sema, GenericInstantiator, CHIR.
     */
    std::set<Ptr<InterfaceTy>> GetSuperInterfaceTys() const override;
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    /**
     * Generic interface type pointer.
     * W: ImportManager, Sema.
     * R: Sema.
     */
    Ptr<InterfaceTy> GetGenericTy() const;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

/**
 * Class type.
 */
struct ClassTy : ClassLikeTy {
    /**
     * Generic class decl pointer. This ptr is used to distinguish tys with same name and same typeArguments. It
     * always points to generic decl in generic ty, which is used to obtain the same ClassTy.
     * W: ImportManager.
     * R: Sema.
     */
    Ptr<ClassDecl> const declPtr{nullptr};
    /**
     * Class decl pointer.
     * W: ImportManager, Sema, GenericInstantiator.
     * R: Sema, GenericInstantiator, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen, Utils.
     */
    Ptr<ClassDecl> decl{nullptr};
    /** Return super class type.
     * U: Sema, GenericInstantiator, AST2CHIR, CHIR, HLIRCodeGen.
     */
    Ptr<ClassTy> GetSuperClassTy() const;
    /** Constructor.
     * U: Sema.
     */
    ClassTy(const std::string& name, ClassDecl& cd, const std::vector<Ptr<Ty>>& typeArgs);
    /** Return all the super interface types.
     * U: Sema, GenericInstantiator, CHIR.
     */
    std::set<Ptr<InterfaceTy>> GetSuperInterfaceTys() const override;
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    /**
     * Generic class type pointer.
     * W: ImportManager, Sema.
     * R: Sema.
     */
    Ptr<ClassTy> GetGenericTy() const;
    virtual size_t Hash() const override;
    virtual bool operator==(const Ty& other) const override;
};

struct ClassThisTy : ClassTy {
    /** Constructor.
     * U: Sema.
     */
    ClassThisTy(std::string name, ClassDecl& cd, std::vector<Ptr<Ty>> typeArgs) : ClassTy(name, cd, typeArgs) {}
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override { return "This"; }
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

struct TypeAliasTy : Ty {
    /**
     * TypeAlias decl pointer.
     * W: no.
     * R: ImportManager, Sema.
     */
    Ptr<TypeAliasDecl> declPtr{nullptr};
    /** Constructor.
     * U: Sema.
     */
    TypeAliasTy(const std::string& name, TypeAliasDecl& tad, const std::vector<Ptr<Ty>>& typeArgs)
        : Ty(TypeKind::TYPE), declPtr(&tad)
    {
        this->name = name;
        for (auto ty : typeArgs) {
            this->typeArgs.emplace_back(ty);
        }
        this->invalid = !Ty::AreTysCorrect(this->typeArgs);
        this->generic = Ty::ExistGeneric(this->typeArgs);
    }
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    TypeAliasTy(const TypeAliasTy&) = delete;
    TypeAliasTy& operator=(const TypeAliasTy&) = delete;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

/**
 * Generic type.
 */
struct GenericsTy : public Ty {
    /**
     * Generic param decl pointer.
     * W: no.
     * R: ImportManager, Sema, GenericInstantiator, HLIRCodeGen, LLVMCodeGen.
     */
    Ptr<GenericParamDecl> const decl{nullptr};
    /** Constructor.
     * U: Sema.
     */
    GenericsTy(const std::string& name, GenericParamDecl& gpd);
    /**
     * Mark whether decl is erased.
     * W: Sema, AST2CHIR.
     * R: Sema.
     */
    const bool isEraseMode;
    /**
     * The upperBounds of generic ty. After collecting assumptions, it will contain all direct
     * upper bounds and non-GenericsTy transitive upper bounds.
     * W: Sema.
     * R: Sema, AST2CHIR.
     */
    std::set<Ptr<Ty>> upperBounds;
    /**
     * A flag to mark whether the upperBound is legal after constraint sanity check.
     * W: Sema.
     * R: Sema.
     */
    bool isUpperBoundLegal{true};
    /**
     * The lower bound of generic ty.
     * W: Sema.
     * R: Sema.
     */
    Ptr<Ty> lowerBound{nullptr};
    /**
     * A flag to mark all type checking should be ignored for this ty var.
     * W: Sema.
     * R: Sema.
     */
    bool isAliasParam{false};
    /**
     * If false, the type var is introduced by a surrounding definition, and does not need to be solved.
     * If true, the type var is a placeholder type variable that needs to be solved, created either
     * by a generic function/type's instantiation, or by a function/lambda's parameter/return type
     * that is not annotated.
     * W: Sema.
     * R: Sema.
     */
    bool isPlaceholder{false};
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

/**
 * Enum type.
 */
struct EnumTy : Ty {
    /**
     * Generic enum decl pointer. This ptr is used to distinguish tys with same name and same typeArguments. It
     * always points to generic decl in generic ty, which is used to obtain the same EnumTy.
     * W: ImportManager.
     * R: Sema, GenericInstantiator, HLIRCodeGen.
     */
    Ptr<EnumDecl> const declPtr{nullptr};
    /**
     * Enum decl pointer.
     * W: ImportManager, Sema, GenericInstantiator.
     * R: Sema, GenericInstantiator, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    Ptr<EnumDecl> decl{nullptr};
    /**
     * don't know.
     * W: no.
     * R: no.
     */
    std::unordered_map<std::string, intptr_t> fieldMap;
    /** Constructor.
     * U: Sema.
     */
    EnumTy(const std::string& name, EnumDecl& ed, const std::vector<Ptr<Ty>>& typeArgs);
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    /** Return all the super interface types.
     * U: Sema, GenericInstantiator.
     */
    std::set<Ptr<InterfaceTy>> GetSuperInterfaceTys() const;
    /**
     * Generic enum type pointer.
     * W: ImportManager, Sema.
     * R: Sema.
     */
    Ptr<EnumTy> GetGenericTy() const;
    /**
     * Whether a correspond RefEnumTy exists or not.
     * W: Sema
     * R: CodeGen
     */
    bool hasCorrespondRefEnumTy = false;
    virtual size_t Hash() const override;
    virtual bool operator==(const Ty& other) const override;
    bool IsNonExhaustive() const;
};

struct RefEnumTy : EnumTy {
    RefEnumTy(const std::string& name, EnumDecl& ed, const std::vector<Ptr<Ty>>& typeArgs) : EnumTy(name, ed, typeArgs)
    {
        this->hasCorrespondRefEnumTy = true;
    }
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

/**
 * Struct type.
 */
struct StructTy : Ty {
    /**
     * Generic 'struct' decl pointer. This ptr is used to distinguish tys with same name and same typeArguments. It
     * always points to generic decl in generic ty, which is used to obtain the same StructTy.
     * W: ImportManager.
     * R: Sema.
     */
    Ptr<StructDecl> const declPtr{nullptr};
    /**
     * Struct decl pointer.
     * W: ImportManager, Sema, GenericInstantiator.
     * R: Sema, GenericInstantiator, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    Ptr<StructDecl> decl{nullptr};
    /** Constructor.
     * U: Sema.
     */
    StructTy(const std::string& name, StructDecl& sd, const std::vector<Ptr<Ty>>& typeArgs);
    /** Return the unique name of a ty.
     * U: ImportManager, Sema, AST2CHIR, CHIR, HLIRCodeGen, LLVMCodeGen.
     */
    std::string String() const override;
    /** Return all the super interface types.
     * U: Sema, GenericInstantiator.
     */
    std::set<Ptr<InterfaceTy>> GetSuperInterfaceTys() const;
    /**
     * Generic struct type pointer.
     * W: ImportManager, Sema.
     * R: Sema.
     */
    Ptr<StructTy> GetGenericTy() const;
    size_t Hash() const override;
    bool operator==(const Ty& other) const override;
};

bool CompTyByNames(Ptr<const Ty> ty1, Ptr<const Ty> ty2);
bool CompTyByNamesModal(const ModalTy& ty1, const ModalTy& ty2);

/// Provide a stable arbitrary ordering of Ty.
struct CmpTyByName {
    bool operator()(Ptr<const Ty> ty1, Ptr<const Ty> ty2) const
    {
        return CompTyByNames(ty1, ty2);
    }
};

struct CmpTyByNameModal {
    bool operator()(const ModalTy& ty1, const ModalTy& ty2) const;
};
} // namespace Cangjie::AST

template <> struct std::hash<Cangjie::AST::ModalTy> {
    size_t operator()(Cangjie::AST::ModalTy modalTy) const;
};
#endif // CANGJIE_AST_TYPES_H
