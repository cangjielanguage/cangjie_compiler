// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the abstract domain of CHIR value.
 */

#include "cangjie/CHIR/Analysis/ValueDomain.h"

namespace Cangjie::CHIR {
AbstractObject::AbstractObject(std::string identifier) : Value(nullptr, identifier, ValueKind::KIND_LOCALVAR)
{
}

std::string AbstractObject::ToString() const
{
    return identifier;
}

AbstractObject* AbstractObject::GetTopObjInstance()
{
    static AbstractObject ins{"TopObj"};
    return &ins;
}

bool AbstractObject::IsTopObjInstance() const
{
    return this == GetTopObjInstance();
}

Ref::Ref(std::string uniqueID, bool isStatic) : isStatic(isStatic), uniqueID(std::move(uniqueID))
{
}

std::string Ref::GetUniqueID() const
{
    return isStatic ? "s" + uniqueID : uniqueID;
}

void Ref::AddRoots(Ref* r1, Ref* r2)
{
    const auto add = [this](Ref* r) {
        if (r->roots.empty()) {
            roots.emplace(r);
        } else {
            roots.insert(r->roots.begin(), r->roots.end());
        }
    };
    add(r1);
    add(r2);
}

bool Ref::IsEquivalent(Ref* r)
{
    return !roots.empty() && roots == r->roots;
}

bool Ref::CanRepresent(Ref* r)
{
    if (auto cacheRes = CheckCache(r); cacheRes.has_value()) {
        return cacheRes.value();
    } else {
        bool res;
        if (roots.empty()) {
            // this is a root ref
            res = false;
        } else if (r->roots.empty()) {
            // rhs is a root ref
            res = roots.find(r) != roots.end();
        } else {
            const auto check = [this](const auto& x) { return roots.find(x) != roots.end(); };
            res = r->roots.size() <= roots.size() && std::all_of(r->roots.begin(), r->roots.end(), check);
        }
        WriteCache(r, res);

        return res;
    }
}

std::optional<bool> Ref::CheckCache(Ref* r)
{
    std::unique_lock<std::mutex> guard(cacheMtx, std::defer_lock);
    if (isStatic) {
        guard.lock();
    }
    if (auto it = cache.find(r); it != cache.end()) {
        return it->second;
    } else {
        return std::nullopt;
    }
}

void Ref::WriteCache(Ref* r, bool res)
{
    std::unique_lock<std::mutex> guard(cacheMtx, std::defer_lock);
    if (isStatic) {
        guard.lock();
    }
    cache.emplace(r, res);
}

Ref* Ref::GetTopRefInstance()
{
    static Ref ins{"TopRef", false};
    return &ins;
}

bool Ref::IsTopRefInstance() const
{
    return this == GetTopRefInstance();
}
}  // namespace Cangjie::CHIR