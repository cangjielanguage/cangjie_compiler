// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares partially persistent data structure
 */

#ifndef CANGJIE_UTILS_PARTIALLYPERSISTENT_H
#define CANGJIE_UTILS_PARTIALLYPERSISTENT_H

#include <set>
#include <vector>
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
using VersionID = size_t;
const VersionID DUMMY_VERSION_ID = 0;

// Uniformed interface to manipulate compound pseudo persistent data.
// Should be specialized for each specific type.
// Template parameters are on each function to allow inference.
class PData {
public:
    // add a new checkpoint
    // note that an empty container always start with a base checkpoint
    template<typename T> static void Commit(T& data)
    {
        data.commit();
    }

    // go back to last checkpoint, discard all changes later
    template<typename T> static void Reset(T& data)
    {
        data.reset();
    }

    // go back to last checkpoint, changes later saved and can be re-applied with the returned ID
    template<typename T, typename V> static V Stash(T& data)
    {
        return data.stash();
    }

    /* apply the changes saved before, specified by the given ID
    
       Note that the ID is associated with the current last checkpoint.
       Stash and apply must happen under the same last checkpoint.
       Otherwise the behavior is undefined. */
    template<typename T, typename V> static void Apply(T& data, V& version)
    {
        data.apply(version);
    }

    /* don't change anything, but remove the last checkpoint,
       effectively making changes since second last checkpoint uncommitted
       e.g.:
       1, 2 !! 3, 4 !! 5, 6
       ResetSoft -->
       1, 2 !! 3, 4, 5, 6
       where `!!` is checkpoint
      
       in case no second last checkpoint, will only clear all stashed versions */
    template<typename T> static void ResetSoft(T& data)
    {
        data.resetSoft();
    }

    /* automatically call Commit(data) when created, and ResetSoft(data) when exiting scope */
    template <typename T> struct CommitScope {
        explicit CommitScope(T& data): data(data)
        {
            Commit(data);
        }

        ~CommitScope()
        {
            ResetSoft(data);
        }

        CommitScope() = delete;
        CommitScope(const CommitScope& other) = delete;

        T& data;
    };
};

// partially persistent set
// can backtrack to history version and switch between saved versions
// but only current version editable
template <typename T>
class PSet {
public:
    PSet()
    {
        commit();
    }

    PSet(const PSet<T>& other):data(other.data), log(other.log), stashes(other.stashes)
    {
    }

    explicit PSet(const std::set<T>& other):data(other)
    {
        commit();
    }
    
    explicit PSet(const std::initializer_list<T>& init):data(std::set<T>(init))
    {
        commit();
    }

    const std::set<T>& raw()
    {
        return data;
    }

    PSet& operator=(const PSet<T>& other)
    {
        if (this == &other) {
            return *this;
        }
        clear();
        data = other.data;
        log = other.log;
        stashes = other.stashes;
        return *this;
    }

    PSet& operator=(const std::set<T>& other)
    {
        if (&other == &data) {
            return *this;
        }
        clear();
        data = other;
        return *this;
    }

    // implicit cast for ease of use
    // must be const since there can't be unlogged changes
    operator const std::set<T>&()
    {
        return data;
    }

    void clear()
    {
        data.clear();
        log.clear();
        stashes.clear();
        commit();
    }

    std::pair<typename std::set<T>::iterator, bool> insert(const T& value)
    {
        checkIn(value);
        return data.insert(value);
    }

    size_t erase(const T& value)
    {
        checkOut(value);
        return data.erase(value);
    }

    typename std::set<T>::iterator erase(const typename std::set<T>::iterator& pos)
    {
        checkOut(*pos);
        return data.erase(pos);
    }

    bool contains(const T& value) const
    {
        return data.count(value) > 0;
    }

    size_t count(const T& value)
    {
        return data.count(value);
    }

    void merge(std::set<T>& src)
    {
        for (auto& e : src) {
            checkIn(e);
        }
        data.merge(src);
    }

    void merge(PSet<T>& src)
    {
        for (auto& e : src.data) {
            checkIn(e);
        }
        data.merge(src.data);
        src.clear();
    }

    bool empty() const noexcept
    {
        return data.empty();
    }

    size_t size() const
    {
        return data.size();
    }

    typename std::set<T>::iterator begin()
    {
        return data.begin();
    }

    typename std::set<T>::iterator end()
    {
        return data.end();
    }

    typename std::set<T>::const_iterator begin() const
    {
        return data.begin();
    }

    typename std::set<T>::const_iterator end() const
    {
        return data.end();
    }

    typename std::set<T>::iterator cbegin() const noexcept
    {
        return data.cbegin();
    }

    typename std::set<T>::iterator cend() const noexcept
    {
        return data.cend();
    }

    typename std::set<T>::iterator rbegin()
    {
        return data.rbegin();
    }

    typename std::set<T>::iterator rend()
    {
        return data.rend();
    }

    typename std::set<T>::const_iterator rbegin() const
    {
        return data.rbegin();
    }

    typename std::set<T>::const_iterator rend() const
    {
        return data.rend();
    }

    typename std::set<T>::iterator find(const T& value)
    {
        return data.find(value);
    }

    typename std::set<T>::const_iterator find(const T& value) const
    {
        return data.find(value);
    }

    void commit()
    {
        log.emplace_back(std::vector<Log>());
        stashes.emplace_back(std::vector<std::vector<Log>>());
    }

    void reset()
    {
        CJC_ASSERT(!log.empty());
        for (auto l = log.back().rbegin(); l != log.back().rend(); l++) {
            resetOne(*l);
        }
        log.back().clear();
    }

    VersionID stash()
    {
        CJC_ASSERT(!stashes.empty());
        stashes.back().push_back(log.back());
        reset();
        return stashes.back().size();
    }

    void apply(VersionID ver)
    {
        if (ver == 0) {
            return;
        }
        auto verRaw = ver - 1;
        CJC_ASSERT(stashes.back().size() > verRaw);
        for (auto& l : stashes.back()[verRaw]) {
            applyOne(l);
        }
    }

    void resetSoft()
    {
        if (log.size() > 1) {
            stashes.pop_back();
            auto& newLast = log[log.size() - 2];
            newLast.insert(newLast.end(), log.back().begin(), log.back().end());
            log.pop_back();
        } else {
            stashes.back().clear();
        }
    }

    bool diff()
    {
        return log.back().empty();
    }

private:
    struct Log {
        bool isInsert; // otherwise erase
        T data;
    };

    std::set<T> data;
    std::vector<std::vector<Log>> log;
    std::vector<std::vector<std::vector<Log>>> stashes;

    void resetOne(const Log& l)
    {
        if (l.isInsert) {
            data.erase(l.data);
        } else {
            data.insert(l.data);
        }
    }

    void applyOne(const Log& l)
    {
        if (l.isInsert) {
            data.insert(l.data);
        } else {
            data.erase(l.data);
        }
    }

    void checkIn(const T& value)
    {
        if (!contains(value)) {
            log.back().push_back({true, value});
        }
    }

    void checkOut(const T& value)
    {
        if (contains(value)) {
            log.back().push_back({false, value});
        }
    }
};
} // namespace

#endif
