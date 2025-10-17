// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Diagnostic Emitter class, which emits single diagnostic to console.
 */

#ifndef CANGJIE_DIAGNOSTICEMITTER_H
#define CANGJIE_DIAGNOSTICEMITTER_H

#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie {
class DiagnosticEmitter {
public:
    DiagnosticEmitter(
        Diagnostic& d, bool nc, bool enableRangeCheckICE, std::basic_ostream<char>& o, SourceManager& sourceManager);
    ~DiagnosticEmitter();
    // return false if some errors occurred. If 'enableOnlyHint' is true, it won't emit error message.
    bool Emit(bool enableOnlyHint = false) const;

private:
    class DiagnosticEmitterImpl* impl;
};
} // namespace Cangjie

#endif // CANGJIE_DIAGNOSTICEMITTER_H
