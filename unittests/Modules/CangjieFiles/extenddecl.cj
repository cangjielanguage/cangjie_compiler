// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

package extenddecl

public enum EnumExtend<T> {
    | AA
}

extend<T> EnumExtend<T> <: Equatable<EnumExtend<T>> where T <: Equatable<T> {}

extend<T> EnumExtend<T> <: ToString where T <: ToString {}

extend<T> EnumExtend<T> <: Hashable where T <: Hashable {}

extend<T> EnumExtend<EnumExtend<T>> {}
