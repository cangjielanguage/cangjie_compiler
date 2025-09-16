// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Utils/SafePointer.h"

#include "cangjie/Utils/ICEUtil.h"

#ifndef CANGJIE_ENABLE_GCOV
NullPointerException::NullPointerException() : triggerPoint(Cangjie::ICE::GetTriggerPoint())
{
}
#endif
