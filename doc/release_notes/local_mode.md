# Local mode: CJO format 0.2.0

Local mode upgrades the CJO format from 0.1.x to 0.2.0. The minor version increases and the patch version resets to zero.

The schema adds `BuiltInType.Copy`, receiver parameter information (`ThisParam`), exclave expressions (`ExclaveExpr`), modal type information (`Mode`), and related fields. `BuiltInType` is not an enum whose old readers are known to tolerate new members; a patch-only upgrade cannot make an old reader handle `BuiltInType_Copy`.

CJO files produced by this compiler require a compiler that supports format 0.2.0. Older 0.1.x readers reject the newer minor version before loading the package. Update the compiler and recompile the standard library, stdx, and dependent packages together before using local-mode packages. Rebuild existing `.cjo` artifacts with the updated toolchain rather than mixing generated artifacts from the two toolchains.

The updated reader continues to accept earlier minor versions in the same major version under the existing compatibility rules; this does not add local-mode support to an older compiler.
