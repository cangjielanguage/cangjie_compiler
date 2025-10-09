// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_NATIVE_FFI_TYPE_CAST_CHECK_H
#define CANGJIE_CHIR_NATIVE_FFI_TYPE_CAST_CHECK_H

#include "cangjie/CHIR/DiagAdapter.h"
#include "cangjie/CHIR/Package.h"

namespace Cangjie::CHIR::NativeFFI {

class TypeCastCheck final {
public:
    explicit TypeCastCheck(DiagAdapter& diag);

    void RunOnPackage(const Package& package, size_t threadNum);

private:
    void RunOnFunc(const Func& func);

private:
    DiagAdapter& diag;
};

} // namespace Cangjie::CHIR::NativeFFI

#endif
