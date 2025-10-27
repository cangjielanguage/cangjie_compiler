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
template <typename ValueDomain>
class DefaultStatePool {
public:
    using ConstIterator = typename std::unordered_map<Value*, ValueDomain>::const_iterator;
    using Iterator = typename std::unordered_map<Value*, ValueDomain>::iterator;

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

    ConstIterator Find(Value* value) const
    {
        return data.find(value);
    }

    Iterator Find(Value* value)
    {
        return data.find(value);
    }

    void Insert(Value* value, ValueDomain domain)
    {
        data.emplace(value, std::move(domain));
    }

    ValueDomain& At(Value* value)
    {
        return data.at(value);
    }
    
    const ValueDomain& At(Value* value) const
    {
        return data.at(value);
    }

    bool Join(const DefaultStatePool<ValueDomain>& rhs)
    {
        return MapJoin<Value*, ValueDomain>(data, rhs.data);
    }

    template<typename... Args>
    std::pair<Iterator, bool> emplace(Args&&... args)
    {
        return data.emplace(args...);
    }

private:
    std::unordered_map<Value*, ValueDomain> data;
};

template <typename Domain, typename = std::enable_if_t<std::is_base_of_v<AbstractDomain<Domain>, Domain>>>
bool MapJoin(DefaultStatePool<Domain>& lhs, const DefaultStatePool<Domain>& rhs)
{
    return lhs.Join(rhs);
}

struct ActiveStateNode {
    ActiveStateNode() {}
    ActiveStateNode(Value* value) : value(value) {}
    ActiveStateNode* prev = nullptr;
    ActiveStateNode* next = nullptr;

    Value* value = nullptr;
};

template <typename ValueDomain>
class ActiveStatePool {
public:
    using ConstIterator = typename std::unordered_map<Value*, ValueDomain>::const_iterator;
    using Iterator = typename std::unordered_map<Value*, ValueDomain>::iterator;

    explicit ActiveStatePool()
    {
    }

    ActiveStatePool(const ActiveStatePool<ValueDomain>& other)
    {
        init();
        if (other.first == nullptr) {
            return;
        }
        for (auto& it : other.data) {
            data.emplace(it.first, it.second);
            auto newNode = ActiveStateNode(it.first);
            obj2StateNode.emplace(it.first, newNode);
        }
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

    ActiveStatePool& operator=(const ActiveStatePool<ValueDomain>& other)
    {
        init();
        if (other.first == nullptr) {
            return *this;
        }
        for (auto& it : other.data) {
            data.emplace(it.first, it.second);
            auto newNode = ActiveStateNode(it.first);
            obj2StateNode.emplace(it.first, newNode);
        }
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

    ActiveStatePool(ActiveStatePool<ValueDomain>&& other)
    {
        first = other.first;
        other.first = nullptr;
        tails = other.tails;
        other.tails = nullptr;

        data = std::move(other.data);
        obj2StateNode = std::move(other.obj2StateNode);
    }

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

    ConstIterator Find(Value* value) const
    {
        return data.find(value);
    }

    Iterator Find(Value* value)
    {
        return data.find(value);
    }

    ValueDomain& At(Value* value)
    {
        if (data.count(value) == 0) {
            return TOP_STATE;
        }
        return data.at(value);
    }
    
    const ValueDomain& At(Value* value) const
    {
        if (data.count(value) == 0) {
            return TOP_STATE;
        }
        return data.at(value);
    }

    ActiveStateNode* Insert(Value* obj, ValueDomain domain)
    {
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

    ActiveStateNode* emplace(Value* obj, ValueDomain domain)
    {
        return Insert(obj, std::move(domain));
    }

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
    void init()
    {
        first = nullptr;
        tails = nullptr;
        data.clear();
        obj2StateNode.clear();
    }

    void CheckPoolOverflow()
    {
        if (data.size() < MAX_STATE_POOL_SIZE) {
            return;
        }
        while (data.size() > BASE_STATE_POOL_SIZE) {
            auto stateFirst = first;
            stateFirst->next->prev = nullptr;
            first = stateFirst->next;
            auto obj = stateFirst->value;
            
            obj2StateNode.erase(obj);
            data.erase(obj);
        }
    }

    // can not merge ValueDomain and stateNode to one, because keep same api with default one.
    std::unordered_map<Value*, ValueDomain> data;
    std::unordered_map<Value*, ActiveStateNode> obj2StateNode;

    ActiveStateNode* first = nullptr;
    ActiveStateNode* tails = nullptr;

    static ValueDomain TOP_STATE;
    
    static size_t MAX_STATE_POOL_SIZE;
    static size_t BASE_STATE_POOL_SIZE;
};

template<typename Domain, typename = std::enable_if_t<std::is_base_of_v<AbstractDomain<Domain>, Domain>>>
bool MapJoin(ActiveStatePool<Domain>& lhs, const ActiveStatePool<Domain>& rhs)
{
    return lhs.Join(rhs);
}

template <typename ValueDomain>
size_t ActiveStatePool<ValueDomain>::MAX_STATE_POOL_SIZE = 120;
template <typename ValueDomain>
size_t ActiveStatePool<ValueDomain>::BASE_STATE_POOL_SIZE = 80;
template <typename ValueDomain>
ValueDomain ActiveStatePool<ValueDomain>::TOP_STATE = ValueDomain(true);
}  // namespace Cangjie::CHIR

#endif