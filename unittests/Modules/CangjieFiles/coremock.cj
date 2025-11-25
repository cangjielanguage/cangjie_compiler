// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

package std.core

public open class Object {
    public const init() {}
}

public interface CType {}

public interface Any {}

public interface ToString {}

public enum Option<T> {
    | None
}

public interface Equatable<T> {}

public interface Hashable {}

public struct String {
    public const init() {}

    public operator func ==(right: String): Bool { return true }
    public operator func !=(right: String): Bool { return false }

    public operator func +(right: String): String { return "" }
}

public type Byte = UInt8

func composition<T1, T2, T3>(f: (T1)->T2, g: (T2)->T3): (T1)->T3 {
    return { x: T1 => g(f(x)) }
}
