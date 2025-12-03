// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANNOINFO_H
#define CANGJIE_CHIR_ANNOINFO_H

#include <string>

#include "cangjie/Utils/SafePointer.h"

namespace Cangjie::CHIR {
// Need refactor: temporary solution of custom defined annotation
/**
 * generate an instance of user defined annotation class,
 * e.g
 *
 * @annotation
 * public class JsonName {
 *     public const JsonName(public let name: String) {}
 * }
 *
 * class Worker {
 *     Worker(
 *         @JsonName["worker_name"]
 *         public let name: String,
 *         @JsonName["worker_age"]
 *         public let age: Int64
 *     ){}
 * }
 *
 * here class Worker has two member vars: name and age, and both use user-defined annotation `JsonName`,
 * so each of two member var has `AnnoInfo` to indicate a compiler-added function that is used to
 * create instance of `class JsonName`, the `mangledName` in `AnnoInfo` record the mangledName of the
 * compiler-added function.
 *
 * respectively, the two compiler-added funcs are $Anno_CN7default6Worker4nameE and $Anno_CN7default6Worker3ageE.
 *
 * the former will create `JsonName` and pass "worker_name" as argument to `JsonName`'s constructor,
 * similarly, the latter will pass "worker_age" to `JsonName`'s constructor.
 *
 * finally, the mangledName is used to generate metadata in CodeGen.
 *
 */
struct AnnoInfo {
    // If it's not cangjie custom annotation, mangledName should be "none". Required for generating metadata.
    std::string mangledName{"none"}; // mangledName of annotation generated func
    // `AnnoPair` is a structure used to save the information of the annotation whose parameter values are
    // literal constants:
    //      - `annoClassName` is the name of that annotation class
    //      - `paramValues` is used to save each parameter value as a string
    // note: annotation that has no parameters is also included
    struct AnnoPair {
        std::string annoClassName;
        std::vector<std::string> paramValues;
        AnnoPair(const std::string& annoClassName, const std::vector<std::string>& paramValues)
            : annoClassName(annoClassName), paramValues(paramValues)
        {
        }
    };
    // `annoPairs` is used to collect all annotations whose parameter values are literal constants.
    // Annotations without parameters are also included in the collection.
    std::vector<AnnoPair> annoPairs;
    bool IsAvailable() const
    {
        return mangledName != "none";
    }
};
} // namespace Cangjie::CHIR

#endif
