# Cangjie Programming Language Compiler

## Introduction

Cangjie is a general-purpose programming language designed for all-scenario application development, balancing development efficiency and runtime performance while providing a great programming experience. Cangjie features concise and efficient syntax, multi-paradigm programming, and type safety. For more information, please refer to the [Cangjie Language Development Guide](https://cangjie-lang.cn/docs?url=%2F1.0.0%2Fuser_manual%2Fsource_zh_cn%2Ffirst_understanding%2Fbasic.html) and the [Cangjie Programming Language White Paper](https://cangjie-lang.cn/docs?url=%2F0.53.18%2Fwhite_paper%2Fsource_zh_cn%2Fcj-wp-abstract.html).

This repository provides the source code for the Cangjie compiler, which consists of two main parts: the compiler frontend and modified open-source LLVM components. The latter includes the LLVM backend, opt optimizer, llc, ld linker, and debugger. For details on third-party dependencies, see the [Third-Party Library Documentation](./third_party/README.md). The overall architecture is shown below:

![Architecture Diagram](figures/Compiler_Architecture_Diagram.png)

**Architecture Description**

- **Compiler Frontend**: Responsible for converting Cangjie source code from text to intermediate representation, including lexical, syntax, macro, and semantic analysis, ensuring code structure and semantics are correct, and preparing for backend code generation. This module depends on mingw-w64 to support Windows platform capabilities, enabling users to generate executable binaries that can call Windows APIs. It also relies on libboundscheck for safe function library access.

    - **Lexer** breaks down Cangjie source code into meaningful tokens.

    - **Parser** builds an Abstract Syntax Tree (AST) according to Cangjie grammar rules to reflect program structure.

    - **Semantic** performs type checking, type inference, and scope analysis on the AST to ensure semantic correctness.

    - **Mangler** handles symbol name mangling for Cangjie, and includes a demangler tool for reverse parsing.

    - **Package Management** manages and loads code modules, handles dependencies and namespace isolation, and supports multi-module collaborative development. This module uses the flatbuffer library for serialization and deserialization.

    - **Macro** handles macro expansion, processing macro definitions and calls for code generation and reuse.

    - **Condition Compile**: Conditional compilation allows compiling based on predefined or custom conditions; incremental compilation speeds up builds using previous compilation cache files.

    - **CHIR**: CHIR (Cangjie High Level IR) converts the AST to an intermediate representation and performs optimizations.

    - **Codegen**: Translates the intermediate representation (CHIR) to LLVM IR, preparing for target machine code (LLVM BitCode) generation.

- **LLVM**: Includes the compiler backend and related LLVM toolchain. The backend receives the intermediate representation from the frontend, optimizes it, generates target platform machine code, and links it into executable files.

    - **opt**: Performs various optimizations on LLVM IR, such as constant folding and loop optimization, to improve code efficiency and quality.

    - **llc**: Converts optimized LLVM IR to target platform machine code, supporting different hardware architectures.

    - **ld**: Links multiple object files and libraries into the final executable, resolving symbol references and generating deployable program artifacts.

    - **debugger**: Provides debugging capabilities for the Cangjie language.

For more details on the LLVM toolchain and backend tools, refer to the [LLVM Command Guide](https://llvm.org/docs/CommandGuide/).

- **OS**: The Cangjie compiler and LLVM toolchain currently support Windows x86-64, Linux x86-64/AArch64, and Mac x86/arm64. HarmonyOS support is under development. In addition to native compilation, the Cangjie compiler supports cross-compiling binaries for the ohos-aarch64 platform. For details, see the [Cangjie SDK Integration and Build Guide](https://gitcode.com/Cangjie/cangjie_build) and [Platform Support Roadmap](#platform-support-roadmap).

## Directory Structure

```text
cangjie_compiler/
├── cmake                       # CMake scripts for build assistance
├── demangler                   # Symbol demangling
├── doc                         # Documentation
├── figures                     # Documentation images
├── include                     # Header files
├── integration_build           # Cangjie SDK integration build scripts
├── schema                      # FlatBuffers schema files for serialization
├── src                         # Compiler source code
│   ├── AST                     # Abstract Syntax Tree
│   ├── Basic                   # Compiler basic components
│   ├── CHIR                    # Intermediate representation and optimization
│   ├── CodeGen                 # Code generation (CHIR to LLVM IR)
│   ├── ConditionalCompilation  # Conditional compilation
│   ├── Driver                  # Compiler driver (frontend/backend orchestration)
│   ├── Frontend                # Compiler instance and workflow
│   ├── FrontendTool            # Compiler instance for external tools
│   ├── IncrementalCompilation  # Incremental compilation
│   ├── Lex                     # Lexical analysis
│   ├── Macro                   # Macro expansion
│   ├── main.cpp                # Compiler entry point
│   ├── Mangle                  # Symbol mangling
│   ├── MetaTransformation      # Metaprogramming plugins
│   ├── Modules                 # Module management
│   ├── Option                  # Compiler options
│   ├── Parse                   # Syntax analysis
│   ├── Sema                    # Semantic analysis
│   └── Utils                   # Utilities
├── third_party                 # Third-party build scripts and patch files
│   ├── cmake                   # Third-party CMake scripts
│   ├── llvmPatch.diff          # LLVM backend patch (includes llvm and cjdb sources)
│   └── flatbufferPatch.diff    # Flatbuffer source patch
├── unittests                   # Unit tests
└── utils                       # Auxiliary tools
```

## Constraints

Currently, building Cangjie compiler artifacts directly in the Windows environment is not supported. Instead, you need to generate compiler artifacts that can run on Windows through cross-compilation in a Linux environment. For details, see the [Cangjie SDK Integration Build Guide](https://gitcode.com/Cangjie/cangjie_build/blob/dev/README_zh.md). For future support plans, refer to the [Platform Support Roadmap](#platform-support-roadmap).

## Platform Support Roadmap
- Build Platform Evolution: Planned support for Windows Native builds of compiler artifacts by the end of 2025.

- Compiler Runtime Platform Evolution: Planned support for running the compiler on the ohos (HarmonyOS PC) platform in 2026.

- Cangjie Application Runtime Platform Evolution: Planned support for ohos-arm32 core features on 2025-09-30, but reflection and certain optimization features will not be included for the time being.

## Building from Source

> **Note:**
>
> This section describes how to build the Cangjie compiler from source. If you only want to use the compiler to build Cangjie code or projects, skip this section and download the release package from the [official website](https://cangjie-lang.cn/download) or refer to the [Integration Build Guide](#integration-build-guide).

### Preparation

For environment requirements and software dependencies on each platform, see the [Standalone Build Guide](doc/Standalone_Build_Guide.md).

Clone the source code:

```shell
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```

### Build Steps

```shell
cd cangjie_compiler
python3 build.py clean
python3 build.py build -t release
python3 build.py install
```

1. The `clean` command removes temporary files from the workspace.
2. The `build` command starts compilation. The `-t` or `--build-type` option specifies the build type: `release`, `debug`, or `relwithdebinfo`.
3. The `install` command installs the build artifacts to the `output` directory.

The `output` directory structure:

```text
./output
├── bin
│   ├── cjc                 # Cangjie compiler executable
│   └── cjc-frontend -> cjc # Cangjie compiler frontend executable (symlink)
├── envsetup.sh             # Environment setup script
├── include                 # Public headers for the frontend
├── lib                     # Compiler libraries (by target platform)
├── modules                 # Standard library cjo files (by target platform)
├── runtime                 # Runtime libraries
├── third_party             # Third-party binaries and libraries (e.g., LLVM)
└── tools                   # Cangjie tools
```

On Linux, run `source ./output/envsetup.sh` to set up the environment, then use `cjc -v` to check the compiler version and platform info:

```shell
source ./output/envsetup.sh
cjc -v
```

Example output:

```text
Cangjie Compiler: x.xx.xx (cjnative)
Target: xxxx-xxxx-xxxx
```

### Run Unittest

Unit tests are built by default. After a successful build, run:

```shell
python3 build.py test
```

### More Build Options

For more build options, please refer to the [build.py build script](./build.py) or use the `--help` option:

```shell
python3 build.py --help
```

For more platform-specific build information, see the [Standalone Build Guide](doc/Standalone_Build_Guide.md).

### Integration Build Guide

For integration builds, please refer to the [Cangjie SDK Integration Build Guide](https://gitcode.com/Cangjie/cangjie_build/blob/dev/README_zh.md).

## License

This project is licensed under [Apache-2.0 with Runtime Library Exception](./LICENSE). Feel free to use and contribute!

## Related Repositories

- [cangjie_docs](https://gitcode.com/Cangjie/cangjie_docs/tree/main/docs/dev-guide)
- [cangjie_runtime](https://gitcode.com/openharmony-sig/third_party_cangjie_runtime)
- [cangjie_tools](https://gitcode.com/openharmony-sig/third_party_cangjie_tools)
- [cangjie_stdx](https://gitcode.com/openharmony-sig/third_party_cangjie_stdx)
- [cangjie_build](https://gitcode.com/Cangjie/cangjie_build)
- [cangjie_test](https://gitcode.com/Cangjie/cangjie_test)

## Open Source Software Statement

| Software Name       | License                              | Usage Description                                                                                                                       | Main Component              | Usage Methods                               |
|---------------------|--------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------|-----------------------------|---------------------------------------------|
| mingw-w64           | Zope Public License V2.1             | The Cangjie Windows SDK includes some static libraries from Mingw, linked with Cangjie-generated objects to produce Windows executables | Compiler                    | Integrated into the Cangjie binary release  |
| LLVM                | Apache 2.0 with LLVM Exception       | Cangjie compiler backend is based on LLVM.                                                                                              | Compiler                    | Integrated into the Cangjie binary release  |
| flatbuffers         | Apache License V2.0                  | Used for serialization/deserialization of cjo files and macros                                                                          | Compiler & StdLib(std.ast)  | Integrated into the Cangjie binary release  |
| libboundscheck      | Mulan Permissive Software License V2 | Used for safe function implementations in the compiler and related code                                                                 | Compiler, StdLib, Extension | Integrated into the Cangjie binary release  |

For details on other build dependencies, see [Build Dependencies](https://gitcode.com/Cangjie/cangjie_build/blob/dev/docs/env_zh.md) and the [Cangjie SDK Integration Build Guide](https://gitcode.com/Cangjie/cangjie_build/blob/dev/README_zh.md).

## Contribution

We welcome contributions from developers in any form, including but not limited to code, documentation, issues, and more.
