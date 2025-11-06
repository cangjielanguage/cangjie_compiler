// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_VALUE_ANALYSIS_H
#define CANGJIE_CHIR_ANALYSIS_VALUE_ANALYSIS_H

#include "cangjie/CHIR/Analysis/ActiveStatePool.h"
#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Analysis/ValueDomain.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"

#include <iostream>
#include <utility>
#include <variant>
#include <vector>

namespace {
// judge if generic type is instance of a certain type. 
template <class T, template<class...> class U>
inline constexpr bool is_instance_of_v = std::false_type{};

template <template<class...> class U, class... Vs>
inline constexpr bool is_instance_of_v<U<Vs...>, U> = std::true_type{};
}

namespace Cangjie::CHIR {
template <typename ValueDomain, typename ValueStatePool> class ValueAnalysis;

/**
 * @brief abstract state to store CHIR value state, mainly store value and reference state.
 * @tparam ValueDomain abstract domain to store status of CHIR value.
 */
template <typename ValueDomain, typename ValueStatePool = FullStatePool<ValueDomain>,
    typename = std::enable_if_t<std::is_base_of_v<AbstractDomain<ValueDomain>, ValueDomain>>>
class State final : public AbstractDomain<State<ValueDomain, ValueStatePool>> {
    friend class ValueAnalysis<ValueDomain, ValueStatePool>;

public:
    /// reference map from CHIR ref to object or other ref.
    using RefMap = std::unordered_map<const Ref*, std::variant<Ref*, AbstractObject*>>;
    /// children map from parent value to its child object.
    using ChildrenMap = std::unordered_map<Value*, std::vector<AbstractObject*>>;
    /// map from CHIR allocate expression to ref
    using AllocatedRefMap = std::unordered_map<const Expression*, Ref*>;
    /// map from CHIR allocate expression to object
    using AllocatedObjMap = std::unordered_map<const Expression*, AbstractObject*>;

    /**
     * @brief State constructor from relation map of value in function.
     * @param childrenMap CHIR child map.
     * @param allocatedRefMap allocate reference map.
     * @param allocatedTwoLevelRefMap allocate reference map for two level reference.
     * @param allocatedObjMap allocate object map.
     * @param refPool all references in function.
     * @param absObjPool all objects in function.
     */
    explicit State(ChildrenMap* childrenMap, AllocatedRefMap* allocatedRefMap, AllocatedRefMap* allocatedTwoLevelRefMap,
        AllocatedObjMap* allocatedObjMap, std::vector<std::unique_ptr<Ref>>* refPool,
        std::vector<std::unique_ptr<AbstractObject>>* absObjPool)
        : childrenMap(childrenMap),
          allocatedRefMap(allocatedRefMap),
          allocatedTwoLevelRefMap(allocatedTwoLevelRefMap),
          allocatedObjMap(allocatedObjMap),
          refPool(refPool),
          absObjPool(absObjPool)
    {
    }

    /**
     * @brief copy constructor
     * @param rhs other state to copy.
     */
    State(const State<ValueDomain, ValueStatePool>& rhs)
    {
        this->kind = rhs.kind;
        this->programState = rhs.programState;
        this->refMap = rhs.refMap;
        this->childrenMap = rhs.childrenMap;
        this->allocatedRefMap = rhs.allocatedRefMap;
        this->allocatedTwoLevelRefMap = rhs.allocatedTwoLevelRefMap;
        this->allocatedObjMap = rhs.allocatedObjMap;
        this->refPool = rhs.refPool;
        this->absObjPool = rhs.absObjPool;
    }

    /**
     * @brief copy operator
     * @param rhs other state to copy.
     * @return copied state.
     */
    State& operator=(const State<ValueDomain, ValueStatePool>& rhs)
    {
        this->kind = rhs.kind;
        this->programState = rhs.programState;
        this->refMap = rhs.refMap;
        // We don't re-assign `childrenMap`, `allocatedRefMap`, `allocatedObjMap`, `refPool` and `absObjPool`.
        // The reason is, these three fields are identical across all the states in an analysis
        // to a function.
        return *this;
    }

    ~State()
    {
    }

    /**
     * @brief join two state, mainly happen in merge two states of different blocks.
     * @param rhs other state to join.
     * @return true if state changed.
     */
    bool Join(const State<ValueDomain, ValueStatePool>& rhs) override
    {
        if (rhs.kind == ReachableKind::UNREACHABLE) {
            return false;
        } else if (this->kind == ReachableKind::UNREACHABLE) {
            *this = rhs;
            return true;
        } else {
            auto changed = MapJoin<ValueDomain>(programState, rhs.programState);
            // We can consider only do the Join operations on living Refs.
            changed |= RefMapJoin(rhs);
            return changed;
        }
    }

    /**
     * @brief output string with beauty format.
     * @return output string.
     */
    std::string ToString() const override
    {
        if (this->kind == ReachableKind::UNREACHABLE) {
            return "Unreachable";
        } else {
            std::stringstream ss;
            ss << "programState: { ";
            for (auto it = programState.Begin(); it != programState.End(); it++) {
                ss << it->first->GetIdentifier() << " -> " << it->second.ToString() << ", ";
            }
            ss << "}\n";
            ss << "refMap: { ";
            for (auto& [k, v] : std::as_const(refMap)) {
                ss << k->GetUniqueID() << " -> ";
                if (auto ref = std::get_if<Ref*>(&v)) {
                    ss << (*ref)->GetUniqueID();
                } else {
                    ss << std::get<AbstractObject*>(v)->GetIdentifier();
                }
                ss << ", ";
            }
            ss << "}";
            return ss.str();
        }
    }

    /**
     *  @brief Update the abstract state of value @p dest.
     *  @param dest value to update
     *  @param absVal domain to update to value.
     */
    template <typename Arg, typename = std::enable_if<std::is_constructible_v<ValueDomain, Arg>>>
    void Update(Value* dest, Arg&& absVal)
    {
        if (auto it = programState.Find(dest); it != programState.End()) {
            it->second = std::forward<Arg>(absVal);
        } else {
            programState.emplace(dest, ValueDomain(std::forward<Arg>(absVal)));
        }
    }

    /**
     * @brief Set the abstract state of value @p dest to the bound value of the abstract domain.
     * @param dest value to set to bound.
     * @param isTop set to top if true else set to bottom.
     */
    void SetToBound(Value* dest, bool isTop)
    {
        if (auto it = programState.Find(dest); it != programState.End()) {
            it->second.SetSelfToBound(isTop);
        } else {
            programState.emplace(dest, ValueDomain(isTop));
        }
    }

    /**
     * @brief do InitToTopOrTopRef if dest is in state map.
     * @param dest value to init.
     * @param isRef set to top ref if true else set to top value.
     */
    void TrySetToTopOrTopRef(Value* dest, bool isRef)
    {
        if (auto it = programState.Find(dest); it != programState.End()) {
            return;
        }
        InitToTopOrTopRef(dest, isRef);
    }

    /**
     * @brief Initialise or update the state of @p dest to a Top or a TopRef depends on @p isRef.
     * @param dest value to set to bound.
     * @param isRef set to top ref if true else set to top value.
     */
    void SetToTopOrTopRef(Value* dest, bool isRef)
    {
        if (auto it = programState.Find(dest); it != programState.End()) {
            if (isRef) {
                it->second = Ref::GetTopRefInstance();
            } else {
                it->second = ValueDomain(/* isTop = */ true);
            }
        } else {
            InitToTopOrTopRef(dest, isRef);
        }
    }

    /// Assuming the state of value @p dest is not a Ref, return a const pointer to the
    /// abstract value of value @p dest.
    auto CheckAbstractValue(Value* obj) const
    {
        auto it = programState.Find(obj);
        return it != programState.End() ? it->second.CheckAbsVal() : nullptr;
    }

    /**
     * @brief get abstract domain from ref or object
     * @param obj ref or object to get its abstract domain.
     * @return domain relation to object or ref.
     */
    const ValueDomain* GetAbstractDomain(Value* obj) const
    {
        auto absObj = obj;
        if (obj->GetType()->IsRef()) {
            absObj = CheckAbstractObjectRefBy(obj);
        }

        return CheckAbstractValueWithTopBottom(absObj);
    }

    /**
     * @brief checkout state from object, return null if no state found.
     * @param obj object to get domain.
     * @return return null if no state found else its domain.
     */
    const ValueDomain* CheckAbstractValueWithTopBottom(Value* obj) const
    {
        auto it = programState.Find(obj);
        return it != programState.End() ? &it->second : nullptr;
    }

    /**
     * @brief Propagate the state of value @p src to @p dest and handle their children as appropriate.
     * @param src source value state pass from
     * @param dest destination state pass to
     * @param state state input to update, null if update state self.
     */
    void Propagate(Value* src, Value* dest, State<ValueDomain, ValueStatePool>* state = nullptr)
    {
        auto targetState = !state ? this : state;
        auto targetProgramState = &targetState->programState;
        PropagateWithoutChildren(src, dest, targetProgramState);

        auto destIt = targetState->childrenMap->find(dest);
        auto srcIt = childrenMap->find(src);
        if (destIt != targetState->childrenMap->end()) {
            auto& destChildren = destIt->second;
            if (srcIt != childrenMap->end()) {
                // a) Both dest value and src value have children information. Therefore, we just need
                // to propagate the state of children in order. It should be noted that, for certain
                // types such as Enum, we are only concerned with that state of **some** of the members
                // (e.g. for enum, we only record the state of the constructor index). Thus, the size of
                // dest children may be less than the size of src children.
                auto& srcChildren = srcIt->second;
                CJC_ASSERT(srcChildren.size() >= destChildren.size());
                for (size_t i = 0; i < destChildren.size(); ++i) {
                    PropagateWithoutChildren(srcChildren[i], destChildren[i], targetProgramState);
                }
            } else {
                // b) The dest value has children, but the src value does not have children information.
                // This often happens when we use a untracked structure(e.g. return value of a function,
                // or nested member of custom definition) to assign a tracked structure.
                // e.g. var sa = SA(); sa = ca.sa
                // In this case, we want to change any known member information(if its value kind is VAL)
                // to an unknown status(Top).
                targetState->SetNonTopChildrenStateToTop(destChildren);
            }
        } else if (srcIt != childrenMap->end()) {
            // c) The dest value does not have children, but the src value does.
            // What we do is creating the children for dest and propagating the state of children in order.
            auto& srcChildren = srcIt->second;
            const auto setChildState = [this, &srcChildren, targetProgramState](AbstractObject* child, size_t index) {
                targetProgramState->emplace(child, programState.At(srcChildren[index]));
            };
            targetState->CreateChildren(dest, srcChildren.size(), setChildState);
        }
        // d) Neither dest value and src value have children.
        // Do nothing.
    }

    /**
     * @brief @p refVal should be a first-class reference or a second-class reference (e.g. Class-CA& or
     * Class-CA&&).
     */
    AbstractObject* CheckAbstractObjectRefBy(Value* refVal) const
    {
        auto it = programState.Find(refVal);
        if (it != programState.End()) {
            CJC_ASSERT(it->second.GetKind() == ValueDomain::ValueKind::REF);
            auto refIt = refMap.find(it->second.GetRef());
            if (refIt != refMap.end()) {
                if (std::holds_alternative<AbstractObject*>(refIt->second)) {
                    return std::get<AbstractObject*>(refIt->second);
                } else {
                    auto oRefIt = refMap.find(std::get<Ref*>(refIt->second));
                    if (oRefIt != refMap.end()) {
                        CJC_ASSERT(std::holds_alternative<AbstractObject*>(oRefIt->second));
                        return std::get<AbstractObject*>(oRefIt->second);
                    }
                }
            }
        }
        return nullptr;
    }

    /// Get the children of @p obj. If it does not have any child, an empty vector will be returned.
    std::vector<AbstractObject*> GetChildren(Value* obj)
    {
        if (auto it = childrenMap->find(obj); it != childrenMap->end()) {
            return it->second;
        }
        if (auto it = programState.Find(obj); it != programState.End()) {
            if (it->second.GetKind() == ValueDomain::ValueKind::REF) {
                auto ref = it->second.GetRef();
                if (auto refIt = refMap.find(ref); refIt != refMap.end()) {
                    CJC_ASSERT(std::holds_alternative<AbstractObject*>(refIt->second));
                    return GetChildren(std::get<AbstractObject*>(refIt->second));
                }
            }
        }
        return {};
    }

    /**
     * @brief get child from object with index input.
     * @param obj parent object.
     * @param index child object index.
     * @return child object
     */
    AbstractObject* GetChild(Value* obj, size_t index)
    {
        auto children = GetChildren(obj);
        return index < children.size() ? children[index] : nullptr;
    }

    /**
     * @brief create ref to CHIR reference type value and set to top.
     * @param dest CHIR reference type value.
     * @param expr mostly allocate expression.
     * @return abstract object created.
     */
    AbstractObject* GetReferencedObjAndSetToTop(Value* dest, const Expression* expr = nullptr)
    {
        CJC_ASSERT(!dest->GetType() || dest->GetType()->IsRef() || dest->GetType()->IsGeneric());
        auto ref = CreateNewRef(expr);
        Update(dest, ref);
        auto obj = CreateNewObject(GetObjName(absObjPool->size()), expr);
        SetToBound(obj, /* isTop = */ true);
        refMap.emplace(ref, obj);
        return obj;
    }

    AbstractObject* GetTwoLevelRefAndSetToTop(Value* dest, const Expression* expr)
    {
        CJC_ASSERT(!dest->GetType() || dest->GetType()->IsRef());
        auto firstRef = StaticCast<RefType*>(dest->GetType());
        CJC_ASSERT(firstRef->IsRef());
        auto refOuter = CreateNewRef(expr);
        Update(dest, refOuter);
        auto refInner = CreateNewRef(expr, true);
        refMap.emplace(refOuter, refInner);
        auto obj = CreateNewObject(GetObjName(absObjPool->size()), expr);
        refMap.emplace(refInner, obj);
        SetToBound(obj, true);
        return obj;
    }

    /// check value is bottom
    bool CheckValueIsBottom(const Ptr<Value>& value) const
    {
        auto it = programState.Find(value);
        return it != programState.End() && it->second.GetKind() == ValueDomain::ValueKind::BOTTOM;
    }

    /// check value is top
    bool CheckValueIsTop(const Ptr<Value>& value) const
    {
        auto it = programState.Find(value);
        return it != programState.End() && it->second.GetKind() == ValueDomain::ValueKind::TOP;
    }

    bool CheckValueIsObject(const Ptr<Value>& value) const
    {
        auto it = programState.Find(value);
        return it != programState.End() && it->second.GetKind() == ValueDomain::ValueKind::VAL;
    }

    bool CheckValueIsRef(const Ptr<Value>& value) const
    {
        auto it = programState.Find(value);
        return it != programState.End() && it->second.GetKind() == ValueDomain::ValueKind::REF;
    }

    /// clear all states.
    void ClearState()
    {
        for (auto it = programState.Begin(); it != programState.End(); ++it) {
            if (it->second.GetKind() == ValueDomain::ValueKind::VAL) {
                // set to top
                it->second = true;
            }
        }
    }

private:
    Ref* CreateNewRef(const Expression* expr = nullptr, bool createTwoLevelRef = false)
    {
        auto& allocateMap = createTwoLevelRef ? allocatedTwoLevelRefMap : allocatedRefMap;
        Ref* ref = nullptr;
        bool isStaticRef = &ValueAnalysis<ValueDomain, ValueStatePool>::globalState == this;
        if (expr) {
            if (auto it = allocateMap->find(expr); it != allocateMap->end()) {
                ref = it->second;
            } else {
                ref = refPool->emplace_back(std::make_unique<Ref>(GetRefName(refPool->size()), isStaticRef)).get();
                allocateMap->emplace(expr, ref);
            }
        } else {
            ref = refPool->emplace_back(std::make_unique<Ref>(GetRefName(refPool->size()), isStaticRef)).get();
        }
        return ref;
    }

    AbstractObject* CreateNewObject(std::string objectName, const Expression* expr = nullptr)
    {
        objectName =
            &ValueAnalysis<ValueDomain, ValueStatePool>::globalState == this ? "s" + objectName : objectName;
        AbstractObject* obj = nullptr;
        if (expr) {
            if (auto it = allocatedObjMap->find(expr); it != allocatedObjMap->end()) {
                obj = it->second;
            } else {
                obj = absObjPool->emplace_back(std::make_unique<AbstractObject>(objectName)).get();
                allocatedObjMap->emplace(expr, obj);
            }
        } else {
            obj = absObjPool->emplace_back(std::make_unique<AbstractObject>(objectName)).get();
        }
        return obj;
    }

    /**
     * Initialise the state of @p dest to a Top or a TopRef depends on @p isRef.
     */
    void InitToTopOrTopRef(Value* dest, bool isRef)
    {
        CJC_ASSERT(programState.Find(dest) == programState.End());
        if (isRef) {
            programState.emplace(dest, Ref::GetTopRefInstance());
        } else {
            programState.emplace(dest, ValueDomain(/* isTop = */ true));
        }
    }

    /**
     * Propagate the state of value @p src to @p dest. Comparing to the function @fn Propagate,
     * this function won't propagate the state of these values' children.
     */
    void PropagateWithoutChildren(Value* src, Value* dest, ValueStatePool* state = nullptr)
    {
        state = !state ? &this->programState : state;
        if (!src->IsParameter() && !src->IsLocalVar()) {
            CJC_ASSERT(src->IsFunc());
            if (state->Find(dest) == state->End()) {
                state->emplace(dest, true);
            }
            return;
        }
        if (auto it = state->Find(dest); it != state->End()) {
            it->second = programState.At(src);
        } else {
            state->emplace(dest, programState.At(src));
        }
    }

    /**
     * Create the children of @p obj based on the @p childrenNum. The parameter @p setChildState
     * is used to set the state of each child.
     */
    void CreateChildren(Value* obj, size_t childrenNum, std::function<void(AbstractObject*, size_t)> setChildState)
    {
        auto objName = obj->GetIdentifier();
        std::vector<AbstractObject*> children;
        for (size_t i = 0; i < childrenNum; ++i) {
            auto child = CreateNewObject(GetObjChildName(objName, i));
            setChildState(child, i);
            children.emplace_back(child);
        }
        if (!children.empty()) {
            childrenMap->emplace(obj, std::move(children));
        }
    }

    void SetSelfAndChildrenStateToTop(Value* val)
    {
        Value* obj = nullptr;
        if (val->GetType()->IsRef()) {
            obj = CheckAbstractObjectRefBy(val);
        } else {
            obj = val;
        }
        if (obj) {
            SetToBound(obj, /* isTop = */ true);
            SetNonTopChildrenStateToTop(GetChildren(obj));
        }
    }

    void SetNonTopChildrenStateToTop(const std::vector<AbstractObject*>& children)
    {
        for (auto child : children) {
            auto childIt = programState.Find(child);
            if (is_instance_of_v<ValueStatePool, ActiveStatePool>) {
                if (childIt == programState.End()) {
                    continue;
                }
            }
            CJC_ASSERT(childIt != programState.End());
            if (childIt->second.GetKind() == ValueDomain::ValueKind::VAL) {
                childIt->second = /* isTop = */ true;
            }
        }
    }

    void StoreGVChildrenState(Value* src, AbstractObject* dest)
    {
        auto& gs = ValueAnalysis<ValueDomain, ValueStatePool>::globalState;
        auto srcChildrenIt = childrenMap->find(src);
        if (srcChildrenIt == childrenMap->end()) {
            return;
        }
        auto& srcChildren = srcChildrenIt->second;
        auto destChildren = gs.GetChildren(dest);
        CJC_ASSERT(srcChildren.size() == destChildren.size());
        for (size_t i = 0; i < srcChildren.size(); ++i) {
            auto srcStateIt = programState.Find(srcChildren[i]);
            CJC_ASSERT(srcStateIt != programState.End());
            auto& objState = srcStateIt->second;
            if (objState.GetKind() == ValueDomain::ValueKind::VAL) {
                gs.Update(destChildren[i], objState);
            } else if (objState.GetKind() == ValueDomain::ValueKind::REF) {
                if (auto srcObj = CheckAbstractObjectRefBy(srcChildren[i]); srcObj) {
                    // The state of a local child may be a TopRef.
                    auto targetObj = gs.GetReferencedObjAndSetToTop(destChildren[i]);
                    PropagateWithoutChildren(srcObj, targetObj, &gs.programState);
                }
            }
        }
    }

    void LoadGVChildrenState(AbstractObject* src, LocalVar* dest)
    {
        auto objChildrenIt = ValueAnalysis<ValueDomain, ValueStatePool>::globalState.childrenMap->find(src);
        if (objChildrenIt == ValueAnalysis<ValueDomain, ValueStatePool>::globalState.childrenMap->end()) {
            return;
        }
        auto& objChildren = objChildrenIt->second;
        const auto setChildState = [this, &objChildren](AbstractObject* child, size_t index) {
            auto& srcState =
                ValueAnalysis<ValueDomain, ValueStatePool>::globalState.programState.At(objChildren[index]);
            programState.emplace(child, srcState);
            if (srcState.GetKind() != ValueDomain::ValueKind::REF) {
                return;
            }
            if (auto ref = srcState.GetRef(); !ref->IsTopRefInstance() && refMap.find(ref) == refMap.end()) {
                auto& obj = ValueAnalysis<ValueDomain, ValueStatePool>::globalState.refMap.at(ref);
                refMap.try_emplace(ref, obj);
                CJC_ASSERT(std::holds_alternative<AbstractObject*>(obj));
                auto it = ValueAnalysis<ValueDomain, ValueStatePool>::globalState.programState.Find(
                    std::get<AbstractObject*>(obj));
                programState.Insert(it->first, it->second);
            }
        };
        if (auto it = childrenMap->find(dest); it != childrenMap->end()) {
            auto& destChildren = it->second;
            CJC_ASSERT(objChildren.size() == destChildren.size());
            for (size_t i = 0; i < destChildren.size(); ++i) {
                setChildState(destChildren[i], i);
            }
        } else {
            CreateChildren(dest, objChildren.size(), setChildState);
        }
    }

    bool RefMapJoin(const State<ValueDomain, ValueStatePool>& rhs)
    {
        const auto action = [this, &rhs](const Ref* key, std::variant<Ref*, AbstractObject*>& v1,
            const std::variant<Ref*, AbstractObject*>& v2) -> bool {
            (void)key;
            if (auto ref1 = std::get_if<Ref*>(&v1); ref1) {
                if ((*ref1)->IsTopRefInstance()) {
                    return false;
                }
                CJC_ASSERT(std::holds_alternative<Ref*>(v2));
                auto ref2 = std::get<Ref*>(v2);
                if (ref2->IsTopRefInstance()) {
                    v1 = Ref::GetTopRefInstance();
                    return true;
                }
                if (*ref1 != ref2) {
                    if ((*ref1)->IsEquivalent(ref2)) {
                        return false;
                    } else if (ref2->CanRepresent(*ref1)) {
                        v1 = ref2;
                        return true;
                    } else if ((*ref1)->CanRepresent(ref2)) {
                        return false;
                    } else {
                        v1 = MergeRef(*ref1, ref2, rhs.programState, rhs.refMap);
                        return true;
                    }
                }
            } else {
                CJC_ASSERT(std::holds_alternative<AbstractObject*>(v1));
                CJC_ASSERT(std::holds_alternative<AbstractObject*>(v2));
                CJC_ASSERT(std::get<AbstractObject*>(v1) == std::get<AbstractObject*>(v2));
            }
            return false;
        };

        return MapJoinTemplate<const Ref*, std::variant<Ref*, AbstractObject*>>(refMap, rhs.refMap, action);
    }

    Ref* MergeRef(Ref* lhs, Ref* rhs, const ValueStatePool& rhsProgramState, const RefMap& rhsRefMap)
    {
        const auto getAbsObject = [](const RefMap& refMap1, Ref* ref) -> AbstractObject* {
            auto it = refMap1.find(ref);
            if (auto val = std::get_if<AbstractObject*>(&it->second); val) {
                return *val;
            } else {
                return nullptr;
            }
        };

        auto newRef = CreateNewRef();
        newRef->AddRoots(lhs, rhs);
        auto lhsObj = getAbsObject(refMap, lhs);
        auto rhsObj = getAbsObject(rhsRefMap, rhs);
        CJC_ASSERT(lhsObj && rhsObj);

        auto newObj = CreateNewObject(GetObjName(absObjPool->size()));
        ValueDomain newAbsVal = programState.At(lhsObj); // should be a clone
        newAbsVal.Join(rhsProgramState.At(rhsObj));
        programState.emplace(newObj, ValueDomain(std::move(newAbsVal)));
        refMap.emplace(newRef, newObj);
        return newRef;
    }

private:
    ValueStatePool programState;
    RefMap refMap;
    ChildrenMap* childrenMap;
    AllocatedRefMap* allocatedRefMap;
    // only for inner ref of &&
    AllocatedRefMap* allocatedTwoLevelRefMap;
    AllocatedObjMap* allocatedObjMap;
    std::vector<std::unique_ptr<Ref>>* refPool;
    std::vector<std::unique_ptr<AbstractObject>>* absObjPool;
};

/**
 * @brief check if global var need analyse.
 * @tparam TDomain specific value domain.
 * @return true means need analyse.
 */
template <typename TDomain> inline bool IsTrackedGV(const GlobalVar& /* gv */)
{
    return true;
}

/**
 * @brief handle literal value of CHIR IR.
 * @tparam TDomain specific value domain.
 * @return domain related to literal value.
 */
template <typename TDomain> inline TDomain HandleNonNullLiteralValue(const LiteralValue* /* literal */)
{
    return TDomain(/* isTop = */ true);
}

/**
 * @brief abstract value analysis of ValueDomain.
 * @tparam ValueDomain specific ValueDomain
 */
template <typename ValueDomain, typename ValueStatePool = FullStatePool<ValueDomain>>
class ValueAnalysis : public Analysis<State<ValueDomain, ValueStatePool>> {
public:
    using isValueAnalysis = void;

    /// delete default construtor.
    ValueAnalysis() = delete;

    /**
     * @brief constructor for value analysis
     * @param func function to analyse
     * @param builder CHIR builder for generating IR.
     * @param isDebug flag whether print debug log.
     */
    ValueAnalysis(const Func* func, CHIRBuilder& builder, bool isDebug = false)
        : Analysis<State<ValueDomain, ValueStatePool>>(func, isDebug), builder(builder)
    {
    }

    virtual ~ValueAnalysis() override
    {
    }

    /**
     * @brief init all global variables.
     * @param package package to analyse.
     * @param builder CHIR builder for generating IR.
     */
    static void InitialiseLetGVState(const Package& package, CHIRBuilder& builder)
    {
        globalState.kind = ReachableKind::REACHABLE;
        for (auto gv : package.GetGlobalVars()) {
            if (!gv->TestAttr(Attribute::READONLY) || !gv->GetInitFunc() || !IsTrackedGV<ValueDomain>(*gv)) {
                continue;
            }
            auto realTy = StaticCast<RefType*>(gv->GetType())->GetBaseType();
            if (realTy->IsRef()) {
                auto ref = globalState.CreateNewRef(nullptr);
                globalState.programState.emplace(gv, ref);
                auto oRef = globalState.CreateNewRef(nullptr);
                globalState.refMap.emplace(ref, oRef);
                auto obj = globalState.CreateNewObject(GetObjName(globalAllocatedObjMap.size()), nullptr);
                globalState.refMap.emplace(oRef, obj);
                globalState.programState.emplace(obj, true);
            } else {
                auto obj = globalState.GetReferencedObjAndSetToTop(gv, nullptr);
                SetObjChildrenStateToTop(globalState, obj, realTy, builder);
            }
        }
    }

    /**
     * @brief get bottom of state
     * @return return bottom state.
     */
    State<ValueDomain, ValueStatePool> Bottom() final
    {
        return State<ValueDomain, ValueStatePool>(
            &childrenMap, &allocatedTwoLevelRefMap, &allocatedRefMap, &allocatedObjMap, &refPool, &absObjPool);
    }

    /**
     * @brief init function entry state.
     * @param state state to store function entry state.
     */
    void InitializeFuncEntryState(State<ValueDomain, ValueStatePool>& state) override
    {
        state.kind = ReachableKind::REACHABLE;
        for (auto param : this->func->GetParams()) {
            if (param->GetType()->IsRef() || param->GetType()->IsGeneric()) {
                auto refObj = state.GetReferencedObjAndSetToTop(param, nullptr);
                HandleFuncParam(state, param, refObj);
            } else {
                state.programState.emplace(param, /* isTop = */ true);
            }
        }
    }

    /**
     * @brief init lambda entry state.
     * @param state state to store lambda entry state.
     */
    void InitializeLambdaEntryState(State<ValueDomain, ValueStatePool>& state) override
    {
        CJC_ASSERT(this->currentLambda.has_value());
        for (auto param : this->currentLambda.value()->GetParams()) {
            if (param->GetType()->IsRef() || param->GetType()->IsGeneric()) {
                auto refObj = state.GetReferencedObjAndSetToTop(param, nullptr);
                HandleFuncParam(state, param, refObj);
            } else {
                state.programState.emplace(param, /* isTop = */ true);
            }
        }
    }

    /**
     * @brief init state of lambda capture vars.
     * @param state state to store lambda capture vars.
     * @param lambda analysed lambda.
     */
    void HandleVarStateCapturedByLambda(State<ValueDomain, ValueStatePool>& state, const Lambda* lambda) override
    {
        for (auto var : GetLambdaCapturedVarsRecursively(*lambda)) {
            state.SetSelfAndChildrenStateToTop(var);
        }
    }

    /**
     * @brief init state of lambda capture vars.
     * @param state state to store lambda capture vars.
     * @param lambda analysed lambda.
     */
    void PreHandleLambdaExpression(State<ValueDomain, ValueStatePool>& state, const Lambda* lambda) override
    {
        state.SetToBound(lambda->GetResult(), /* isTop = */ true);
    }

    /**
     * @brief propagate state in normal expression.
     * @param state state to store all domain.
     * @param expression normal expression to analyse domain.
     */
    void PropagateExpressionEffect(State<ValueDomain, ValueStatePool>& state, const Expression* expression) final
    {
        switch (expression->GetExprMajorKind()) {
            case ExprMajorKind::MEMORY_EXPR:
                PreHandleMemoryExpr(state, expression);
                break;
            case ExprMajorKind::UNARY_EXPR:
            case ExprMajorKind::BINARY_EXPR: {
                break;
            }
            case ExprMajorKind::OTHERS: {
                PreHandleOthersExpr(state, expression);
                break;
            }
            case ExprMajorKind::STRUCTURED_CTRL_FLOW_EXPR:
            default:
                InternalError("Unsupported major expr kind");
                break;
        }

        HandleNormalExpressionEffect(state, expression);
    }

    /**
     * @brief propagate state in terminators.
     * @param state state to store all domain.
     * @param terminator normal terminators to analyse domain.
     * @return blocks may goto after analysing.
     */
    std::optional<Block*> PropagateTerminatorEffect(
        State<ValueDomain, ValueStatePool>& state, const Terminator* terminator) override
    {
        switch (terminator->GetExprKind()) {
            case ExprKind::APPLY_WITH_EXCEPTION: {
                return PreHandleApplyExpr(state, StaticCast<const ApplyWithException*>(terminator));
            }
            case ExprKind::INVOKE_WITH_EXCEPTION: {
                return PreHandleInvokeExpr(state, StaticCast<const InvokeWithException*>(terminator));
            }
            case ExprKind::ALLOCATE_WITH_EXCEPTION: {
                auto allocate = StaticCast<const AllocateWithException*>(terminator);
                auto refObj = PreHandleAllocateExpr(state, allocate);
                return HandleAllocateWithExceptionTerminator(state, allocate, refObj);
            }
            case ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION: {
                return PreHandleRawArrayAllocate(state, StaticCast<const RawArrayAllocateWithException*>(terminator));
            }
            case ExprKind::INTRINSIC_WITH_EXCEPTION: {
                auto intrinsic = StaticCast<const IntrinsicWithException*>(terminator);
                if (intrinsic->GetIntrinsicKind() == CHIR::IntrinsicKind::INOUT_PARAM) {
                    return PreHandleInoutIntrinsic(state, intrinsic);
                } else {
                    break;
                }
            }
            default:
                break;
        }

        return HandleTerminatorEffect(state, terminator);
    }

    /// state of global variables.
    static State<ValueDomain, ValueStatePool> globalState;
    /// children map of global variables.
    static typename State<ValueDomain, ValueStatePool>::ChildrenMap globalChildrenMap;
    /// allocate ref map of global variables.
    static typename State<ValueDomain, ValueStatePool>::AllocatedRefMap globalAllocatedRefMap;
    /// allocate object map of global variables.
    static typename State<ValueDomain, ValueStatePool>::AllocatedObjMap globalAllocatedObjMap;
    /// all global reference
    static std::vector<std::unique_ptr<Ref>> globalRefPool;
    /// all global object
    static std::vector<std::unique_ptr<AbstractObject>> globalAbsObjPool;

protected:
    virtual void PreHandleGetElementRefExpr(State<ValueDomain, ValueStatePool>& state, const GetElementRef* getElemRef)
    {
        auto dest = getElemRef->GetResult();
        auto destIt = state.programState.Find(dest);
        if (destIt == state.programState.End()) {
            auto destRef = state.CreateNewRef(getElemRef);
            state.programState.emplace(dest, destRef);
            state.refMap.emplace(destRef, FindTargetElement(state, getElemRef));
        } else {
            CJC_ASSERT(destIt->second.GetKind() == ValueDomain::ValueKind::REF);
            auto destRef = destIt->second.GetRef();
            auto destRefIt = state.refMap.find(destRef);
            CJC_ASSERT(destRefIt != state.refMap.end());
            destRefIt->second = FindTargetElement(state, getElemRef);
        }
    }

    virtual void PreHandleFieldExpr(State<ValueDomain, ValueStatePool>& state, const Field* field)
    {
        auto dest = field->GetResult();
        auto indexes = field->GetPath();
        if (indexes.size() > 1) {
            if (auto it = state.programState.Find(dest); it == state.programState.End()) {
                state.InitToTopOrTopRef(dest, dest->GetType()->IsRef());
            }
            return;
        }
        if (auto it = childrenMap.find(field->GetBase()); it != childrenMap.end()) {
            auto& children = it->second;
            if (indexes[0] < children.size()) {
                return state.PropagateWithoutChildren(children[indexes[0]], field->GetResult());
            }
        }
        state.SetToTopOrTopRef(dest, dest->GetType()->IsRef());
    }

private:
    void PreHandleMemoryExpr(State<ValueDomain, ValueStatePool>& state, const Expression* expression)
    {
        switch (expression->GetExprKind()) {
            case ExprKind::ALLOCATE: {
                auto allocate = StaticCast<const Allocate*>(expression);
                auto refObj = PreHandleAllocateExpr(state, allocate);
                HandleAllocateExpr(state, allocate, refObj);
                break;
            }
            case ExprKind::LOAD: {
                PreHandleLoadExpr(state, StaticCast<const Load*>(expression));
                break;
            }
            case ExprKind::STORE: {
                PreHandleStoreExpr(state, StaticCast<const Store*>(expression));
                break;
            }
            case ExprKind::GET_ELEMENT_REF: {
                PreHandleGetElementRefExpr(state, StaticCast<const GetElementRef*>(expression));
                break;
            }
            case ExprKind::STORE_ELEMENT_REF: {
                PreHandleStoreElementRefExpr(state, StaticCast<const StoreElementRef*>(expression));
                break;
            }
            default: {
                InternalError("Unexpected memory expr");
                break;
            }
        }
    }

    bool PreHandleOthersExpr(State<ValueDomain, ValueStatePool>& state, const Expression* expression)
    {
        switch (expression->GetExprKind()) {
            case ExprKind::CONSTANT: {
                PreHandleConstantExpr(state, StaticCast<const Constant*>(expression));
                return true;
            }
            case ExprKind::DEBUGEXPR: {
                return true;
            }
            case ExprKind::TUPLE: {
                PreHandleTupleExpr(state, StaticCast<const Tuple*>(expression));
                return true;
            }
            case ExprKind::FIELD: {
                PreHandleFieldExpr(state, StaticCast<const Field*>(expression));
                return true;
            }
            case ExprKind::APPLY: {
                PreHandleApplyExpr(state, StaticCast<const Apply*>(expression));
                return true;
            }
            case ExprKind::INVOKE: {
                PreHandleInvokeExpr(state, StaticCast<const Invoke*>(expression));
                return true;
            }
            case ExprKind::TYPECAST: {
                auto cast = StaticCast<TypeCast*>(expression);
                if (!cast->Get<NeedCheckCast>()) {
                    return PreHandleNonCheckedTypeCast(state, cast);
                }
                return false;
            }
            case ExprKind::RAW_ARRAY_ALLOCATE: {
                PreHandleRawArrayAllocate(state, StaticCast<const RawArrayAllocate*>(expression));
                return true;
            }
            case ExprKind::INTRINSIC: {
                auto intrinsic = StaticCast<const Intrinsic*>(expression);
                if (intrinsic->GetIntrinsicKind() == CHIR::IntrinsicKind::INOUT_PARAM) {
                    PreHandleInoutIntrinsic(state, intrinsic);
                    return true;
                } else {
                    return false;
                }
            }
            default: {
                return false;
            }
        }
    }

    void PreHandleConstantExpr(State<ValueDomain, ValueStatePool>& state, const Constant* constant)
    {
        auto dest = constant->GetResult();
        if (state.programState.Find(dest) != state.programState.End()) {
            return;
        }
        if (constant->IsConstantNull()) {
            if (dest->GetType()->IsRef()) {
                state.programState.emplace(dest, Ref::GetTopRefInstance());
            } else {
                state.programState.emplace(dest, /* isTop = */ true);
            }
        } else {
            state.programState.emplace(dest, HandleNonNullLiteralValue<ValueDomain>(constant->GetValue()));
        }
    }

    template <typename TElemRef> AbstractObject* FindTargetElement(
        State<ValueDomain, ValueStatePool>& state, const TElemRef* elemRef)
    {
        auto loc = elemRef->GetLocation();
        if (loc->IsGlobal() || loc->TestAttr(Attribute::STATIC)) {
            return AbstractObject::GetTopObjInstance();
        }

        auto& paths = elemRef->GetPath();
        if (paths.size() > 1) {
            return AbstractObject::GetTopObjInstance();
        }

        auto locIt = state.programState.Find(loc);
        if (locIt == state.programState.End()) {
            if (this->isDebug) {
                std::cout << "Value Analysis: use-before-initialization detected";
            }
            return AbstractObject::GetTopObjInstance();
        }
        auto& locVal = locIt->second;
        CJC_ASSERT(locVal.GetKind() == ValueDomain::ValueKind::REF);
        auto locRef = locVal.GetRef();
        if (locRef->IsTopRefInstance()) {
            return AbstractObject::GetTopObjInstance();
        }
        auto refIt = state.refMap.find(locRef);
        CJC_ASSERT(refIt != state.refMap.end());
        CJC_ASSERT(std::holds_alternative<AbstractObject*>(refIt->second));
        auto rootObj = std::get<AbstractObject*>(refIt->second);
        auto childrenIt = state.childrenMap->find(rootObj);
        if (childrenIt == state.childrenMap->end() || paths[0] >= childrenIt->second.size()) {
            return AbstractObject::GetTopObjInstance();
        }
        return childrenIt->second[paths[0]];
    }

    void PreHandleStoreExpr(State<ValueDomain, ValueStatePool>& state, const Store* store)
    {
        /*
         * We are storing to a Ref. And there are two cases (can be splitted into four).
         * The first case (a&b) is that we are trying to put a new Ref to the location of the Store
         * expression, which means the location of the Store (a Ref) stores a Ref or a Bottom.
         * Thus, we just need to replace the old Ref or the old Bottom.
         *
         * The second case (c&d) is that the Ref is referencing an AbstractObject, whicn means we
         * need to update the abstract value of this AbstractObject in `programState`. If this
         * AbstractObject is a compound data structure, we also need to update the abstract value
         * of its children.
         *
         * a) class CA { var x = 1 }; ... var ca = CA() ...
         *  IR: %0 : Class-CA& = Allocate(Class-CA)
         *      %1 : Unit = Apply(init, %0)
         *      %ca : Class-CA&& = Allocate(Class-CA&)      // currently, %ca |-> Ref0.
         *      %3 : Unit = Store(%0 : Class-CA&, %ca : Class-CA&&)
         *  programState: { %0 |-> Ref0, %ca |-> Ref1, Obj0 |-> Top, Obj0.x |-> 1 }
         *  refMap: { Ref1 |-> Ref2, Ref0 |-> Obj0 }
         *
         * b) class CA { var x = 1 }; ... var ca = CA(); ca = CA() ...
         *  IR: %0 : Class-CA& = Allocate(Class-CA)
         *      %1 : Unit = Apply(init, %0)
         *      %3 : Unit = Store(%0, %ca : Class-CA&&)     // currently, %ca |-> Ref1, Ref1 |-> Ref0.
         *  programState: { %0 |-> Ref2, Obj1 |-> Top, Obj1.x |-> 1, %ca |-> Ref1, Obj0 |-> Top, Obj0.x |-> 1 }
         *  refMap: { Ref2 |-> Obj1, Ref1 |-> Ref2, Ref0 |-> Obj0 }
         *
         * c) class CA { var x = 1 }; ... var ca = CA(); ca.x = 2 ...
         *  IR: %0 : Int64 = ConstantInt(2)
         *      %1 : Class-CA& = Load(%ca : Class-CA&&)
         *      %2 : Int64& = GetElementRef(%1, 0)
         *      %3 : Unit = Store(%0, %2)
         *  programState: { %2 |-> Ref2, %1 |-> Ref0, %0 |-> 2, %ca |-> Ref1, Obj0 |-> Top, Obj0.x |-> 2 }
         *  refMap: { Ref2 |-> Obj0.x, Ref1 |-> Ref0, Ref0 |-> Obj0 }
         *
         * d) struct SA { var y = 2 }; ... var sa = SA(); sa = SA() ...
         *  IR: %0 : SA& = Allocate(SA)
         *      %1 : Unit = Apply(init, %0 : SA&)
         *      %2 : SA = Load(%0 : SA&)
         *      %3 : Unit = Store(%2 : SA, %sa : SA&)
         *  programState: { %2 |-> Top, %2.y |-> 2, %0 |-> Ref1, Obj1 |-> Top, Obj1.y |-> 2,
         *                  %sa |-> Ref0, Obj0 |-> Top, Obj0.y |-> 2 }
         *  refMap: { Ref1 |-> Obj1, Ref0 |-> Obj0 }
         */
        auto value = store->GetValue();
        if (value->IsGlobal() || value->TestAttr(Attribute::STATIC)) {
            return;
        }
        auto location = store->GetLocation();
        if (location->IsGlobal() || location->TestAttr(Attribute::STATIC)) {
            return HandleStoreToGlobal(state, location, value);
        }
        auto valIt = state.programState.Find(value);
        auto locIt = state.programState.Find(location);
        if (is_instance_of_v<ValueStatePool, ActiveStatePool>) {
            if (valIt == state.programState.End() || locIt == state.programState.End()) {
                // dead state will delete in active state mode
                state.SetToTopOrTopRef(location, true);
                return;
            }
        }
        CJC_ASSERT(valIt != state.programState.End());
        CJC_ASSERT(locIt != state.programState.End());
        auto& locVal = locIt->second;
        CJC_ASSERT(locVal.GetKind() == ValueDomain::ValueKind::REF);
        auto locRef = locVal.GetRef();
        if (locRef->IsTopRefInstance()) {
            return;
        }
        if (auto it = state.refMap.find(locRef); it != state.refMap.end()) {
            auto& locRefVal = it->second;
            if (std::holds_alternative<AbstractObject*>(locRefVal)) {
                if (auto dest = std::get<AbstractObject*>(locRefVal); !dest->IsTopObjInstance()) {
                    // c, d)
                    CJC_ASSERT(location->IsLocalVar());
                    if (StaticCast<LocalVar*>(location)->GetExpr()->GetExprKind() == ExprKind::GET_ELEMENT_REF) {
                        state.PropagateWithoutChildren(value, dest);
                    } else {
                        state.Propagate(value, dest);
                    }
                }
            } else {
                // b
                auto& toBeStored = valIt->second;
                CJC_ASSERT(toBeStored.GetKind() == ValueDomain::ValueKind::REF);
                it->second = const_cast<Ref*>(toBeStored.GetRef());
            }
        } else {
            // a)
            auto& toBeStored = valIt->second;
            CJC_ASSERT(toBeStored.GetKind() == ValueDomain::ValueKind::REF);
            state.refMap.emplace(locRef, const_cast<Ref*>(toBeStored.GetRef()));
        }
    }

    void HandleStoreToGlobal(State<ValueDomain, ValueStatePool>& state, Value* location, Value* value)
    {
        if (this->isStable || !location->IsGlobalVarInCurPackage()) {
            return;
        }
        auto gv = VirtualCast<GlobalVar*>(location);
        if (!gv->TestAttr(Attribute::READONLY) || globalState.programState.Find(gv) == globalState.programState.End()) {
            return;
        }
        auto targetObj = globalState.CheckAbstractObjectRefBy(gv);
        CJC_NULLPTR_CHECK(targetObj);
        if (gv->GetType()->GetRefDims() == 1) {
            state.PropagateWithoutChildren(value, targetObj, &globalState.programState);
            state.StoreGVChildrenState(value, targetObj);
        } else {
            // The global var must be a class / raw array, and we don't record children of them.
            CJC_ASSERT(globalState.GetChildren(targetObj).empty());
            if (auto srcObj = state.CheckAbstractObjectRefBy(value); srcObj) {
                state.PropagateWithoutChildren(srcObj, targetObj, &globalState.programState);
            } else {
                globalState.Update(targetObj, /* isTop = */ true);
            }
        }
    }

    void PreHandleLoadExpr(State<ValueDomain, ValueStatePool>& state, const Load* load)
    {
        /*
         * We are loading from a Ref. Similar to the store expression, there are also two cases here.
         * The first case (a) is loading from a two-level Ref. Thus, what we get is just another
         * one-level Ref.
         *
         * The second case (b,c) is loading from an one-level Ref, which means it stores a value
         * instead of a Ref. Therefore, we will copy the abstract value of the abstract object
         * referenced by the Ref. If this AbstractObject is a compound data structure, we also need
         * to copy the abstract value of its children.
         *
         * a,b) class CA { var x = 1 }; ... var ca = CA(); ca.x ...
         *  IR: %0 : Class-CA& = Load(%ca : Class-CA&&)         the first Load
         *      %1 : Int64& = GetElementRef(%0 : Class-CA&, 0)
         *      %2 : Int64 = Load(%1 : Int64&)                  the second Load
         *  programState: { %2 |-> 1, %1 |-> Ref2, %0 |-> Ref0, %ca |-> Ref1, Obj0 |-> Top, Obj0.x |-> 1 }
         *  refMap: { Ref2 |-> Obj0.x, Ref1 |-> Ref0, Ref0 |-> Obj0 }
         *
         * c)   struct SA { var y = 2 }; ... var sa = SA(); sa = SA() ...
         *  IR: %0 : SA& = Allocate(SA)
         *      %1 : Unit = Apply(init, %0 : SA&)
         *      %2 : SA = Load(%0 : SA&)
         *      ...
         *  programState: { %2 |-> Top, %2.y |-> 2, %0 |-> Ref1, Obj1 |-> Top, Obj1.y |-> 2,
         *                  %sa |-> Ref0, Obj0 |-> Top, Obj0.y |-> 2 }
         *  refMap: { Ref1 |-> Obj1, Ref0 |-> Obj0 }
         */

        auto loc = load->GetLocation();
        if (loc->IsGlobal() || loc->TestAttr(Attribute::STATIC)) {
            return HandleLoadFromGlobal(state, load);
        }
        auto dest = load->GetResult();
        auto locIt = state.programState.Find(loc);
        auto& locVal = locIt->second;
        if (is_instance_of_v<ValueStatePool, ActiveStatePool>) {
            if (locIt == state.programState.End()) {
                // dead state will delete in active state mode
                state.SetToTopOrTopRef(dest, dest->GetType()->IsRef());
                return;
            }
        }
        CJC_ASSERT(locIt != state.programState.End());
        CJC_ASSERT(locVal.GetKind() == ValueDomain::ValueKind::REF);
        if (locVal.GetRef()->IsTopRefInstance()) {
            return state.SetToTopOrTopRef(dest, dest->GetType()->IsRef());
        }
        auto refIt = state.refMap.find(locVal.GetRef());
        if (refIt == state.refMap.end()) {
            if (this->isDebug) {
                std::cout << "Value Analysis: use-before-initialization detected";
            }
            return;
        }
        auto& refVal = refIt->second;
        if (std::holds_alternative<Ref*>(refVal)) {
            // a)
            CJC_ASSERT(dest->GetType()->IsRef());
            return state.Update(dest, std::get<Ref*>(refVal));
        } else {
            auto obj = std::get<AbstractObject*>(refVal);
            if (obj->IsTopObjInstance()) {
                // %0 = GetElementRef(...)      %0 -> Refx, Refx -> TopObj
                // %1 = Load(%0)                %1 -> Top/TopRef depends on the type
                return state.SetToTopOrTopRef(dest, dest->GetType()->IsRef());
            } else {
                // b,c)
                return state.Propagate(std::get<AbstractObject*>(refVal), dest);
            }
        }
    }

    void HandleLoadFromGlobal(State<ValueDomain, ValueStatePool>& state, const Load* load)
    {
        auto dest = load->GetResult();
        if (state.programState.Find(dest) != state.programState.End()) {
            return;
        }
        auto loc = load->GetLocation();
        if (!loc->IsGlobalVarInCurPackage()) {
            return state.InitToTopOrTopRef(dest, dest->GetType()->IsRef());
        }
        auto globalVar = VirtualCast<GlobalVar*>(loc);
        if (!globalVar->TestAttr(Attribute::READONLY)) {
            return state.InitToTopOrTopRef(dest, dest->GetType()->IsRef());
        }
        auto initializer = globalVar->GetInitializer();
        if (initializer) {
            CJC_ASSERT(!initializer->IsNullLiteral());
            state.programState.emplace(dest, HandleNonNullLiteralValue<ValueDomain>(initializer));
        } else {
            auto gvIt = globalState.programState.Find(globalVar);
            if (gvIt == globalState.programState.End()) {
                return state.InitToTopOrTopRef(dest, dest->GetType()->IsRef());
            }
            auto gRefIt = globalState.refMap.find(gvIt->second.GetRef());
            CJC_ASSERT(gRefIt != globalState.refMap.end());
            if (std::holds_alternative<Ref*>(gRefIt->second)) {
                // The global var must be a class / raw array, and we don't record children of them.
                // gv_a -> sRef1, sRef1 -> sRef2, sRef2 -> Obj0, Obj0 -> Class-CA
                // %0 = Load(gv_a), %0 -> sRef2, sRef2 -> Obj0
                auto oRef = std::get<Ref*>(gRefIt->second);
                state.programState.emplace(dest, oRef);
                auto oRefIt = globalState.refMap.find(oRef);
                CJC_ASSERT(oRefIt != globalState.refMap.end());
                auto obj = std::get<AbstractObject*>(oRefIt->second);
                state.refMap.emplace(oRef, obj);
                auto objIt = globalState.programState.Find(obj);
                CJC_ASSERT(objIt != globalState.programState.End());
                state.programState.emplace(obj, objIt->second);
            } else {
                auto obj = std::get<AbstractObject*>(gRefIt->second);
                globalState.PropagateWithoutChildren(obj, dest, &state.programState);
                state.LoadGVChildrenState(obj, dest);
            }
        }
    }

    void PreHandleStoreElementRefExpr(State<ValueDomain, ValueStatePool>& state, const StoreElementRef* storeElemRef)
    {
        auto value = storeElemRef->GetValue();
        if (value->IsGlobal() || value->TestAttr(Attribute::STATIC)) {
            return;
        }
        auto targetObj = FindTargetElement(state, storeElemRef);
        if (targetObj->IsTopObjInstance()) {
            return;
        }
        state.PropagateWithoutChildren(value, targetObj);
    }

    template <typename TAllocate>
    Value* PreHandleAllocateExpr(State<ValueDomain, ValueStatePool>& state, const TAllocate* allocate)
    {
        // In our framework, we associate a *single* memory location with every static allocation site.
        // That is, when we meet an Allocation expression again, we won't come up with a new Ref, instead
        // we will keep the same old Ref.
        // The reason that we don't generate a new fresh Ref is, the iteration won't be converge. If the
        // program has a loop, the analysis will keep going around the loop, generating new memory locations,
        // getting back to the top of the loop with a new input value. Thus, we'll need to rerun the analysis
        // for the loop again, and this will never stop.
        // e.g.
        //  approach (a): Generate a new Ref everytime
        //  while() {           round1          round2          round3
        //      var a = CA()    a |-> Ref1      a |-> Ref2      a |-> Ref3      ...BOOM!
        //  }
        //  approach (b): Generate a single Ref everytime
        //  while() {           round1          round2
        //      var a = CA()    a |-> Ref1      a |-> Ref       ...stable :-D
        //  }
        auto dest = allocate->GetResult();
        auto allocatedTy = allocate->GetType();
        if (allocatedTy->IsRef()) {
            // %0 : Class-CA&& = Allocate(Class-CA&)
            // programState: { %0 |-> Ref1 }
            state.programState.emplace(dest, state.CreateNewRef(allocate));
            return nullptr;
        } else {
            // struct SA { var x = 1; var y = CA() }
            // %0 : Struct-SA& = Allocate(Struct-SA)
            // programState: { %0 |-> Ref0, Obj0 |-> Top, Obj0.0 -> Top, Obj0.1 -> TopRef },
            //               where Obj0.0 represents member `x`, Obj0.1 represents member `y`
            // refMap: { Ref0 |-> Obj0 }
            auto obj = state.GetReferencedObjAndSetToTop(dest, allocate);
            SetObjChildrenStateToTop(state, obj, allocatedTy, builder);
            return obj;
        }
    }

    template <typename TApply> std::optional<Block*>
    PreHandleApplyExpr(State<ValueDomain, ValueStatePool>& state, const TApply* apply)
    {
        // check if this apply is a call to a mut func of a struct
        if (auto callee = apply->GetCallee(); callee->TestAttr(Attribute::MUT)) {
            auto structArg = apply->GetArgs()[0];
            auto structTy = StaticCast<RefType*>(structArg->GetType())->GetBaseType();
            if (structTy->IsStruct()) {
                if (auto obj = state.CheckAbstractObjectRefBy(structArg); obj) {
                    if (childrenMap.find(obj) != childrenMap.end()) {
                        SetObjChildrenStateToTop(state, obj, structTy, builder);
                    }
                }
            } else {
                CJC_ASSERT(structTy->IsClass()); // actually interface in Cangjie
            }
        }

        auto refObj = PreHandleFuncCall(state, apply);
        if constexpr (std::is_same_v<Apply, TApply>) {
            HandleApplyExpr(state, apply, refObj);
            return std::nullopt;
        } else {
            return HandleApplyWithExceptionTerminator(state, apply, refObj);
        }
    }

    template <typename TInvoke>
    std::optional<Block*> PreHandleInvokeExpr(State<ValueDomain, ValueStatePool>& state, const TInvoke* invoke)
    {
        auto refObj = PreHandleFuncCall(state, invoke);

        if constexpr (std::is_same_v<Invoke, TInvoke>) {
            HandleInvokeExpr(state, invoke, refObj);
            return std::nullopt;
        } else {
            return HandleInvokeWithExceptionTerminator(state, invoke, refObj);
        }
    }

    template <typename T>
    Value* PreHandleFuncCall(State<ValueDomain, ValueStatePool>& state, const T* apply)
    {
        auto dest = apply->GetResult();
        auto ty = dest->GetType();
        if (ty->IsRef() || ty->IsGeneric()) {
            return state.GetReferencedObjAndSetToTop(dest, apply);
        } else {
            state.SetToBound(dest, /* isTop = */ true);
            SetObjChildrenStateToTop(state, dest, ty, builder);
            return nullptr;
        }
    }

    static void SetObjChildrenStateToTop(
        State<ValueDomain, ValueStatePool>& state, Value* root, Type* rootTy, CHIRBuilder& builder)
    {
        std::vector<bool> childrenTypes;
        if (rootTy->GetTypeKind() == Type::TypeKind::TYPE_STRUCT) {
            auto structTy = StaticCast<StructType*>(rootTy);
            auto structDef = structTy->GetStructDef();
            if (structTy->IsStructArray()) {
                childrenTypes = {true, false, false};
            } else if (!structDef->IsCStruct()) {
                for (auto ty : structTy->GetInstantiatedMemberTys(builder)) {
                    childrenTypes.emplace_back(ty->IsRef());
                }
            }
        } else if (rootTy->GetTypeKind() == Type::TypeKind::TYPE_TUPLE) {
            auto tupleTy = StaticCast<TupleType*>(rootTy);
            for (auto ty : tupleTy->GetElementTypes()) {
                childrenTypes.emplace_back(ty->IsRef());
            }
        } else if (rootTy->GetTypeKind() == Type::TypeKind::TYPE_ENUM) {
            // We only focus the state of index of an enum. And its type is not a ref type.
            childrenTypes.emplace_back(false);
        } else if (rootTy->GetTypeKind() == Type::TypeKind::TYPE_CLASS) {
            auto classTy = StaticCast<ClassType*>(rootTy);
            auto classDef = classTy->GetClassDef();
            if (classDef->GetSrcCodeIdentifier().find("$BOX_RNat5Array") == 0) {
                childrenTypes.emplace_back(classDef->GetInstanceVar(0).type->IsRef());
            }
        }

        if (auto it = state.childrenMap->find(root); it != state.childrenMap->end()) {
            auto& children = it->second;
            CJC_ASSERT(children.size() == childrenTypes.size());
            for (size_t i = 0; i < children.size(); ++i) {
                state.SetToTopOrTopRef(children[i], childrenTypes[i]);
            }
        } else {
            const auto setChildState = [&state, &childrenTypes](AbstractObject* child, size_t index) {
                state.SetToTopOrTopRef(child, childrenTypes[index]);
            };
            state.CreateChildren(root, childrenTypes.size(), setChildState);
        }
    }

    void PreHandleTupleExpr(State<ValueDomain, ValueStatePool>& state, const Tuple* tuple)
    {
        state.SetToBound(tuple->GetResult(), /* isTop = */ true);

        auto operands = tuple->GetOperands();
        auto operandNum = tuple->GetResult()->GetType()->IsEnum() ? 1 : operands.size();
        if (auto it = childrenMap.find(tuple->GetResult()); it != childrenMap.end()) {
            auto& children = it->second;
            CJC_ASSERT(children.size() == operandNum);
            for (size_t i = 0; i < operandNum; ++i) {
                state.PropagateWithoutChildren(operands[i], children[i]);
            }
        } else {
            const auto setChildState = [&state, &operands](AbstractObject* child, size_t index) {
                state.PropagateWithoutChildren(operands[index], child);
            };
            state.CreateChildren(tuple->GetResult(), operandNum, setChildState);
        }
    }

    bool PreHandleNonCheckedTypeCast(State<ValueDomain, ValueStatePool>& state, const TypeCast* cast)
    {
        auto dest = cast->GetResult();
        if (dest->GetUsers().size() == 1U && dest->GetUsers()[0]->GetExprKind() == ExprKind::MULTIBRANCH) {
            // var x = 34; match (x) { ... }
            // There will be a 'TypeCast(%x, UInt64)' but its checkTypeCast is false.
            return false;
        }
        auto ty = dest->GetType();
        if (ty->IsRef()) {
            const auto getClassDef = [](const Type& ty) {
                if (!ty.IsRef()) {
                    return static_cast<ClassDef*>(nullptr);
                }
                auto baseTy = StaticCast<const RefType&>(ty).GetBaseType();
                if (!baseTy->IsClass()) {
                    return static_cast<ClassDef*>(nullptr);
                }
                return StaticCast<ClassType*>(baseTy)->GetClassDef();
            };

            auto targetClassDef = getClassDef(*ty);
            bool isCollection = targetClassDef && targetClassDef->GetSrcCodeIdentifier() == "Collection" &&
                targetClassDef->GetPackageName() == Cangjie::CORE_PACKAGE_NAME;
            auto sourceClassDef = getClassDef(*cast->GetSourceTy());
            bool isBoxArray = sourceClassDef &&
                sourceClassDef->GetSrcCodeIdentifier().find(Cangjie::BOX_DECL_PREFIX) == 0 &&
                sourceClassDef->GetAllInstanceVarNum() == 1 && sourceClassDef->GetInstanceVar(0).type->IsStructArray();
            if (isBoxArray && isCollection) {
                return false;
            }

            state.GetReferencedObjAndSetToTop(dest, cast);
        } else {
            state.SetToBound(dest, /* isTop = */ true);
            SetObjChildrenStateToTop(state, dest, ty, builder);
        }
        return true;
    }

    template <typename TRawArrayAllocate>
    std::optional<Block*> PreHandleRawArrayAllocate(
        State<ValueDomain, ValueStatePool>& state, const TRawArrayAllocate* allocate)
    {
        state.GetReferencedObjAndSetToTop(allocate->GetResult(), allocate);
        return std::nullopt;
    }

    template <typename TIntrinsic>
    std::optional<Block*> PreHandleInoutIntrinsic(
        State<ValueDomain, ValueStatePool>& state, const TIntrinsic* intrinsic)
    {
        auto param = intrinsic->GetOperand(0);
        if (!param->IsLocalVar()) {
            return std::nullopt;
        }
        state.SetSelfAndChildrenStateToTop(param);
        return std::nullopt;
    }

    // ============ functions that need to be implemented by a concrete analysis ============ //
    virtual void HandleFuncParam(State<ValueDomain, ValueStatePool>& state, Parameter* param, Value* refObj)
    {
        (void)state;
        (void)param;
        (void)refObj;
    }

    virtual void HandleAllocateExpr(
        State<ValueDomain, ValueStatePool>& state, const Allocate* expression, Value* refObj)
    {
        (void)state;
        (void)expression;
        (void)refObj;
    }

    virtual std::optional<Block*> HandleAllocateWithExceptionTerminator(
        State<ValueDomain, ValueStatePool>& state, const AllocateWithException* allocate, Value* refObj)
    {
        (void)state;
        (void)allocate;
        (void)refObj;
        return std::nullopt;
    }

    virtual void HandleApplyExpr(State<ValueDomain, ValueStatePool>& state, const Apply* apply, Value* refObj)
    {
        (void)state;
        (void)apply;
        (void)refObj;
    }

    virtual std::optional<Block*> HandleApplyWithExceptionTerminator(
        State<ValueDomain, ValueStatePool>& state, const ApplyWithException* apply, Value* refObj)
    {
        (void)state;
        (void)apply;
        (void)refObj;
        return std::nullopt;
    }

    virtual void HandleInvokeExpr(State<ValueDomain, ValueStatePool>& state, const Invoke* invoke, Value* refObj)
    {
        (void)state;
        (void)invoke;
        (void)refObj;
    }

    virtual std::optional<Block*> HandleInvokeWithExceptionTerminator(
        State<ValueDomain, ValueStatePool>& state, const InvokeWithException* invoke, Value* refObj)
    {
        (void)state;
        (void)invoke;
        (void)refObj;
        return std::nullopt;
    }

    virtual void HandleNormalExpressionEffect(State<ValueDomain, ValueStatePool>& state, const Expression* expression)
    {
        (void)state;
        (void)expression;
    }

    virtual std::optional<Block*> HandleTerminatorEffect(
        State<ValueDomain, ValueStatePool>& state, const Terminator* terminator)
    {
        (void)state;
        (void)terminator;
        return std::nullopt;
    }

    typename State<ValueDomain, ValueStatePool>::ChildrenMap childrenMap;
    typename State<ValueDomain, ValueStatePool>::AllocatedRefMap allocatedRefMap;
    typename State<ValueDomain, ValueStatePool>::AllocatedRefMap allocatedTwoLevelRefMap;
    typename State<ValueDomain, ValueStatePool>::AllocatedObjMap allocatedObjMap;
    std::vector<std::unique_ptr<Ref>> refPool;
    std::vector<std::unique_ptr<AbstractObject>> absObjPool;

protected:
    CHIRBuilder& builder;
}; // namespace Cangjie::CHIR

} // namespace Cangjie::CHIR

#endif
