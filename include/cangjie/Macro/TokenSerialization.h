// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/*
 * @file
 *
 * This file declares the Cangjie Token Serialization.
 */

#ifndef CANGJIE_MODULES_TOKENSERIALIZATION_H
#define CANGJIE_MODULES_TOKENSERIALIZATION_H

#include <fstream>
#include <set>
#include <vector>
#include <cstdint>

#include "cangjie/Lex/Token.h"

namespace TokenSerialization {
using namespace Cangjie;

std::vector<uint8_t> GetTokensBytes(const std::vector<Token>& tokens);

std::vector<Token> GetTokensFromBytes(const uint8_t* pBuffer);

uint8_t* GetTokensBytesWithHead(const std::vector<Token>& tokens);
} // namespace TokenSerialization

#endif // CANGJIE_MODULES_TOKENSERIALIZATION_H
