// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_VALUE_DOMAIN_H
#define CANGJIE_CHIR_ANALYSIS_VALUE_DOMAIN_H

#include "cangjie/CHIR/Analysis/Analysis.h"
#include "cangjie/CHIR/IR/Value/Value.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace Cangjie::CHIR {

/**
 * @brief template equal functor to compare domain
 */
template <typename T> struct HasEqual {
    template <typename U>
    static auto hasEqual(int) -> decltype(std::declval<U>().Join(std::declval<U&>()), std::true_type());
    template <typename U> static auto hasToString(int) -> decltype(std::declval<U>().ToString(), std::true_type());
    template <typename U> static auto hasClone(int) -> decltype(std::declval<U>().Clone(), std::true_type());
    template <typename U> static std::false_type hasEqual(...);
    template <typename U> static std::false_type hasToString(...);
    template <typename U> static std::false_type hasClone(...);
    static constexpr bool value =
        decltype(hasEqual<T>(0))::value && decltype(hasToString<T>(0))::value && decltype(hasClone<T>(0))::value;
};

/**
 * @brief abstract object to store domain of CHIR value.
 */
class AbstractObject : public Value {
public:
    /// create an abstract object from identifier created by analysis pass.
    explicit AbstractObject(std::string identifier);

    using Value::ToString;

    std::string ToString() const override;

    /// get static top object.
    static AbstractObject* GetTopObjInstance();

    /// if this object is top object.
    bool IsTopObjInstance() const;
};

class Ref {
public:
    /// reference object of CHIR value.
    explicit Ref(std::string uniqueID, bool isStatic);

    /// get unique id created by analysis pass.
    std::string GetUniqueID() const;

    /// add input ref to this roots.
    void AddRoots(Ref* r1, Ref* r2);

    /// check if is same as input ref.
    bool IsEquivalent(Ref* r);

    /// check ref can be represented, write cache if meet new one.
    bool CanRepresent(Ref* r);

    /// get info directly from cache, otherwise return nullopt.
    std::optional<bool> CheckCache(Ref* r);

    /// write cache to ref with input.
    void WriteCache(Ref* r, bool res);

    /// get top ref.
    static Ref* GetTopRefInstance();

    /// check if ref is top.
    bool IsTopRefInstance() const;

private:
    bool isStatic;
    std::string uniqueID;
    std::unordered_set<Ref*> roots;
    std::unordered_map<Ref*, bool> cache;
    std::mutex cacheMtx;
};

/**
 * @brief Abstract domain for CHIR value.
 * @tparam AbstractValue value type to analyse in specific domain.
 */
template <typename AbstractValue, typename = std::enable_if_t<HasEqual<AbstractValue>::value>>
class ValueDomain : public AbstractDomain<ValueDomain<AbstractValue>> {
public:
    /**
     * @brief The kind of a abstract value. A abstract value can be a Ref, a Val, Top or Bottom.
     *
     *                         Top
     *                          |      \       \       \
     *   ...   Ref(a)  Ref(b)  ...   Val(1)   Val(2)   ...
     *    \       \      \      |      /       /       /
     *                       Bottom
     *
     * From the Hasse diagram, we can see that the abstract domain is not a standard domain.
     * This is, there is no an explicit partial-order relationship between a Ref and Top.
     * The reason is, if the abstract value of a value is a Ref. Then ...
     *
     */
    enum class ValueKind { BOTTOM, REF, VAL, TOP };

    /**
     * @brief delete default constructor.
     */
    ValueDomain() = delete;

    /**
     * @brief create a bound domain.
     * @param isTop create top is true else bottom.
     */
    ValueDomain(bool isTop) : kind(isTop ? ValueKind::TOP : ValueKind::BOTTOM)
    {
    }

    /**
     * @brief create a ref domain.
     * @param ref ref to create domain.
     */
    ValueDomain(Ref* ref) : kind(ValueKind::REF), ref(ref)
    {
    }

    /**
     * @brief create an object domain.
     * @param absVal object to create domain.
     */
    ValueDomain(std::unique_ptr<AbstractValue> absVal) : kind(ValueKind::VAL), absVal(std::move(absVal))
    {
    }

    /// copy constructor.
    ValueDomain(const ValueDomain<AbstractValue>& rhs)
    {
        this->kind = rhs.kind;
        this->ref = rhs.ref;
        if (rhs.absVal) {
            this->absVal = rhs.absVal->Clone();
        } else {
            this->absVal.reset();
        }
    }

    /// copy operator.
    ValueDomain& operator=(const ValueDomain<AbstractValue>& rhs)
    {
        this->kind = rhs.kind;
        this->ref = rhs.ref;
        if (rhs.absVal) {
            this->absVal = rhs.absVal->Clone();
        } else {
            this->absVal.reset();
        }
        return *this;
    }

    /// move constructor.
    ValueDomain(ValueDomain&& rhs)
    {
        this->kind = rhs.kind;
        this->ref = rhs.ref;
        this->absVal = std::move(rhs.absVal);
    }
    /// move operator.
    ValueDomain& operator=(ValueDomain&& rhs)
    {
        this->kind = rhs.kind;
        this->ref = rhs.ref;
        this->absVal = std::move(rhs.absVal);
        return *this;
    }

    /// create a bound domain from operator.
    ValueDomain& operator=(bool isTop)
    {
        this->kind = isTop ? ValueKind::TOP : ValueKind::BOTTOM;
        this->ref = nullptr;
        this->absVal.reset();
        return *this;
    }

    /// create domain from a ref.
    ValueDomain& operator=(Ref* r)
    {
        CJC_ASSERT(kind != ValueKind::VAL);
        this->kind = ValueKind::REF;
        this->ref = r;
        return *this;
    }

    /// create domain from a value.
    ValueDomain& operator=(std::unique_ptr<AbstractValue> vl)
    {
        CJC_ASSERT(this->kind != ValueKind::REF);
        this->kind = ValueKind::VAL;
        this->absVal = std::move(vl);
        return *this;
    }

    /// destructor.
    virtual ~ValueDomain()
    {
    }

    /// join two different domain, return std::nullopt if no change happened.
    bool Join(const ValueDomain<AbstractValue>& rhs) override
    {
        /**
         *  1. (Top, _)                 => no change
         *  2. a) (Ref, Bottom)         => no change
         *     a) (Ref, Ref)            => must be equal, thus no change
         *     b) (Ref, Val)            => impossible
         *     c) (Ref, Top)            => Top
         *  3. a) (Val, Bottom)         => no change
         *     b) (Val, Ref)            => impossible
         *     c) (Val(a), Val(b))      => Top if a != b; otherwise no change
         *     d) (Val, Top)            => Top
         *  4. a) (Bottom, Bottom)      => no change
         *     b) (Bottom, v)           => v
         */

        if (this->kind == ValueKind::TOP || rhs.kind == ValueKind::BOTTOM) {
            return false;
        }

        if (rhs.kind == ValueKind::TOP) {
            SetSelfToBound(/* isTop = */ true);
            return true;
        }

        if (this->kind == ValueKind::BOTTOM) {
            *this = rhs;
            return true;
        }

        if (this->kind == ValueKind::REF && rhs.kind == ValueKind::REF) {
            CJC_ASSERT(this->ref && rhs.ref);
            if (this->ref->IsTopRefInstance()) {
                return false;
            } else if (rhs.ref->IsTopRefInstance()) {
                SetSelfToTopRef();
                return true;
            } else if (this->ref == rhs.ref) {
                return false;
            } else {
                SetSelfToTopRef();
                return true;
            }
        }

        if (this->kind == ValueKind::VAL && rhs.kind == ValueKind::VAL) {
            CJC_ASSERT(this->absVal && rhs.absVal);
            auto res = this->absVal->Join(*(rhs.absVal));
            if (res.has_value()) {
                if (res.value().get() == nullptr) {
                    SetSelfToBound(/* isTop = */ true);
                } else {
                    this->absVal = std::move(res.value());
                }
                return true;
            } else {
                return false;
            }
        }

        // (Val, Ref) or (Ref, Val)
        CJC_ABORT();
        return false;
    }

    /// check if domain is bottom.
    bool IsBottom() const override
    {
        return kind == ValueKind::BOTTOM;
    }

    /// check if domain is top.
    bool IsTop() const
    {
        return kind == ValueKind::TOP;
    }

    std::string ToString() const override
    {
        switch (kind) {
            case ValueKind::BOTTOM:
                return "Bottom";
            case ValueKind::REF:
                return this->ref->GetUniqueID();
            case ValueKind::VAL:
                return absVal->ToString();
            case ValueKind::TOP:
                return "TOP";
        }
    }

    /// get value kind.
    ValueKind GetKind() const
    {
        return kind;
    }

    /// get ref if kind is ref.
    const Ref* GetRef() const
    {
        CJC_ASSERT(kind == ValueKind::REF);
        return ref;
    }

    /// set ref if kind is ref.
    void SetRef(Ref* r)
    {
        CJC_ASSERT(kind != ValueKind::VAL);
        if (kind != ValueKind::REF) {
            kind = ValueKind::REF;
        }
        this->ref = r;
    }

    /// set to bound, top is true else bottom.
    void SetSelfToBound(bool isTop)
    {
        this->kind = isTop ? ValueKind::TOP : ValueKind::BOTTOM;
        ref = nullptr;
        absVal.reset();
    }

    /// set to top ref if kind is ref.
    void SetSelfToTopRef()
    {
        CJC_ASSERT(kind == ValueKind::REF);
        ref = Ref::GetTopRefInstance();
    }

    /// get object if kind is object.
    const AbstractValue* CheckAbsVal() const
    {
        CJC_ASSERT(kind != ValueKind::REF);
        return absVal ? absVal.get() : nullptr;
    }

    bool IsRef() const
    {
        return kind == ValueKind::REF;
    }

private:
    ValueKind kind;
    Ref* ref;
    std::unique_ptr<AbstractValue> absVal;
};
} // namespace Cangjie::CHIR

#endif
