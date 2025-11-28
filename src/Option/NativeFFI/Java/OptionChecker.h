// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_OPTION_NATIVE_FFI_JAVA
#define CANGJIE_OPTION_NATIVE_FFI_JAVA

#include "cangjie/Option/Option.h"

namespace Cangjie::Interop::Java {

/**
 * @brief Checks if the export path option is valid in the given global options.
 *
 * This function validates Java export path options and reports deprecated usage
 * through diagnostic messages. It checks for conflicts between deprecated
 * --output-javagen-dir and recommended --export-java-path options.
 *
 * These options are used to specify the directory where generated Java classes
 * from @JavaImpl annotated classes will be placed.
 *
 * @param opts The GlobalOptions object containing the export path to validate
 * @return true if the export path is valid and accessible
 * @return false if the export path is invalid or inaccessible
 */
bool CheckExportJavaPathOption(const GlobalOptions& opts);

} // namespace Cangjie::Interop::Java

#endif // CANGJIE_OPTION_NATIVE_FFI_JAVA
