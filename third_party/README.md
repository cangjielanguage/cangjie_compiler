# 使用的开源软件说明

## libboundscheck

### 代码来源说明

该仓被编译器及周边组件源码依赖，仓库源码地址为 [third_party_bounds_checking_function](https://gitcode.com/openharmony/third_party_bounds_checking_function)，版本为 [OpenHarmony-v6.0-Release](https://gitcode.com/openharmony/third_party_bounds_checking_function/tags/OpenHarmony-v6.0-Release)。

该开源软件被编译器及周边组件以包含头文件的方式使用，并通过链接库（动态库或静态库）的方式依赖。

### 构建说明

本仓库通过 CMake 作为子目标项目引入，编译过程中会自动完成构建并建立依赖关系，具体构建参数请参见 [CMakeLists.txt](./cmake/CMakeLists.txt) 文件。

## flatbuffers

### 代码来源说明

FlatBuffers 是一个高效的跨平台、跨语言序列化库，仓颉语言使用 FlatBuffers 库完成编译器数据到指定格式的序列化反序列化操作。

该仓被编译器及标准库源码依赖，其基于开源代码 [flatbuffers OpenHarmony-v6.0-Release](https://gitcode.com/openharmony/third_party_flatbuffers/tags/OpenHarmony-v6.0-Release) 进行定制化修改。

该开源软件被编译器及周边组件以包含头文件的方式使用，并通过链接库（动态库或静态库）的方式依赖。

### 三方库 patch 说明

为了提供 C++ 到仓颉语言的序列化反序列化能力，本项目对 FlatBuffers 库进行定制化修改，导出为 [flatbufferPatch.diff](./flatbufferPatch.diff) 文件。

### 构建说明

本仓库通过 CMake 作为子目标项目引入，编译过程中会自动完成构建并建立依赖关系，具体构建参数请参见 [Flatbuffer.cmake](./cmake/Flatbuffer.cmake) 文件。

开发者也可以手动下载 [flatbuffers](https://gitcode.com/openharmony/third_party_flatbuffers.git) 源码，并应用 patch 文件，命令如下：

```shell
mkdir -p third_party/flatbuffers
cd third_party/flatbuffers
git clone https://gitcode.com/openharmony/third_party_flatbuffers.git -b OpenHarmony-v6.0-Release ./
git apply ../flatbufferPatch.diff
```

构建项目时，则直接使用 third_party/flatbuffers 目录源码进行构建。

## LLVM

### 代码来源说明

LLVM 作为仓颉编译器后端，当前基于官方代码仓 [llvmorg-15.0.4](https://gitcode.com/openharmony/third_party_llvm-project)（对应commit 5c68a1cb123161b54b72ce90e7975d95a8eaf2a4）开源版本修改实现。为了便于代码管理以及支持鸿蒙版本构建，LLVM 在构建时来源有两种：

- 来源于仓库 https://gitcode.com/Cangjie/llvm-project/ ，使用此代码仓为默认方式，便于日常代码开发、检视和管理。

- 来源于仓库 https://gitcode.com/openharmony/third_party_llvm-project （llvmorg-15.0.4 对应 commit hash），并外加 llvmPatch.diff 进行构建，此方式主要为 OpenHarmony 构建版本时采用。

llvmPatch.diff 基于 https://gitcode.com/Cangjie/llvm-project/ 仓库改动经过验证后生成，每个版本都会保证 patch 可用。需要注意的是，LLVM 版本升级需结合 OpenHarmony 社区开源软件升级要求共同评估可行性，确保 OpenHarmony 版本可用。

### 构建说明

本仓库通过 CMake 作为子目标项目引入，编译过程中会自动完成代码构建并建立依赖关系。

当使用选项 "--use-oh-llvm-repo" 时，默认通过 [third_party_llvm-project](https://gitcode.com/openharmony/third_party_llvm-project) 仓代码外加 llvmPatch.diff 进行构建。

开发者也可以手动下载 [third_party_llvm-project](https://gitcode.com/openharmony/third_party_llvm-project) 源码，并应用 patch 文件，命令如下：

```shell
mkdir -p third_party/llvm-project
cd third_party/llvm-project
git clone -b master --depth 1 https://gitcode.com/openharmony/third_party_llvm-project ./
git fetch --depth 1 origin 5c68a1cb123161b54b72ce90e7975d95a8eaf2a4
git checkout 5c68a1cb123161b54b72ce90e7975d95a8eaf2a4
git apply --reject --whitespace=fix ../llvmPatch.diff
```

或直接拉取 [Cangjie/llvm-project](https://gitcode.com/Cangjie/llvm-project/)：

```shell
mkdir -p third_party/llvm-project
cd third_party/llvm-project
git clone -b dev --depth 1 https://gitcode.com/Cangjie/llvm-project.git ./
```

构建项目时，则直接使用 third_party/llvm-project 目录源码进行构建。

## MinGW-w64

### 代码来源说明

仓颉 Windows 版本 SDK 携带 MinGW 中的部分静态库文件，与仓颉代码生成的目标文件链接在一起，为用户生成最终的可以调用 Windows API 的可执行二进制文件。

该仓部分产物将被打包至仓颉发布包中，基于开源仓库 [third_party_mingw-w64 12.0.0](https://gitcode.com/openharmony/third_party_mingw-w64/commit/feea9a87fa42591b298b18fe0e07198f0b8c2f63?ref=master) 进行编译。

该开源软件被编译器及周边组件以包含头文件的方式使用，并通过链接库（动态库或静态库）的方式依赖。

### 构建说明

当编译目标平台为 Windows 时，需要依赖该开源软件。

该开源软件需在构建编译器之前完成，并且构建编译器时，需指定其编译产物路径。MinGW-w64 的详细构建流程请参阅 [构建指导书](https://gitcode.com/Cangjie/cangjie_build/blob/dev/docs/linux_cross_windows_zh.md#23-%E7%BC%96%E8%AF%91mingw-w64%E5%8F%8A%E9%85%8D%E5%A5%97%E5%B7%A5%E5%85%B7%E9%93%BE-%E5%85%B3%E9%94%AE%E6%AD%A5%E9%AA%A4)。
