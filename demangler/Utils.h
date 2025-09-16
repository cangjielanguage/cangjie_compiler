// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.


#ifndef CANGJIE_DEMANGLER_UTILS_H
#define CANGJIE_DEMANGLER_UTILS_H

#include <unordered_map>
#include <cstring>

namespace Cangjie {
const char MANGLE_CANGJIE_PREFIX[] = "_C";
const char MANGLE_NESTED_PREFIX[] = "N";
constexpr char MANGLE_GENERIC_PREFIX = 'I';
constexpr char MANGLE_FUNCTION_PREFIX = 'H';
constexpr char MANGLE_COUNT_PREFIX = 'K';
constexpr char MANGLE_FILE_ID_PREFIX = 'U';
constexpr char MANGLE_EXTEND_PREFIX = 'X';
constexpr char MANGLE_LAMBDA_PREFIX = 'L';
constexpr char MANGLE_UNDERSCORE_PREFIX = '_';
constexpr char END = 'E';
constexpr char MANGLE_FILE_NUMBER_END = '$';
constexpr size_t FILE_HASH_LEN = 13;
constexpr size_t SPECIAL_NAME_LEN = 2;
constexpr size_t MANGLE_CHAR_LEN = 1;

struct StdPkgCompare {
    bool operator()(const char* pkg1, const char* pkg2) const
    {
        return strcmp(pkg1, pkg2) == 0;
    }
};

struct StdPkgHash {
    size_t operator()(const char* pkg) const
    {
        std::string cPkg = pkg;
        size_t hash = 1013;
        size_t offset = 5;
        size_t id = 0;
        const size_t n = cPkg.size();
        while (id < n) {
            hash = ((hash << offset) + hash) + cPkg[id++];
        }
        return hash & (0x7FFFFFFF);
    }
};

enum class OperatorKind : size_t {
    AN = 13,
    CL = 75,
    DV = 117,
    EO = 142,
    EQ = 144,
    GE = 196,
    GT = 211,
    IX = 279,
    LE = 356,
    LS = 370,
    LT = 371,
    ML = 395,
    NE = 420,
    NG = 422,
    NT = 435,
    OR = 465,
    PL = 491,
    PW = 502,
    RM = 556,
    RS = 562
};

const std::unordered_map<const char*, const char*, StdPkgHash, StdPkgCompare> MANGLE_STDPKG_MAP = {
    {"ab", "std.sync"},
    {"ac", "std.collection"},
    {"ad", "std.deriving"},
    {"ae", "std.database"},
    {"af", "std.net"},
    {"ag", "std.chirad"},
    {"ah", "std.io"},
    {"ai", "std.convert"},
    {"aj", "std.ffi"},
    {"ak", "std.argopt"},
    {"al", "std.objectpool"},
    {"am", "std.reflect"},
    {"an", "std.fs"},
    {"ao", "std.runtime"},
    {"ap", "std.overflow"},
    {"aq", "std.unittest"},
    {"ar", "std.binary"},
    {"as", "std.unicode"},
    {"at", "std.core"},
    {"au", "std.console"},
    {"av", "std.random"},
    {"aw", "std.sort"},
    {"ax", "std.ast"},
    {"ay", "std.env"},
    {"ba", "std.process"},
    {"bb", "std.time"},
    {"bc", "std.regex"},
    {"bd", "std.posix"},
    {"be", "std.crypto"},
    {"bf", "std.ref"},
    {"bg", "std.math"},
    {"bh", "std.collection.concurrent"},
    {"bi", "std.collection.concurrent.native"},
    {"bj", "std.deriving.impl"},
    {"bk", "std.deriving.builtins"},
    {"bl", "std.deriving.api"},
    {"bm", "std.deriving.resolve"},
    {"bn", "std.database.sql"},
    {"bo", "std.net.native"},
    {"bq", "std.convert.native"},
    {"bs", "std.objectpool.native"},
    {"bt", "std.fs.native"},
    {"bu", "std.runtime.native"},
    {"bv", "std.unittest.mock"},
    {"bw", "std.unittest.testmacro"},
    {"bx", "std.unittest.prop_test"},
    {"by", "std.unittest.common"},
    {"ca", "std.unittest.native"},
    {"cb", "std.unittest.diff"},
    {"cc", "std.unittest.mock.mockmacro"},
    {"cd", "std.unittest.mock.internal"},
    {"ce", "std.core.native"},
    {"cf", "std.random.native"},
    {"cg", "std.ast.native"},
    {"ch", "std.env.native"},
    {"ci", "std.process.native"},
    {"cj", "std.time.native"},
    {"ck", "std.regex.native"},
    {"cl", "std.posix.native"},
    {"cm", "std.crypto.digest"},
    {"cn", "std.crypto.cipher"},
    {"co", "std.math.numeric"},
    {"cp", "std.math.native"},
    {"cq", "std.math.numeric.native"}
};
} // namespace Cangjie
#endif // CANGJIE_DEMANGLER_UTILS_H