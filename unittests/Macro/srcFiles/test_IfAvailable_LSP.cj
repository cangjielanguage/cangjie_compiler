/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * This source file is part of the Cangjie project, licensed under Apache-2.0
 * with Runtime Library Exception.
 *
 * See https://cangjie-lang.cn/pages/LICENSE for license information.
 */
 
public class DeviceInfo {
    public static prop sdkApiVersion: Int64 {
        get() {19}
    }
}
 
enum Nums {
    Red | Blue
}
 
func f1() {
    var aa = 11
    @IfAvailable(level: 19, {=>
        var cc = 1
    }, {=>
        var bb = 1
    })
}
 
main(){}