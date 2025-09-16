// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

macro package child

import std.ast.*

public macro Outer(input: Tokens): Tokens {
    let messages = getChildMessages("Inner")

    for (m in messages) {
        let identName = m.getString("identifierName")
        println(identName)
        
    }

    return input

}

public macro Inner(input: Tokens): Tokens {
    assertParentContext("Outer")
    setItem("identifierName", "value")
    return input
}
