# ArkTS language SDK package

This directory temporarily hosts a fixed SDK source project in the compiler
repository. The project can later move to its own repository without changing
the package names. Applications configure a reviewed, prebuilt SDK path and
explicitly import `arkts.lang.*`.

`sdk/` is a CJPM workspace containing the `arkts` and
`cj_arkts_context_v1` dynamic modules. `arkts.lang` publicly reexports the
existing `ohos.ark_interop` APIs, `std.math.numeric.BigInt`, and the three context
functions. The context implementation remains ABI 1: one `ThreadLocal` state,
enter/current/restore, and the existing native-environment checks. There is no
application ID in the source or package identity.

Build the package once per target and toolchain combination, outside this
repository, using `build_sdk.py`. The driver runs real `cjpm build` against a
complete Cangjie toolchain and the existing OHOS SDK/NDK. It copies the fixed
project to a fresh work directory, configures the external binary dependency,
and retains the command, versions, input hashes, logs and product manifest in
the output directory. Windows directory junctions avoid CJPM 1.2 path quoting
limitations; the work and output paths must have no whitespace or command
metacharacters. The driver neither installs nor replaces SDK files.

```powershell
python utils/arkts/build_sdk.py --target x86_64-linux-ohos `
  --sdk-root <cangjie-sdk-root> --native-root <ohos-native-root> `
  --work-dir <fresh-external-work> --output-dir <fresh-external-output>
```

The output contains `arkts-sdk-manifest.json` and these ordinary CJPM products:

```text
modules/
  arkts/
    arkts.cjo
    arkts.lang.cjo
    libarkts.so
    libarkts.lang.so
  cj_arkts_context_v1/
    cj_arkts_context_v1.cjo
    libcj_arkts_context_v1.so
```

For direct compiler use, add `modules/` as an import root and configure the two
library directories for linking. For CJPM, add `modules/arkts` and
`modules/cj_arkts_context_v1` to the target's `bin-dependencies.path-option`.
Retain the existing OHOS/stdlib dependencies. Package all three shared libraries
once in the final application; all consumers must resolve the same
`libcj_arkts_context_v1.so`. The facade and root package have normal package
initialization artifacts and are part of the link/package contract. CJO files
alone do not satisfy it.

Application packaging belongs in the application build. Copy the manifest's
three shared libraries into the owning module's `libs/<architecture>` directory,
verify their hashes and package each library once. Every other consumer uses the
same binary dependency directories. Compilation and ELF inspection do not prove
runtime context isolation; validate it in the paired real applications.

The checked configuration targets API26 with its CJPM 1.2 toolchain layout and
the paired compiler supporting CJO 0.2.0. `cjc-version = "1.2.0"` is the CJPM
package compatibility declaration, not a promise that arbitrary compiler or SDK
versions can be mixed. `--toolchain-root` selects a complete compatible toolchain;
retain the SDK's standard-library/runtime files and record the actual compiler,
CJPM and dependency identities in the generated manifest.

A source file explicitly imports the facade before declaring or calling a
foreign function. For example, if `my.module` exports `echo`:

```cangjie
package entry

import arkts.lang.*

@ArkTS[module: "my.module"]
foreign func echo(value: String): String

@ArkTS
public func run(): Unit {
    println(echo("hello"))
}
```

The ArkTS application calls the generated `run` entry. That entry establishes
and restores its calling context; user code does not pass `JSContext`. Omitting
`module` uses the declaration's exact package name, `entry` in this example.
The facade reexports the existing types; their defining packages and mangled
identities remain unchanged. It does not define replacement `JSContext` or
`BigInt` types. Full type/package separation follows in later requirements.
