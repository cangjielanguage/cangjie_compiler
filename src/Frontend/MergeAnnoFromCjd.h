// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares MergeCusAnno.
 */

#ifndef CANGJIE_MERGE_ANNO_FROM_CJD_H
#define CANGJIE_MERGE_ANNO_FROM_CJD_H

#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie {
void MergeCusAnno(Ptr<AST::Package> target, Ptr<AST::Package> source);
}

#endif
