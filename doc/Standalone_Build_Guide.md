# 独立构建指导书

## 规格

当前前端编译器构建支持：

1. [Linux 上构建 Linux 平台运行的编译器](#linux-上构建-linux-平台运行的编译器)
2. [MacOS 上构建 MacOS 平台运行的编译器](#macos-上构建-macos-平台运行的编译器)
3. [Linux 上构建 Windows 平台运行的编译器（交叉编译）](#linux-上构建-windows-平台运行的编译器交叉编译)
4. [Linux 上构建适用于 Android 平台的基础库（交叉编译）](#linux-上构建适用于-android-平台的基础库交叉编译)
5. [MacOS 上构建适用于 Android 平台的基础库（交叉编译）](#macos-上构建适用于-android-平台的基础库交叉编译)
6. [MacOS 上构建适用于 iOS 平台的基础库（交叉编译）](#macos-上构建适用于-ios-平台的基础库交叉编译)

## Linux 上构建 Linux 平台运行的编译器

### 环境依赖

编译器独立构建环境除额外依赖 googletest 执行 UT 外，其他内容与集成构建环境基本一致，详细信息请参阅 [Cangjie 构建指导书 (Ubuntu 22.04)-环境准备](https://gitcode.com/Cangjie/cangjie_build/blob/dev/docs/linux_zh.md#2-%E7%8E%AF%E5%A2%83%E5%87%86%E5%A4%87)。
googletest 依赖安装可参考 [通用构建指导](https://github.com/google/googletest/blob/main/googletest/README.md)，也可以在构建时通过 [--no-test](#build-选项) 选项临时关闭 UT 构建。

### 构建命令

下载源码：

> **注意：**
>
> 请确保编译平台能够正常连接网络并且正常访问 Gitcode 或 Gitee 等代码托管平台。

```shell
export WORKSPACE=$HOME/cangjie_build;
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```

对源码进行编译：

```shell
cd $WORKSPACE/cangjie_compiler;
export CMAKE_PREFIX_PATH=/opt/buildtools/libedit-3.1:/opt/buildtools/ncurses-6.3/usr;
python3 build.py clean;
python3 build.py build -t release --build-cjdb;
python3 build.py install;
```

1. `build.py clean` 命令用于清空工作区临时文件。
2. `build.py build` 命令开始执行编译：
    - 二级选项 `-t` 即 `--build-type`，指定编译产物类型，可以是 `release`、`debug` 或 `relwithdebuginfo`。
    - 二级选项 `--build-cjdb` 选项开启 cjdb(lldb) 编译，了解更多更多关于 cjdb 内容，请参阅 [`cjdb` 工具介绍](https://gitcode.com/Cangjie/cangjie_docs/blob/main/docs/tools/source_zh_cn/cmd-tools/cjdb_manual.md)。
3. `build.py install` 命令将编译产物安装到 `output` 目录下。

验证产物：

```shell
source ./output/envsetup.sh
cjc -v
```

输出如下：

```text
Cangjie Compiler: x.xx.xx (cjnative)
Target: xxxx-xxxx-xxxx
```

## MacOS 上构建 MacOS 平台运行的编译器

### 环境准备

编译器独立构建环境除额外依赖 googletest 执行 UT 外，其他内容与集成构建环境基本一致，详细信息请参阅 [Cangjie 构建指导书 (Macos 14 Sonoma)-环境准备](https://gitcode.com/Cangjie/cangjie_build/blob/dev/docs/macos_zh.md#2-%E7%8E%AF%E5%A2%83%E5%87%86%E5%A4%87)。
googletest 依赖安装可参考 [通用构建指导](https://github.com/google/googletest/blob/main/googletest/README.md)，也可以在构建时通过 [--no-test](#build-选项) 选项临时关闭 UT 构建。

### 构建命令

下载源码：

> **注意：**
>
> 请确保编译平台能够正常连接网络并且正常访问 Gitcode 或 Gitee 等代码托管平台。

```shell
export WORKSPACE=$HOME/cangjie_build;
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```

对源码进行编译：

```shell
cd $WORKSPACE/cangjie_compiler;
python3 build.py clean;
python3 build.py build -t release --build-cjdb;
python3 build.py install;
```

1. `build.py clean` 命令用于清空工作区临时文件。
2. `build.py build` 命令开始执行编译：
    - 二级选项 `-t` 即 `--build-type`，指定编译产物类型，可以是 `release`、`debug` 或 `relwithdebuginfo`。
    - 二级选项 `--build-cjdb` 选项开启 cjdb(lldb) 编译，了解更多更多关于 cjdb 内容，请参阅 [`cjdb` 工具介绍](https://gitcode.com/Cangjie/cangjie_docs/blob/main/docs/tools/source_zh_cn/cmd-tools/cjdb_manual.md)。
3. `build.py install` 命令将编译产物安装到 `output` 目录下。

验证产物：

```shell
source ./output/envsetup.sh
cjc -v
```

输出如下：

```text
Cangjie Compiler: x.xx.xx (cjnative)
Target: xxxx-xxxx-xxxx
```

## Linux 上构建 Windows 平台运行的编译器（交叉编译）

### 环境准备

编译器独立构建环境除额外依赖 googletest 执行 UT 外，其他内容与集成构建环境基本一致，详细信息请参阅 [Cangjie 构建指导书 (Ubuntu 22.04)-环境准备](https://gitcode.com/Cangjie/cangjie_build/blob/dev/docs/linux_cross_windows_zh.md#2-%E7%8E%AF%E5%A2%83%E5%87%86%E5%A4%87)。
googletest 依赖安装可参考 [通用构建指导](https://github.com/google/googletest/blob/main/googletest/README.md)，也可以在构建时通过 [--no-test](#build-选项) 选项临时关闭 UT 构建。

> **注意：**
>
> 请确保编译平台能够正常连接网络并且正常访问 Gitcode 或 Gitee 等代码托管平台。

### 构建命令

下载源码：

```shell
export WORKSPACE=$HOME/cangjie_build;
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```

源码构建：

```shell
cd $WORKSPACE/cangjie_compiler;
export CMAKE_PREFIX_PATH=${MINGW_PATH}/x86_64-w64-mingw32;
python3 build.py build -t release \
	--product cjc \
	--target windows-x86_64 \
	--target-sysroot /opt/buildtools/mingw-w64/ \
	--target-toolchain /opt/buildtools/mingw-w64/bin \
	--build-cjdb;
python3 build.py install --host windows-x86_64;
```

1. `CMAKE_PREFIX_PATH` 环境变量用来指定 cmake 用于将产物生成到目标平台对应的文件夹中。
2. `build.py clean` 命令用于清空工作区临时文件。
3. `build.py build` 命令开始执行编译：
    - 二级选项 `-t` 即 `--build-type`，可以是 `release`、`debug` 或 `relwithdebuginfo`。
    - 二级选项 `--product` 指定构建目标产物， 可以是 `all`、`cjc` 或 `libs`。
    - 二级选项 `--target` 选项指定目标平台描述，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`ios-aarch64`、`android-aarch64`、`android-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`。
    - 二级选项 `--target-sysroot` 选项将后面的参数传递给 C/C++ 编译器作为其 `--sysroot` 参数。
    - 二级选项 `--target-toolchain` 选项指定目标平台工具链路径，使用该路径下的编译器进行交叉编译。
    - 二级选项 `--build-cjdb` 选项开启 cjdb(lldb) 编译，了解更多更多关于 cjdb 内容，请参阅 [`cjdb` 工具介绍](https://gitcode.com/Cangjie/cangjie_docs/blob/main/docs/tools/source_zh_cn/cmd-tools/cjdb_manual.md)。
4. `build.py install` 命令将编译产物安装到 `output` 目录下：
    - 二级选项 `--host` 选项指定目标平台安装策略，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`android-aarch64`、`android-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`。

验证产物：

由于编译产物为 Windows 平台可执行文件，需要将产物拷贝至 Windows，并使用 ./output/envsetup.bat 脚本应用 cjc 环境。

```bash
source ./output/envsetup.bat
cjc.exe -v
```

该步骤仅生成目标平台 cjc 可执行文件，如需构建周边依赖请参阅 [Cangjie 构建指导书 (Ubuntu 22.04)-源码构建](https://gitcode.com/Cangjie/cangjie_build/blob/dev/docs/linux_cross_windows_zh.md#4-%E7%BC%96%E8%AF%91%E6%B5%81%E7%A8%8B)。

## Linux 上构建适用于 Android 平台的基础库（交叉编译）

### 环境准备

编译器独立构建环境除额外依赖 googletest 执行 UT 外，其他内容与集成构建环境基本一致，详细信息请参阅 Cangjie 构建指导书 (Ubuntu 22.04)-环境准备。
googletest 依赖安装可参考 [通用构建指导](https://github.com/google/googletest/blob/main/googletest/README.md)，也可以在构建时通过 [--no-test](#build-选项) 选项临时关闭 UT 构建。

> **注意：**
>
> 请确保编译平台能够正常连接网络并且正常访问 Gitcode 或 Gitee 等代码托管平台。

### 构建命令

下载源码：

```shell
export WORKSPACE=$HOME/cangjie_build;
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```

源码构建：

```shell
cd $WORKSPACE/cangjie_compiler;
export ANDROID_NDK_ROOT=/opt/Android-NDK-r25c/AndroidNDK9519653.app/Contents/NDK;
python3 build.py build -t release --build-cjdb --no-tests;
python3 build.py build -t release \
	--target android-aarch64 \
	--android-ndk ${ANDROID_NDK_ROOT} \
    --no-tests;
python3 build.py install --host android-aarch64;
```

1. `ANDROID_NDK_ROOT` 环境变量用来设置 Android NDK 的路径。
2. `build.py clean` 命令用于清空工作区临时文件。
3. `build.py build` 命令开始执行编译：
    - 二级选项 `-t` 即 `--build-type`，可以是 `release`、`debug` 或 `relwithdebuginfo`。
    - 二级选项 `--product` 指定构建目标产物， 可以是 `all`、`cjc` 或 `libs`。
    - 二级选项 `--target` 选项指定目标平台描述，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`ios-aarch64`、`android-aarch64`、`android-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`。
    - 二级选项 `--android-ndk` 选项指定 Android NDK 的路径。
    - 二级选项 `--build-cjdb` 选项开启 cjdb(lldb) 编译，了解更多更多关于 cjdb 内容，请参阅 [`cjdb` 工具介绍](https://gitcode.com/Cangjie/cangjie_docs/blob/main/docs/tools/source_zh_cn/cmd-tools/cjdb_manual.md)。
4. `build.py install` 命令将编译产物安装到 `output` 目录下：
    - 二级选项 `--host` 选项指定目标平台安装策略，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`android-aarch64`、`android-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`。

## MacOS 上构建适用于 Android 平台的基础库（交叉编译）

### 环境准备

编译器独立构建环境除额外依赖 googletest 执行 UT 外，其他内容与集成构建环境基本一致，详细信息请参阅 Cangjie 构建指导书 (Macos 14 Sonoma)-环境准备。
googletest 依赖安装可参考 [通用构建指导](https://github.com/google/googletest/blob/main/googletest/README.md)，也可以在构建时通过 [--no-test](#build-选项) 选项临时关闭 UT 构建。

### 构建命令

下载源码：

> **注意：**
>
> 请确保编译平台能够正常连接网络并且正常访问 Gitcode 或 Gitee 等代码托管平台。

```shell
export WORKSPACE=$HOME/cangjie_build;
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```
源码构建：

```shell
cd $WORKSPACE/cangjie_compiler;
export ANDROID_NDK_ROOT=/opt/Android-NDK-r25c/AndroidNDK9519653.app/Contents/NDK
python3 build.py build -t release --build-cjdb --no-tests;
python3 build.py build  -t release \
    --target android-aarch64 \
    --android-ndk ${ANDROID_NDK_ROOT} \
    --no-tests;
python3 build.py install --host android-aarch64
```

1. `ANDROID_NDK_ROOT` 环境变量用来设置 Android NDK 的路径。
2. `build.py clean` 命令用于清空工作区临时文件。
3. `build.py build` 命令开始执行编译：
    - 二级选项 `-t` 即 `--build-type`，可以是 `release`、`debug` 或 `relwithdebuginfo`。
    - 二级选项 `--product` 指定构建目标产物， 可以是 `all`、`cjc` 或 `libs`。
    - 二级选项 `--target` 选项指定目标平台描述，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`ios-aarch64`、`android-aarch64`、`android-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`。
    - 二级选项 `--android-ndk` 选项指定 Android NDK 的路径。
    - 二级选项 `--build-cjdb` 选项开启 cjdb(lldb) 编译，了解更多更多关于 cjdb 内容，请参阅 [`cjdb` 工具介绍](https://gitcode.com/Cangjie/cangjie_docs/blob/main/docs/tools/source_zh_cn/cmd-tools/cjdb_manual.md)。
4. `build.py install` 命令将编译产物安装到 `output` 目录下：
    - 二级选项 `--host` 选项指定目标平台安装策略，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`android-aarch64`、`android-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`。

## MacOS 上构建适用于 iOS 平台的基础库（交叉编译）

### 环境准备

编译器独立构建环境除额外依赖 googletest 执行 UT 外，其他内容与集成构建环境基本一致，详细信息请参阅 Cangjie 构建指导书 (Macos 14 Sonoma)-环境准备。
googletest 依赖安装可参考 [通用构建指导](https://github.com/google/googletest/blob/main/googletest/README.md)，也可以在构建时通过 [--no-test](#build-选项) 选项临时关闭 UT 构建。

### 构建命令

下载源码：

> **注意：**
>
> 请确保编译平台能够正常连接网络并且正常访问 Gitcode 或 Gitee 等代码托管平台。

```shell
export WORKSPACE=$HOME/cangjie_build;
git clone https://gitcode.com/Cangjie/cangjie_compiler.git -b main;
```
源码构建：

```shell
# 编译 mac aarch64 host
cd ${WORKSPACE}/cangjie_compiler;
python3 build.py clean;
python3 build.py build -t release --no-tests --build-cjdb;
python3 build.py install;

# 编译 mac ios libs
export XCODE_DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export IOS_SDKROOT=${XCODE_DEVELOPER_DIR}/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS17.5.sdk
export IOS_SIMULATOR_SDKROOT=${XCODE_DEVELOPER_DIR}/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator17.5.sdk
export TOOLCHAIN_BIN=${XCODE_DEVELOPER_DIR}/Toolchains/XcodeDefault.xctoolchain/usr/bin
python3 build.py build -t release \
    --product libs \
    --target ios-aarch64 \
    --target-sysroot ${IOS_SDKROOT} \
    --target-toolchain ${TOOLCHAIN_BIN};
python3 build.py install;

# 编译 mac ios simulator libs
​python3 build.py build -t release \
    --product libs \
    --target ios-simulator-aarch64 \
    --target-sysroot ${IOS_SIMULATOR_SDKROOT} \
    --target-toolchain ${TOOLCHAIN_BIN};
python3 build.py install;
```

1. `XCODE_DEVELOPER_DIR` 环境变量用来设置 Xcode 的开发目录。
2. `IOS_SDKROOT` 环境变量用来设置 iPhoneOS 的 SDK 路径。
3. `IOS_SIMULATOR_SDKROOT` 环境变量用来设置 iOS 模拟器的 SDK 路径。
4. `TOOLCHAIN_BIN` 环境变量用来设置 Xcode 工具链的二进制文件路径。
5. `build.py clean` 命令用于清空工作区临时文件。
6. `build.py build` 命令开始执行编译：
    - 二级选项 `-t` 即 `--build-type`，可以是 `release`、`debug` 或 `relwithdebuginfo`。
    - 二级选项 `--product` 指定构建目标产物， 可以是 `all`、`cjc` 或 `libs`。
    - 二级选项 `--target` 选项指定目标平台描述，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`ios-aarch64`、`android-aarch64`、`android-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`。
    - 二级选项 `--target-sysroot` 选项将后面的参数传递给 C/C++ 编译器作为其 `--sysroot` 参数。
    - 二级选项 `--target-toolchain` 选项指定目标平台工具链路径，使用该路径下的编译器进行交叉编译。
    - 二级选项 `--build-cjdb` 选项开启 cjdb(lldb) 编译，了解更多更多关于 cjdb 内容，请参阅 [`cjdb` 工具介绍](https://gitcode.com/Cangjie/cangjie_docs/blob/main/docs/tools/source_zh_cn/cmd-tools/cjdb_manual.md)。
7. `build.py install` 命令将编译产物安装到 `output` 目录下：
    - 二级选项 `--host` 选项指定目标平台安装策略，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`。

## build.py 选项帮助

### `clean` 选项

`clean` 选项用于清理 build/output 等文件夹。

### `build` 选项

`build` 选项用于构建工程文件。它提供了如下二级选项：

- `-h, --help`：用于展示二级选项的帮助信息。
- `-t, --build-type`：用于指定编译产物类型，可以是 `release`、`debug` 或 `relwithdebuginfo`。
- `--print-cmd`：用于展示构建脚本配置的完整 cmake 命令。
- `-j, --jobs JOBS`：并发执行构建任务数。
- `--link-jobs LINK_JOBS`：并发执行链接任务数。
- `--enable-assert`：使能编译器断言，开发调试编译器使用。
- `--no-tests`：不编译 unittest 用例代码。
- `--disable-stack-grow-feature`：关闭栈增长功能。
- `--hwasan`：开启编译器源码硬件 asan 功能，由于其依赖 hwasan 工具，目前仅支持 ohos 平台开启。
- `--gcc-toolchain`：指定 gcc 工具链，它用于交叉编译。
- `--target`：选项指定目标平台描述，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`、`android-aarch64`、`android-x86_64`。
- `-L, --target-lib`：指定目标平台所需链接的依赖库路径。
- `--target-toolchain`：指定编译工具所在的路径。
- `-I, --include` 指定目标平台头文件查找路径。
- `--target-sysroot`：传递 sysroot 内容到 C/C++ 编译器的 sysroot 选项。
- `--product {all,cjc,libs}`：指定构建目标产物，可以是`all`(默认值，指定编译包含 `cjc` 和 `libs` 内容)、`cjc`(编译器二进制文件)、`libs`(标准库依赖的编译器库)。
- `--build-cjdb`：开启构建仓颉调试器。

### `install` 选项

`install` 选项用于将编译产物组织到指定目录下。它提供了如下二级选项：

- `-h, --help`：用于展示二级选项的帮助信息。
- `--host`：指定目标平台安装策略，可以是 `native`(当前编译平台)、`windows-x86_64`、`ohos-aarch64`、`ohos-x86_64`、`ios-simulator-aarch64`、`ios-aarch64`、`android-aarch64`、`android-x86_64`。
- `--prefix`：指定产物安装文件夹路径，该选项和 `--host` 均未指定时，安装到工程目录下 output 文件夹，与 `--host` 同时指定时，安装到 `--prefix` 指定路径。

### `test` 选项

`test` 选项用于执行编译好的 unittest 用例，如果编译时指定 `--no-test` 时无作用。
