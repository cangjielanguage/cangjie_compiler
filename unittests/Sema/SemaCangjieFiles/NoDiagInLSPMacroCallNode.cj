// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.macro package macrodef

package macroModule

import macrodefX.{Component, Prop}

public enum Who {
    You | Me
}

public class Message {
    public Message(public let who: Who) {}
}

@Component
public class ChatBlock {
    @Prop
    var message: Message = Message(Me)

    func build() {
        if (let Me <- message.who) {
        } else {
        }
    }
}
