// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file lists paths to std module bitcode files, which is included by headers.
 */
#ifdef STDLIB
STDLIB(CORE, "std", "core")
STDLIB(BINARY, "std", "binary")
STDLIB(IO, "std", "io")
STDLIB(MATH, "std", "math")
STDLIB(OVERFLOW, "std", "overflow")
STDLIB(RUNTIME, "std", "runtime")
STDLIB(CONVERT, "std", "convert")
STDLIB(RANDOM, "std", "random")
STDLIB(COLLECTION, "std", "collection")
STDLIB(UNICODE, "std", "unicode")
STDLIB(SORT, "std", "sort")
STDLIB(ARGOPT, "std", "argopt")
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
STDLIB(AST, "std", "ast")
STDLIB(INTEROP, "std", "interop")
#endif
STDLIB(TIME, "std", "time")
STDLIB(SYNC, "std", "sync")
STDLIB(COLLECTION.CONCURRENT, "std", "collection.concurrent")
STDLIB(NET, "std", "net")
STDLIB(REGEX, "std", "regex")
STDLIB(UNITTEST.COMMON, "std", "unittest.common")
STDLIB(UNITTEST.PROP_TEST, "std", "unittest.prop_test")
STDLIB(UNITTEST.DIFF, "std", "unittest.diff")
STDLIB(MATH.NUMERIC, "std", "math.numeric")
STDLIB(FS, "std", "fs")
STDLIB(UNITTEST.MOCK.INTERNAL, "std", "unittest.mock.internal")
STDLIB(UNITTEST.MOCK, "std", "unittest.mock")
STDLIB(REFLECT, "std", "reflect")
STDLIB(REF, "std", "ref")
STDLIB(STD.CRYPTO, "std", "crypto")
STDLIB(CRYPTO.DIGEST, "std", "crypto.digest")
STDLIB(CRYPTO.CIPHER, "std", "crypto.cipher")
STDLIB(CONSOLE, "std", "console")
STDLIB(DATABASE, "std", "database")
STDLIB(DATABASE.SQL, "std", "database.sql")
STDLIB(POSIX, "std", "posix")
STDLIB(PROCESS, "std", "process")
STDLIB(ENV, "std", "env")
STDLIB(OBJECTPOOL, "std", "objectpool")
STDLIB(UNITTEST, "std", "unittest")

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
STDLIB(DERIVING.API, "std", "deriving.api")
STDLIB(DERIVING.RESOLVE, "std", "deriving.resolve")
STDLIB(DERIVING.IMPL, "std", "deriving.impl")
STDLIB(DERIVING.BUILTINS, "std", "deriving.builtins")
STDLIB(DERIVING, "std", "deriving")
STDLIB(UNITTEST.TESTMACRO, "std", "unittest.testmacro")
STDLIB(UNITTEST.MOCK.MOCKMACRO, "std", "unittest.mock.mockmacro")
#endif // CANGJIE_CODEGEN_CJNATIVE_BACKEND

STDLIB_TOPPKG(STD, "std")

#ifndef BACKEND_BC
STDLIB_ROOTPKG(STD, "std")
STDLIB(FFI.PYTHON, "std", "ffi.python")
// This will cause `cjstack` cannot be linked, because missing symbol `LLVMFuzzerRunDriver`.
STDLIB(FUZZ, "fuzz", "fuzz")
#endif
#endif
