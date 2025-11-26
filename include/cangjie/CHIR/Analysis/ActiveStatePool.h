// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_ACTIVE_STATE_POOL_H
#define CANGJIE_CHIR_ANALYSIS_ACTIVE_STATE_POOL_H

#include <unordered_map>

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Analysis/ValueDomain.h"

namespace Cangjie::CHIR {

/**
 * @file ActiveStatePool.h
 * @brief Containers for mapping CHIR Value pointers to ValueDomain instances.
 *
 * This header provides two pool implementations:
 * - FullStatePool: a thin wrapper around std::unordered_map<Value*, ValueDomain>.
 * - ActiveStatePool: a bounded pool that keeps insertion order via a doubly-linked
 *   list (ActiveStateNode) and evicts oldest entries when capacity is exceeded.
 *
 * Both pools expose a similar API (Begin/End/Find/Insert/At/Join/emplace) so they
 * can be used interchangeably for value analysis state storage.
 */

/**
 * @brief Default (unbounded) state pool.
 *
 * Lightweight mapping Value* -> ValueDomain backed by std::unordered_map.
 * Provides basic iteration, lookup and join utilities.
 *
 * @tparam ValueDomain Domain type stored for each Value pointer.
 */
template <typename ValueDomain>
class FullStatePool {
public:
    using ConstIterator = typename std::unordered_map<Value*, ValueDomain>::const_iterator;
    using Iterator = typename std::unordered_map<Value*, ValueDomain>::iterator;

    /** @name Iteration */ /// @{
    ConstIterator Begin() const
    {
        return data.begin();
    }

    ConstIterator End() const
    {
        return data.end();
    }

    Iterator Begin()
    {
        return data.begin();
    }
    
    Iterator End()
    {
        return data.end();
    }
    /// @}

    /** @name Lookup */ /// @{
    /**
     * @brief Find mapping for @p value.
     * @return const_iterator to entry or End().
     */
    ConstIterator Find(Value* value) const
    {
        return data.find(value);
    }

    /**
     * @brief Find mapping for @p value (non-const).
     * @return iterator to entry or End().
     */
    Iterator Find(Value* value)
    {
        return data.find(value);
    }
    /// @}

    /**
     * @brief Insert a mapping value -> domain.
     * @param value pointer key
     * @param domain stored domain
     */
    void Insert(Value* value, ValueDomain domain)
    {
        data.emplace(value, std::move(domain));
    }

    /**
     * @brief Access the domain for @p value. Throws if not present.
     * @return reference to ValueDomain
     */
    ValueDomain& At(Value* value)
    {
        return data.at(value);
    }
    
    const ValueDomain& At(Value* value) const
    {
        return data.at(value);
    }

    /**
     * @brief Join another FullStatePool into this one.
     * @param rhs right-hand side pool
     * @return true if any entry in this pool changed as a result of the join.
     */
    bool Join(const FullStatePool<ValueDomain>& rhs)
    {
        return MapJoin<Value*, ValueDomain>(data, rhs.data);
    }

    /**
     * @brief Perfect-forwarding emplace into underlying map.
     */
    template<typename... Args>
    std::pair<Iterator, bool> emplace(Args&&... args)
    {
        return data.emplace(args...);
    }

private:
    std::unordered_map<Value*, ValueDomain> data;
};

/**
 * @brief MapJoin adaptor for FullStatePool to enable generic join usage.
 * @tparam Domain domain type, must derive from AbstractDomain<Domain>.
 */
template <typename Domain, typename = std::enable_if_t<std::is_base_of_v<AbstractDomain<Domain>, Domain>>>
bool MapJoin(FullStatePool<Domain>& lhs, const FullStatePool<Domain>& rhs)
{
    return lhs.Join(rhs);
}

/**
 * @brief Doubly-linked node used by ActiveStatePool to track insertion order.
 */
struct ActiveStateNode {
    ActiveStateNode() {}
    explicit ActiveStateNode(Value* value) : value(value) {}
    ActiveStateNode* prev = nullptr; /**< previous (older) node */
    ActiveStateNode* next = nullptr; /**< next (newer) node */

    Value* value = nullptr; /**< associated Value pointer */
};

/**
 * @brief Bounded active state pool with eviction and insertion-order tracking.
 *
 * ActiveStatePool stores a mapping Value* -> ValueDomain and maintains a
 * doubly-linked list of ActiveStateNode to record insertion order. When the
 * number of entries exceeds MAX_STATE_POOL_SIZE the oldest entries are evicted
 * until the size reaches BASE_STATE_POOL_SIZE.
 *
 * The API mirrors FullStatePool and adds Insert/emplace that return the
 * ActiveStateNode* for the inserted value.
 *
 * @tparam ValueDomain domain type stored per Value; must derive from AbstractDomain<ValueDomain>.
 */
template <typename ValueDomain>
class ActiveStatePool {
public:
    using ConstIterator = typename std::unordered_map<Value*, ValueDomain>::const_iterator;
    using Iterator = typename std::unordered_map<Value*, ValueDomain>::iterator;

    /**
     * @brief Construct an empty ActiveStatePool.
     */
    explicit ActiveStatePool()
    {
    }

    /**
     * @brief Deep-copy constructor.
     *
     * Produces an independent pool with copied ValueDomain entries and newly
     * constructed ActiveStateNode instances preserving the original insertion order.
     */
    ActiveStatePool(const ActiveStatePool<ValueDomain>& other)
    {
        init();
        if (other.first == nullptr) {
            return;
        }
        data = other.data;
        obj2StateNode = other.obj2StateNode;
        for (auto& it : other.data) {
            auto& newNode = obj2StateNode.at(it.first);
            auto& node = other.obj2StateNode.at(it.first);
            if (node.prev) {
                newNode.prev = &obj2StateNode.at(node.prev->value);
            }
            if (node.next) {
                newNode.next = &obj2StateNode.at(node.next->value);
            }
        }
        if (other.first) {
            first = &obj2StateNode.at(other.first->value);
        }
        if (other.tails) {
            tails = &obj2StateNode.at(other.tails->value);
        }
    }

    /**
     * @brief Deep-copy assignment.
     * @return reference to this
     */
    ActiveStatePool& operator=(const ActiveStatePool<ValueDomain>& other)
    {
        init();
        if (other.first == nullptr) {
            return *this;
        }
        data = other.data;
        obj2StateNode = other.obj2StateNode;
        for (auto& it : other.data) {
            auto& newNode = obj2StateNode.at(it.first);
            auto& node = other.obj2StateNode.at(it.first);
            if (node.prev) {
                newNode.prev = &obj2StateNode.at(node.prev->value);
            }
            if (node.next) {
                newNode.next = &obj2StateNode.at(node.next->value);
            }
        }
        if (other.first) {
            first = &obj2StateNode.at(other.first->value);
        }
        if (other.tails) {
            tails = &obj2StateNode.at(other.tails->value);
        }
        return *this;
    }

    /**
     * @brief Move constructor transfers ownership of internal structures.
     */
    ActiveStatePool(ActiveStatePool<ValueDomain>&& other)
    {
        first = other.first;
        other.first = nullptr;
        tails = other.tails;
        other.tails = nullptr;

        data = std::move(other.data);
        obj2StateNode = std::move(other.obj2StateNode);
    }

    /**
     * @brief Move assignment transfers ownership.
     * @return reference to this
     */
    ActiveStatePool& operator=(ActiveStatePool<ValueDomain>&& other)
    {
        first = other.first;
        other.first = nullptr;
        tails = other.tails;
        other.tails = nullptr;

        data = std::move(other.data);
        obj2StateNode = std::move(other.obj2StateNode);
        return *this;
    }

    ~ActiveStatePool()
    {
    }

    /** @name Iteration */ /// @{
    ConstIterator Begin() const
    {
        return data.begin();
    }

    ConstIterator End() const
    {
        return data.end();
    }

    Iterator Begin()
    {
        return data.begin();
    }
    
    Iterator End()
    {
        return data.end();
    }
    /// @}

    /** @name Lookup */ /// @{
    /**
     * @brief Find mapping for @p value (const).
     * @return const_iterator to entry or End().
     */
    ConstIterator Find(Value* value) const
    {
        return data.find(value);
    }

    /**
     * @brief Find mapping for @p value (non-const).
     * @return iterator to entry or End().
     */
    Iterator Find(Value* value)
    {
        return data.find(value);
    }
    /// @}

    /**
     * @brief Access stored domain for @p value.
     *
     * If the value is not present in the pool a reference to a shared TOP domain
     * instance is returned (TOP_REF_STATE for reference types, TOP_STATE for others).
     *
     * @param value pointer to Value
     * @return reference to ValueDomain
     */
    ValueDomain& At(Value* value)
    {
        if (data.count(value) == 0) {
            return GetTopState(*value);
        }
        return data.at(value);
    }
    
    const ValueDomain& At(Value* value) const
    {
        if (data.count(value) == 0) {
            return GetTopState(*value);
        }
        return data.at(value);
    }

    /**
     * @brief Insert a new mapping and append its node to the tail of the list.
     *
     * If the object already exists, the existing node pointer is returned.
     *
     * @param obj Value* key
     * @param domain ValueDomain value to store
     * @return pointer to the ActiveStateNode associated with the object
     */
    ActiveStateNode* Insert(Value* obj, ValueDomain domain)
    {
        if (data.count(obj) != 0) {
            return &obj2StateNode[obj];
        }
        data.emplace(obj, std::move(domain));
        obj2StateNode[obj] = ActiveStateNode(obj);
        auto stateNode = &obj2StateNode[obj];
        
        stateNode->next = nullptr;
        stateNode->prev = tails;
        if (tails == nullptr) {
            CJC_ASSERT(first == nullptr);
            tails = stateNode;
            first = stateNode;
        } else {
            tails->next = stateNode;
            tails = stateNode;
        }
        CheckPoolOverflow();
        return stateNode;
    }

    /**
     * @brief Emplace wrapper for Insert.
     */
    ActiveStateNode* emplace(Value* obj, ValueDomain domain)
    {
        return Insert(obj, std::move(domain));
    }

    /**
     * @brief Join another ActiveStatePool into this one.
     *
     * Existing keys are joined using ValueDomain::Join; newly inserted keys do
     * not mark the result as changed.
     *
     * @param other pool to join from
     * @return true if any existing domain changed
     */
    bool Join(const ActiveStatePool<ValueDomain>& other)
    {
        bool changed = false;
        for (const auto& [k2, v2] : other.data) {
            if (auto it = data.find(k2); it != data.end()) {
                auto& v1 = it->second;
                changed |= v1.Join(v2);
            } else {
                Insert(k2, v2);
                // add new value do not change.
            }
        }
        return changed;
    }

private:
    /**
     * @brief Reset internal roots and clear maps.
     */
    void init()
    {
        first = nullptr;
        tails = nullptr;
        data.clear();
        obj2StateNode.clear();
    }

    /**
     * @brief Evict oldest entries when pool exceeds MAX_STATE_POOL_SIZE.
     *
     * Eviction continues until the pool size falls to BASE_STATE_POOL_SIZE.
     */
    void CheckPoolOverflow()
    {
        if (data.size() < MAX_STATE_POOL_SIZE) {
            return;
        }
        while (data.size() > BASE_STATE_POOL_SIZE) {
            auto stateFirst = first;
            CJC_NULLPTR_CHECK(stateFirst);
            CJC_NULLPTR_CHECK(stateFirst->next);
            stateFirst->next->prev = nullptr;
            first = stateFirst->next;
            auto obj = stateFirst->value;
            
            obj2StateNode.erase(obj);
            data.erase(obj);
        }
    }

    /**
     * @brief Return shared TOP state for a Value not present in the pool.
     * @param value reference to Value
     * @return reference to TOP ValueDomain (ref or non-ref)
     */
    static ValueDomain& GetTopState(Value& value)
    {
        auto type = value.GetType();
        if (type != nullptr && type->IsRef()) {
            return TOP_REF_STATE;
        }
        return TOP_STATE;
    }

    // can not merge ValueDomain and stateNode to one, because keep same api with default one.
    std::unordered_map<Value*, ValueDomain> data;            /**< mapping Value* -> ValueDomain */
    std::unordered_map<Value*, ActiveStateNode> obj2StateNode; /**< mapping Value* -> node */

    ActiveStateNode* first = nullptr; /**< oldest node (head) */
    ActiveStateNode* tails = nullptr; /**< newest node (tail) */

    static ValueDomain TOP_STATE;      /**< shared TOP state for non-ref values */
    static ValueDomain TOP_REF_STATE;  /**< shared TOP state for ref values */
    
    static size_t MAX_STATE_POOL_SIZE;  /**< eviction high-water threshold */
    static size_t BASE_STATE_POOL_SIZE; /**< eviction low-water threshold */
};

template<typename Domain, typename = std::enable_if_t<std::is_base_of_v<AbstractDomain<Domain>, Domain>>>
bool MapJoin(ActiveStatePool<Domain>& lhs, const ActiveStatePool<Domain>& rhs)
{
    return lhs.Join(rhs);
}

// eviction high-water mark
template <typename ValueDomain>
size_t ActiveStatePool<ValueDomain>::MAX_STATE_POOL_SIZE = 120;
// eviction low-water mark
template <typename ValueDomain>
size_t ActiveStatePool<ValueDomain>::BASE_STATE_POOL_SIZE = 80;
// non-ref top state
template <typename ValueDomain>
ValueDomain ActiveStatePool<ValueDomain>::TOP_STATE = ValueDomain(true);
// non-ref top state
template <typename ValueDomain>
ValueDomain ActiveStatePool<ValueDomain>::TOP_REF_STATE = ValueDomain(Ref::GetTopRefInstance());
}  // namespace Cangjie::CHIR

#endif