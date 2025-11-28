// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.


#include "cangjie/Option/Option.h"
#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie::Interop::Java {

bool CheckExportJavaPathOption(const GlobalOptions& opts)
{
    if (opts.outputJavaGenDir.has_value()) {
        DiagnosticEngine diag;
        diag.RegisterHandler(opts.diagFormat);
        const char* recommendedOption = "--export-java-path";
        const char* deprecatedOption = "--output-javagen-dir";

        (void) diag.DiagnoseRefactor(DiagKindRefactor::driver_deprecated_option_recommended, DEFAULT_POSITION,
            deprecatedOption, recommendedOption);
    
        if (opts.exportJavaPath.has_value()) {
            (void) diag.DiagnoseRefactor(DiagKindRefactor::driver_simultanious_deprecated_and_recommmended_option_usage,
                DEFAULT_POSITION, deprecatedOption, recommendedOption);
            return false;
        }
    }

    return true;
}

} // namespace Cangjie::Interop::Java
