// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares wrappers for unique_ptr and raw pointers
 * that throws exception on accessing nullptr.
 */

#ifndef CANGJIE_UTILS_SAFEPOINTER_H
#define CANGJIE_UTILS_SAFEPOINTER_H

#include <cstdint>
#ifndef CANGJIE_ENABLE_GCOV
static_assert(true);
#include <exception>
#endif
#include <type_traits>
#include <vector>

#include "CheckUtils.h"

/*
 * A non-crash counterpart for unique_ptr and raw pointer.
 * Usage is almost identical to unique_ptr and raw pointer.
 *
 * It is user's duty to guarantee that null pointers are not dereferenced
 * and that no pointer is accessed after the owner's lifespan ends.
 *
 * However, a dereference to null pointer
 * will raise an exception instead of triggering a system-level fault signal.
 * Dereference to a pointer that is already released still crashes.
 */

#ifndef CANGJIE_ENABLE_GCOV
class NullPointerException : public std::exception {
public:
    NullPointerException();

    int64_t GetTriggerPoint() const noexcept
    {
        return triggerPoint;
    }

    const char* what() const noexcept override
    {
        return static_cast<const char*>("Dereferencing a null pointer");
    }

private:
    int64_t triggerPoint = 0;
};
#endif

template <typename T> class Ptr;

template <typename T> class OwnedPtr {
public:
    OwnedPtr() : origin(nullptr)
    {
    }
    OwnedPtr(std::nullptr_t) : origin(nullptr)
    {
    }

    /* to infer type argument when not up-casting */
    OwnedPtr(T* raw) : origin(raw)
    {
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>> OwnedPtr(U* raw) : origin(raw)
    {
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>>
    OwnedPtr(OwnedPtr<U>&& src) : origin(src.origin)
    {
        src.origin = nullptr;
    }

    OwnedPtr(const OwnedPtr<T>&) = delete;
    OwnedPtr<T>& operator=(const OwnedPtr<T>&) = delete;

    void operator=(std::nullptr_t)
    {
        reset(nullptr);
    }

    ~OwnedPtr()
    {
        delete origin;
    }

    Ptr<T> get() const
    {
        return Ptr<T>(*this);
    }

    void reset(std::nullptr_t = nullptr)
    {
        auto old = origin;
        origin = nullptr;
        if (old) {
            delete old;
        }
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>> void reset(U* raw)
    {
        auto old = origin;
        origin = raw;
        if (old) {
            delete old;
        }
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>> void swap(OwnedPtr<U>& src) noexcept
    {
        std::swap(origin, src.origin);
    }

    T* release()
    {
        auto old = origin;
        origin = nullptr;
        return old;
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>> void operator=(OwnedPtr<U>&& src)
    {
        auto old = origin;
        origin = src.origin;
        src.origin = nullptr;
        if (old) {
            delete old;
        }
    }

    T* operator->() const
    {
#ifdef CANGJIE_ENABLE_GCOV
        return origin;
#else
        if (origin != nullptr) {
            return origin;
        } else {
#ifndef CANGJIE_ENABLE_GCOV
            throw NullPointerException();
#else
#ifdef CJC_ABORT
            CJC_ABORT();
#else
            return nullptr;
#endif
#endif
        }
#endif
    }

    T& operator*() const
    {
        return *operator->();
    }

    operator bool() const
    {
        return origin != nullptr;
    }

    template <typename U> bool operator<(const OwnedPtr<U>& src) const
    {
        return origin < src.origin;
    }

    bool operator!=(std::nullptr_t) const
    {
        return origin != nullptr;
    }

    bool operator==(std::nullptr_t) const
    {
        return origin == nullptr;
    }

    template <typename U> bool operator!=(const OwnedPtr<U>& src) const
    {
        return origin != src.origin;
    }

    template <typename U> bool operator==(const OwnedPtr<U>& src) const
    {
        return origin == src.origin;
    }

    template <class U>
    friend OwnedPtr<U> StaticPointerCast(OwnedPtr<T>&& o);

    friend void Swap(OwnedPtr& one, OwnedPtr& other)
    {
        std::swap(one.origin, other.origin);
    }

private:
    template <typename U> friend class OwnedPtr;
    template <typename U> friend class Ptr;
    friend std::hash<OwnedPtr<T>>;
    T* origin = nullptr;
};

template <class T, class U>
OwnedPtr<U> StaticPointerCast(OwnedPtr<T>&& o)
{
    return {static_cast<U*>(o.release())};
}

template <typename T> class Ptr {
public:
    Ptr()
    {
    }

    Ptr(std::nullptr_t) : ptr(nullptr)
    {
    }

    /* to infer type argument when not up-casting */
    Ptr(T* raw) : ptr(raw)
    {
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>> Ptr(U* src) : ptr(src)
    {
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>>
    Ptr(const OwnedPtr<U>& src) : ptr(src.origin)
    {
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>> Ptr(const Ptr<U>& src) : ptr(src.ptr)
    {
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>> Ptr(const Ptr<U>&& src) : ptr(src.ptr)
    {
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>>
    Ptr<T>& operator=(const OwnedPtr<U>& src)
    {
        this->ptr = src.origin;
        return *this;
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>> Ptr<T>& operator=(const Ptr<U>& src)
    {
        this->ptr = src.ptr;
        return *this;
    }

    template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>> Ptr<T>& operator=(const Ptr<U>&& src)
    {
        this->ptr = src.ptr;
        return *this;
    }

    T* operator->() const
    {
#ifdef CANGJIE_ENABLE_GCOV
        return ptr;
#else
        if (ptr != nullptr) {
            return ptr;
        } else {
#ifndef CANGJIE_ENABLE_GCOV
            throw NullPointerException();
#else
#ifdef CJC_ABORT
            CJC_ABORT();
#else
            return nullptr;
#endif
#endif
        }
#endif
    }

    T& operator*() const
    {
        return *operator->();
    }

    T* get() const
    {
        return ptr;
    }

    template <typename U> Ptr<U> StaticCast()
    {
        return Ptr<U>(static_cast<U*>(ptr));
    }

    operator bool() const
    {
        return ptr != nullptr;
    }

    operator T*() const
    {
        return ptr;
    }

    template <typename U> bool operator<(const Ptr<U>& src) const
    {
        return ptr < src.ptr;
    }

    bool operator!=(std::nullptr_t) const
    {
        return ptr != nullptr;
    }

    bool operator==(std::nullptr_t) const
    {
        return ptr == nullptr;
    }

    template <typename U> bool operator!=(const Ptr<U>& src) const
    {
        return ptr != src.ptr;
    }

    template <typename U> bool operator==(const Ptr<U>& src) const
    {
        return ptr == src.ptr;
    }

private:
    template <typename U> friend class Ptr;
    friend std::hash<Ptr<T>>;

    T* ptr = nullptr;
};

/* Do not support raw array */
template <class T, class... Args> std::enable_if_t<!std::is_array<T>::value, OwnedPtr<T>> MakeOwned(Args&&... args)
{
    return OwnedPtr<T>(new T(std::forward<Args>(args)...));
}

template <typename T> struct std::hash<OwnedPtr<T>> {
    std::size_t operator()(const OwnedPtr<T>& ptr) const noexcept
    {
        return static_cast<std::size_t>(reinterpret_cast<uintptr_t>(ptr.origin));
    }
};

template <typename T> struct std::hash<Ptr<T>> {
    std::size_t operator()(const Ptr<T>& ptr) const noexcept
    {
        return static_cast<std::size_t>(reinterpret_cast<uintptr_t>(ptr.ptr));
    }
};

template <typename T> struct std::hash<const OwnedPtr<T>> {
    std::size_t operator()(const OwnedPtr<T>& ptr) const noexcept
    {
        return static_cast<std::size_t>(reinterpret_cast<uintptr_t>(ptr.origin));
    }
};

template <typename T> struct std::hash<const Ptr<T>> {
    std::size_t operator()(const Ptr<T>& ptr) const noexcept
    {
        return static_cast<std::size_t>(reinterpret_cast<uintptr_t>(ptr.ptr));
    }
};

template <class T> struct std::remove_pointer<Ptr<T>> {
    using type = T;
};

#endif
