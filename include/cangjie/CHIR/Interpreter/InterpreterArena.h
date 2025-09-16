// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares an arena for allocating interpreter values.
 */

#ifndef CANGJIE_CHIR_INTERRETER_INTERPREVERARENA_H
#define CANGJIE_CHIR_INTERRETER_INTERPREVERARENA_H

#include "cangjie/CHIR/Interpreter/InterpreterValueUtils.h"

namespace Cangjie::CHIR::Interpreter {

class Arena {
public:
    /* list of objects that needs to run finalizer on them */
    std::vector<IVal*> finalizingObjects;

    Arena()
    {
        buckets.reserve(BUCKETS);
        buckets.emplace_back(std::make_unique<std::vector<IVal>>());
        buckets.back()->reserve(BUCKET_SIZE);
        finalizingObjects.reserve(BUCKET_SIZE);
    }
    IVal* Allocate(IVal&& value)
    {
        if (buckets.back()->size() == BUCKET_SIZE) {
            buckets.emplace_back(std::make_unique<std::vector<IVal>>());
            buckets.back()->reserve(BUCKET_SIZE);
        }
        auto& lastBucket = buckets.back();
        lastBucket->emplace_back(std::move(value));
        auto ptr = &lastBucket->back();
        return ptr;
    }

    void PrintStats()
    {
        std::cout << "Number of buckets: " << buckets.size() << std::endl;
    }

    int64_t GetAllocatedSize()
    {
        CJC_ASSERT(buckets.size() >= 1);
        size_t r = ((buckets.size() - 1) * BUCKET_SIZE + buckets.back()->size()) * sizeof(IVal);
        return static_cast<int64_t>(r);
    }

private:
    static const size_t BUCKETS = 2048;
    static const size_t BUCKET_SIZE = 2048;

    // Why unique_ptr? Because in C++ vector reallocation may either copy or move its contents.
    // It sohuld move if possible -- and it should be possible in this case.
    // HOWEVER, unique_ptr will FORCE the move. IE -- if something goes wrong and moving is not
    // possible for some reason, this will NOT COMPILE.
    // Therefore, I think to start of it's worth keeping the indirection on, and remove it only
    // after profiling. T0D0!!
    using Bucket = std::unique_ptr<std::vector<IVal>>;
    std::vector<Bucket> buckets;
};

} // namespace Cangjie::CHIR::Interpreter

#endif // CANGJIE_CHIR_INTERRETER_INTERPREVERARENA_H