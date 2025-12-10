#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

"""cangjie compiler build entry"""

import argparse
import logging
import multiprocessing
import os
import platform
import re
import tarfile
import shutil
import stat
import subprocess
import sys
from enum import Enum
from logging.handlers import TimedRotatingFileHandler
from pathlib import Path
from subprocess import DEVNULL, PIPE

HOME_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(HOME_DIR, "build")
CMAKE_BUILD_DIR = os.path.join(BUILD_DIR, "build")
CMAKE_OUTPUT_DIR = os.path.join(HOME_DIR, "output")
OUTPUT_BIN_DIR = os.path.join(CMAKE_OUTPUT_DIR, "bin")
LOG_DIR = os.path.join(BUILD_DIR, "logs")
LOG_FILE = os.path.join(LOG_DIR, "cangjie.log")

IS_WINDOWS = platform.system() == "Windows"
IS_MACOS = platform.system() == "Darwin"
IS_ARM = platform.uname().processor in ["aarch64", "arm", "arm64"]
CJ_OS_NAME = platform.system().lower()
CJ_ARCH_NAME = platform.machine().replace("AMD64", "x86_64").lower() # normalize AMD64 in Windows to x86_64
CAN_RUN_CJNATIVE = (platform.system(), platform.machine()) in [
    ("Linux", "x86_64"),
    ("Linux", "aarch64"),
    ("Windows", "AMD64"),
    ("Darwin", "x86_64"),
    ("Darwin", "arm64")
]
LD_LIBRARY_PATH = "Path" if IS_WINDOWS else "DYLD_LIBRARY_PATH" if IS_MACOS else "LD_LIBRARY_PATH"
LIBRARY_PATH = "Path" if IS_WINDOWS else "LIBRARY_PATH"
PATH_ENV_SEPERATOR = ";" if IS_WINDOWS else ":"
# Wait for the version of aarch64 libcore to be ready.
DELTA_JOBS = 2
MAKE_JOBS = multiprocessing.cpu_count() + DELTA_JOBS

PYTHON_EXECUTABLE = sys.executable
TARGET_DICTIONARY = {
    "native": None,
    "ohos-aarch64": "aarch64-linux-ohos",
    "ohos-x86_64": "x86_64-linux-ohos",
    "windows-x86_64": "x86_64-w64-mingw32",
    "ios-simulator-aarch64": "arm64-apple-ios11-simulator",
    "ios-simulator-x86_64": "x86_64-apple-ios11-simulator",
    "ios-aarch64": "arm64-apple-ios11",
    "android-aarch64": "aarch64-linux-android31",
    "android31-aarch64": "aarch64-linux-android31",
    "android26-aarch64": "aarch64-linux-android26",
    "android-x86_64": "x86_64-linux-android",
    "android31-x86_64": "x86_64-linux-android31",
    "android26-x86_64": "x86_64-linux-android26"
}

def resolve_path(path):
    if os.path.isabs(path):
        return path
    return os.path.abspath(path)

def log_output(output, checker=None, args=[]):
    """log command output"""
    while True:
        line = output.stdout.readline()
        if not line:
            output.communicate()
            returncode = output.returncode
            if returncode != 0:
                LOG.error("build error: %d!\n", returncode)
                sys.exit(1)
            break
        try:
            LOG.info(line.decode("ascii", "ignore").rstrip())
        except UnicodeEncodeError:
            LOG.info(line.decode("utf-8", "ignore").rstrip())

def generate_version_tail(target):
    """generate version string for cjc compiler"""

    res = "{}".format("cjnative")
    res = " (" + res + ")"

    arch_name = platform.machine().replace("AMD64", "x86_64").replace("arm64", "aarch64").lower()
    vendor_name = "apple" if IS_MACOS else "unknown"
    os_name = None
    if IS_WINDOWS:
        os_name = "windows-gnu"
    elif IS_MACOS:
        os_name = "darwin"
    else:
        os_name = "linux-gnu"
    res += "\\nTarget: {}".format(target) if target else "\\nTarget: {0}-{1}-{2}".format(arch_name, vendor_name, os_name)
    return str(res)

def generate_cmake_defs(args):
    """convert args to cmake defs"""

    def bool_to_opt(value):
        return "ON" if value else "OFF"

    if args.target:
        if args.target == "aarch64-linux-ohos":
            toolchain_file = "ohos_aarch64_clang_toolchain.cmake"
        elif args.target == "arm-linux-ohos":
            toolchain_file = "ohos_arm_clang_toolchain.cmake"
        elif args.target == "x86_64-linux-ohos":
            toolchain_file = "ohos_x86_64_clang_toolchain.cmake"
        elif args.target == "x86_64-w64-mingw32":
            toolchain_file = "mingw_x86_64_toolchain.cmake"
        elif args.target == "arm64-apple-ios11-simulator":
            toolchain_file = "ios_simulator_arm64_toolchain.cmake"
        elif args.target == "x86_64-apple-ios11-simulator":
            toolchain_file = "ios_simulator_x86_64_toolchain.cmake"
        elif args.target == "arm64-apple-ios11":
            toolchain_file = "ios_arm64_toolchain.cmake"
        elif "aarch64-linux-android" in args.target:
            toolchain_file = "android_aarch64_toolchain.cmake"
        elif "x86_64-linux-android" in args.target:
            toolchain_file = "android_x86_64_toolchain.cmake"
    else:
        args.target = None
        if IS_WINDOWS:
            toolchain_file = "mingw_x86_64_toolchain.cmake"
        elif IS_MACOS:
            toolchain_file = "darwin_aarch64_toolchain.cmake" if IS_ARM else "darwin_x86_64_toolchain.cmake"
        elif IS_ARM:
            toolchain_file = "linux_aarch64_toolchain.cmake"
        else:
            toolchain_file = "linux_x86_64_toolchain.cmake"

    install_prefix = CMAKE_OUTPUT_DIR + ("-{}".format(args.target) if (args.target and args.product == "cjc") else "")

    result = [
        "-DCMAKE_BUILD_TYPE=" + args.build_type.value,
        "-DCMAKE_ENABLE_ASSERT=" + bool_to_opt(args.enable_assert),
        "-DCMAKE_INSTALL_PREFIX=" + install_prefix,
        "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(HOME_DIR, "cmake/" + toolchain_file),
        "-DCMAKE_PREFIX_PATH=" + (args.target_toolchain + "/../x86_64-w64-mingw32" if args.target_toolchain else ""),
        "-DCANGJIE_BUILD_TESTS=" + bool_to_opt((not args.no_tests) and (args.product == "all" or args.product == "libs")),
        "-DCANGJIE_BUILD_CJC=" + bool_to_opt(args.product in ['all', 'cjc']),
        "-DCANGJIE_BUILD_STD_SUPPORT=" + bool_to_opt(args.product in ['all', 'libs']),
        "-DCANGJIE_BUILD_CJDB=" + bool_to_opt(args.build_cjdb),
        "-DCANGJIE_ENABLE_HWASAN=" + bool_to_opt(args.hwasan),
        "-DCANGJIE_VERSION=" + generate_version_tail(args.target),
        "-DBUILD_GCC_TOOLCHAIN=" + (args.gcc_toolchain if args.gcc_toolchain and args.target is None else ""),
        "-DCANGJIE_TARGET_LIB=" + (";".join(args.target_lib) if args.target_lib else ""),
        "-DCANGJIE_TARGET_TOOLCHAIN=" + (args.target_toolchain if args.target_toolchain else ""),
        "-DCANGJIE_INCLUDE=" + (";".join(args.include) if args.include else ""),
        "-DCANGJIE_TARGET_SYSROOT=" + (args.target_sysroot if args.target_sysroot else ""),
        "-DCANGJIE_LINK_JOB_POOL=" + (str(args.link_jobs) if args.link_jobs != 0 else ""),
        "-DCANGJIE_DISABLE_STACK_GROW_FEATURE=" + bool_to_opt(args.disable_stack_grow_feature),
        "-DCANGJIE_USE_OH_LLVM_REPO=" + bool_to_opt(args.use_oh_llvm_repo),
        "-DCANGJIE_ENABLE_SANITIZE_OPTION=" + bool_to_opt(args.enable_sanitize_option)]

    if args.target and "aarch64-linux-android" in args.target:
        android_api_level = re.match(r'aarch64-linux-android(\d{2})?', args.target).group(1)
        result.append("-DCMAKE_ANDROID_NDK=" + (args.android_ndk if args.android_ndk else ""))
        result.append("-DCMAKE_ANDROID_API=" + (android_api_level if android_api_level else ""))

    if args.sanitizer_support:
        result.append("-DCANGJIE_SANITIZER_SUPPORT=" + args.sanitizer_support)
    return result

def download_and_patch_tinytoml():
    """Set up the tinytoml third-party library"""
    TINYTOML_DIR = os.path.join(HOME_DIR, "third_party", "tinytoml")
    PATCH_FILE = os.path.join(TINYTOML_DIR, "fixFloatEqualError.patch")
    TAR_PATH = os.path.join(TINYTOML_DIR, "tinytoml-0.4.tar.gz")
    TINYTOMLTAR_PATH = os.path.join(TINYTOML_DIR, "tinytoml-0.4")

    LOG.info("Setting up tinytoml...")

    # Create Parent Directory
    os.makedirs(os.path.dirname(TINYTOML_DIR), exist_ok=True)

    try:

        if not os.path.exists(TAR_PATH):
            # Fetch tinytoml from the remote repostory
            subprocess.run(
                ["git", "clone", "https://gitee.com/src-openeuler/tinytoml.git",
                 TINYTOML_DIR],
                 check=True
            )
            # Switch to the specified tag
            subprocess.run(
                ["git", "checkout", "openEuler-24.03-LTS-SP1"],
                cwd=TINYTOML_DIR,
                check=True
            )

        # Extract the package
        LOG.info("Extracting tinytoml package...")
        with tarfile.open(TAR_PATH) as tar:
            tar.extractall(path=TINYTOML_DIR)

        # Apply Patch
        patch_file_exists = os.path.exists(PATCH_FILE)
        target_dir_exists = os.path.exists(TINYTOMLTAR_PATH) and os.path.isdir(TINYTOMLTAR_PATH)
        if patch_file_exists and target_dir_exists:
            LOG.info("Applying tinytoml patch...")
            subprocess.run(
                f"patch -p0 -l -f < {PATCH_FILE}",
                cwd=TINYTOMLTAR_PATH,
                shell=True,
                check=True
            )
        else:
            LOG.warning(f"Patch file not found: {PATCH_FILE}")

        TOML_H_SRC = os.path.join(TINYTOMLTAR_PATH, "include", "toml", "toml.h")
        TOML_H_DST = os.path.join(TINYTOML_DIR, "toml.h")

        if os.path.exists(TOML_H_SRC):
            LOG.info(f"Copying toml.h from {TOML_H_SRC} to {TOML_H_DST}")
            shutil.copy2(TOML_H_SRC, TOML_H_DST)

    except subprocess.CalledProcessError as e:
        LOG.error(f"Failed to setup tinytoml: {str(e)}")
        # Clean Files
        if os.path.exists(TINYTOML_DIR):
            shutil.rmtree(TINYTOML_DIR)
        raise

def build(args):
    # The target also affects the prefix concatenation of the compiler executable file; it is specific.
    if args.target:
        args.target = TARGET_DICTIONARY[args.target]

    if args.gcc_toolchain and args.target and args.product != "cjc":
        LOG.warning("There is no intermediate or product targeting the host platform in this build, so --gcc-toolchain won't take effect")

    # Add the tinytoml third-party library for acquisition and customize code application.
    tinytoml_target = os.path.join(HOME_DIR, "third_party", "tinytoml", "toml.h")
    if not os.path.exists(tinytoml_target):
        download_and_patch_tinytoml()

    check_compiler(args)

    """build cangjie compiler"""
    LOG.info("begin build...")

    set_cjnative_backend_env(args)

    if args.product is None:
        args.product = "all" if args.target is None else "libs"

    if args.target == "aarch64-linux-ohos" or args.target == "x86_64-linux-ohos":
        # Frontend supports cross compilation in a general way by asking path to required tools
        # and libraries. However, Runtime supports cross compilation in a speific way, which asks
        # for the root path of OHOS toolchain. Since we asked for a path to tools, the root path of
        # OHOS toolchain is relative to the tool path we get. Tool path normally looks like
        # ${OHOS_ROOT}/prebuilts/clang/ohos/linux-x86_64/llvm/bin/. Six /.. can bring us to the root.
        os.environ["OHOS_ROOT"] = os.path.join(args.target_toolchain, "../../../../../..")

    if args.android_ndk:
        os.environ["ANDROID_NDK_ROOT"] = args.android_ndk

    if IS_WINDOWS:
        # For Windows, try Ninja first, otherwise use Make instead.
        ninja_valid = True
        try:
            output = subprocess.Popen(["ninja", "--version"], stdout=DEVNULL, stderr=DEVNULL)
            output.communicate()
            ninja_valid = output.returncode == 0
        except:
            ninja_valid = False

        if ninja_valid:
            generator = "Ninja"
            build_cmd = ["ninja"]
            if args.jobs > 0:
                build_cmd.extend(["-j", str(args.jobs)])
        else:
            generator = "MinGW Makefiles"
            build_cmd = ["mingw32-make", "-j" + str(MAKE_JOBS)]
    else:
        # For Linux, just use Ninja.
        generator = "Ninja"
        build_cmd = ["ninja"]
        if args.jobs > 0:
            build_cmd.extend(["-j", str(args.jobs)])

    cmake_command = ["cmake", HOME_DIR, "-G", generator] + generate_cmake_defs(args)

    if args.print_cmd:
        print(' '.join(cmake_command))
        exit(0)

    if args.target == "x86_64-w64-mingw32":
        package_mingw_dependencies(args)

    if not os.path.exists(BUILD_DIR):
        os.makedirs(BUILD_DIR)

    if args.sanitizer_support:
        cmake_build_dir = os.path.join(BUILD_DIR, "build-libs-{}".format(args.sanitizer_support))
        if args.target:
            cmake_build_dir += "-{}".format(args.target)
    else:
        cmake_build_dir = os.path.join(BUILD_DIR, "build-{}-{}".format(args.product, args.target)) if args.target else CMAKE_BUILD_DIR

    if not os.path.exists(os.path.join(cmake_build_dir, "build.ninja")):
        os.makedirs(cmake_build_dir, exist_ok=True)
        output = subprocess.Popen(cmake_command, cwd=cmake_build_dir, stdout=PIPE)
        log_output(output)

    output = subprocess.Popen(build_cmd, cwd=cmake_build_dir, stdout=PIPE)
    log_output(output)

    if output.returncode != 0:
        LOG.fatal("build failed")

    LOG.info("end build")

def set_cangjie_env(args):
    os.environ["HOME_DIR"] = HOME_DIR
    os.environ[LD_LIBRARY_PATH] = PATH_ENV_SEPERATOR.join([
        os.path.join(HOME_DIR, "output/lib"),
        os.path.join(HOME_DIR, "output/runtime/lib/{os}_{arch}_cjnative".format(os=CJ_OS_NAME, arch=CJ_ARCH_NAME)),
        os.environ.get(LD_LIBRARY_PATH, "")
    ])
    LOG.info("set cangjie env: %s=%s", LD_LIBRARY_PATH, os.environ[LD_LIBRARY_PATH])

def set_cjnative_backend_env(args):
    os.environ[LD_LIBRARY_PATH] = PATH_ENV_SEPERATOR.join([
        os.path.join(HOME_DIR, CMAKE_OUTPUT_DIR, "lib"),
        os.path.join(HOME_DIR, CMAKE_OUTPUT_DIR, "lib/{os}_{arch}_cjnative".format(os=CJ_OS_NAME, arch=CJ_ARCH_NAME)),
        os.path.join(HOME_DIR, "build/build/lib"),
        os.path.join(HOME_DIR, "build/build/lib/{os}_{arch}_cjnative".format(os=CJ_OS_NAME, arch=CJ_ARCH_NAME)),
        os.environ.get(LD_LIBRARY_PATH, "")
    ] + (args.target_lib if hasattr(args, 'target_lib') else []))
    LOG.info("set cjnative backend env: %s=%s", LD_LIBRARY_PATH, os.environ[LD_LIBRARY_PATH])

    os.environ[LIBRARY_PATH] = PATH_ENV_SEPERATOR.join([
        os.path.join(HOME_DIR, "build/build/lib"),
        os.environ.get(LIBRARY_PATH, "")
    ] + (args.target_lib if hasattr(args, 'target_lib') else []))
    LOG.info("set cjnative backend env: %s=%s", LIBRARY_PATH, os.environ[LIBRARY_PATH])

    os.environ["PATH"] = PATH_ENV_SEPERATOR.join([
        os.path.join(HOME_DIR, "output/tools/bin"),
        os.environ.get("PATH", "")])

    if IS_MACOS:
        os.environ["ZERO_AR_DATE"] = "1"

def test(args):
    """test"""
    LOG.info("begin test...")
    if platform.system() == "Linux" or platform.system() == "Darwin":
        set_cangjie_env(args)
    unit_test(args)
    LOG.info("end test...\n")

def unit_test(args):
    """unit test"""
    LOG.info("begin unit test...\n")
    output = subprocess.Popen(
        ["ctest", "--output-on-failure"], cwd=CMAKE_BUILD_DIR, stdout=PIPE
    )
    log_output(output)
    LOG.info("end unit test...\n")

def install(args):
    """install targets"""
    if args.host:
        args.host = TARGET_DICTIONARY[args.host]

    LOG.info("begin install targets...")
    targets = []

    # - If "build.py install" is invoked without "--host",
    #   the native build directories and all cross-compiled libs
    #   will be installed to the "output" directory.
    # - If "build.py install" is invoked with "--host <triple>",
    #   the build-cjc-<triple> directories and all cross-compiled libs
    #   will be installed to a seperated "output-<target>" directory.

    # Searching for cjc's build directory.
    if not args.host:
        if os.path.exists(CMAKE_BUILD_DIR):
            targets.append(("native", CMAKE_BUILD_DIR))
    else:
        suffix = "cjc-{}".format(args.host)
        cross_build_path = os.path.join(BUILD_DIR, "build-{}".format(suffix))
        if os.path.exists(cross_build_path):
            targets.append((suffix, cross_build_path))

    # Searching for all libs' build directories.
    for directory in os.listdir(BUILD_DIR):
        build_prefix = "build-libs-"
        if directory.startswith(build_prefix):
            targets.append(("libs-{}".format(directory[len(build_prefix):]), os.path.join(BUILD_DIR, directory)))

    if len(targets) == 0:
        LOG.fatal("Nothing is built yet.")
        sys.exit(1)

    # install for all build directories in the list
    for target in targets:
        LOG.info("installing {} build...".format(target[0]))
        cmake_cmd = ["cmake", "--install", "."]
        if args.prefix:
            cmake_cmd += ["--prefix", os.path.abspath(args.prefix)]
        elif args.host:
            cmake_cmd += ["--prefix", os.path.join(HOME_DIR, "output-{}".format(args.host))]

        output = subprocess.Popen(cmake_cmd, cwd=target[1], stdout=PIPE)
        log_output(output)
        if output.returncode != 0:
            LOG.fatal("install {} build failed".format(target[0]))
            sys.exit(1)

    if args.host == "x86_64-w64-mingw32":
        mingw_bin_path = os.path.join(HOME_DIR, "output-x86_64-w64-mingw32/third_party/mingw/bin")
        if os.path.exists(mingw_bin_path):
            for bin in os.listdir(mingw_bin_path):
                bin_path = os.path.join(mingw_bin_path, bin)
                if os.path.isfile(bin_path) and not bin.endswith(".exe") and bin != "libssp-0.dll":
                    os.remove(bin_path)
    LOG.info("end install targets...")

def redo_with_write(redo_func, path, err):
    # Is the error an access error?
    if not os.access(path, os.W_OK):
        os.chmod(path, stat.S_IWUSR)
        redo_func(path)
    else:
        raise

def clean(args):
    """clean build outputs and logs"""

    LOG.info("begin clean...\n")
    # In order to successfully delete all files in the build directory,
    # the file handlers (especially for cangjie.log) that this script holds must be closed first.
    handlerToBeRemoved = []
    for handler in LOG.handlers:
        if isinstance(handler, logging.FileHandler):
            handler.close()
            handlerToBeRemoved.append(handler)
    for handler in handlerToBeRemoved:
        LOG.removeHandler(handler)
    output_dirs = []

    # Remove entire build directory by default
    output_dirs.append("build")
    output_dirs.append("output")
    # Files in `cjnative_backend` directory may be used while installing if native build is built
    # from binary. It can only be removed when `build.py clean` is being applied to all build targets.
    output_dirs.append("third_party/binary/llvm_backend")

    # Collecting all cross-compiled cjcs' build directories.
    cross_output_suffix = []
    for directory in os.listdir(BUILD_DIR):
        build_prefix = "build-cjc-"
        if directory.startswith(build_prefix):
            cross_output_suffix.append(directory[len(build_prefix):])
    # Removing all cross-compiled output package.
    for suffix in cross_output_suffix:
        cross_output_path = os.path.join(HOME_DIR, "output-{}".format(suffix))
        if os.path.isdir(cross_output_path):
            output_dirs.append(cross_output_path)

    for file_path in output_dirs:
        abs_file_path = os.path.join(HOME_DIR, file_path)
        if os.path.isdir(abs_file_path):
            # If some build files are read-only and not allowed to be deleted (especially in Windows),
            # Try to deal with the error by giving write permissions and deleting them again.
            shutil.rmtree(abs_file_path, ignore_errors=False, onerror=redo_with_write)
        if os.path.isfile(abs_file_path):
            os.remove(abs_file_path)
    LOG.info("end clean\n")

def package_mingw_dependencies(args):
    if os.path.exists(os.path.join(HOME_DIR, "third_party/binary/windows-x86_64-mingw.tar.gz")):
        return
    LOG.info("Packaging mingw dependencies...")
    def copy_files_to(from_path, filename_list, to_directory):
        if not os.path.exists(to_directory):
            os.makedirs(to_directory, exist_ok=True)
        for filename in filename_list:
            filepath = os.path.join(from_path, filename)
            if os.path.exists(filepath):
                shutil.copy(filepath, to_directory + "/")
            else:
                LOG.info("%s not found, skipped packaging the file...", filepath)

    search_path = args.target_toolchain if args.target_toolchain else None
    mingw_path = str(os.path.dirname(os.path.dirname(os.path.abspath(shutil.which("x86_64-w64-mingw32-gcc", path=search_path)))))
    mingw_package_path = str(os.path.join(HOME_DIR, "third_party/binary/mingw"))
    dll_dependencies = ["bin/libc++.dll", "bin/libunwind.dll", "bin/libwinpthread-1.dll"]
    copy_files_to(os.path.join(mingw_path, "x86_64-w64-mingw32"), dll_dependencies, os.path.join(mingw_package_path, "dll"))
    lib_dependencies = ["crt2.o", "dllcrt2.o", "libadvapi32.a", "libkernel32.a", "libm.a", "libmingw32.a",
                            "libmingwex.a", "libmoldname.a", "libmsvcrt.a", "libpthread.a", "libshell32.a", "libuser32.a",
                            "libws2_32.a", "libcrypt32.a", "crtbegin.o", "crtend.o", "CRT_glob.o"]
    copy_files_to(os.path.join(mingw_path, "x86_64-w64-mingw32/lib"), lib_dependencies, os.path.join(mingw_package_path, "lib"))

    subprocess.run(["tar", "-C", "mingw", "-zcf", "windows-x86_64-mingw.tar.gz", "dll", "lib"],
        cwd=os.path.join(HOME_DIR, "third_party/binary"))
    
def init_log(name):
    """init log config"""
    if not os.path.exists(LOG_DIR):
        os.makedirs(LOG_DIR)

    log = logging.getLogger(name)
    log.setLevel(logging.DEBUG)
    formatter = logging.Formatter(
        "[%(asctime)s] [%(module)s] [%(levelname)s] %(message)s",
        "%Y-%m-%d %H:%M:%S"
    )
    streamhandler = logging.StreamHandler(sys.stdout)
    streamhandler.setLevel(logging.DEBUG)
    streamhandler.setFormatter(formatter)
    log.addHandler(streamhandler)
    filehandler = TimedRotatingFileHandler(
        LOG_FILE, when="W6", interval=1, backupCount=60
    )
    filehandler.setLevel(logging.DEBUG)
    filehandler.setFormatter(formatter)
    log.addHandler(filehandler)
    return log

class BuildType(Enum):
    """CMAKE_BUILD_TYPE options"""

    debug = "Debug"
    release = "Release"
    relwithdebinfo = "RelWithDebInfo"

    def __str__(self):
        return self.name

    def __repr__(self):
        return str(self)

    @staticmethod
    def argparse(s):
        try:
            return BuildType[s]
        except KeyError:
            return s.build_type

def main():
    """build entry"""
    parser = argparse.ArgumentParser(description="build cangjie project")
    subparsers = parser.add_subparsers(help="sub command help")

    parser_clean = subparsers.add_parser("clean", help="clean build", add_help=False)
    parser_clean.set_defaults(func=clean)

    parser_build = subparsers.add_parser("build", help=" build cangjie")
    parser_build.add_argument(
        "-t", "--build-type", type=BuildType.argparse, dest="build_type", choices=list(BuildType), required=True,
        help="select target build type"
    )
    parser_build.add_argument(
        "--print-cmd", action="store_true", help="print the command to cmake without running"
    )
    parser_build.add_argument(
        "-j", "--jobs", dest="jobs", type=int, default=0, help="run N jobs in parallel (0 means default)"
    )
    parser_build.add_argument(
        "--link-jobs", dest="link_jobs", type=int, default=0, help="max N link jobs in parallel (0 means default)"
    )
    parser_build.add_argument(
        "--enable-assert", action="store_true", help="build with assert and self-checking, only effective in release mode"
    )
    parser_build.add_argument(
        "--no-tests", action="store_true", help="build without unittests"
    )
    # cjnative BE supports stack grow feature and cjc enables stack grow feature by default. However, the feature is
    # supported at a cost of code-size and performance. In code-size and performance sensitive scenario, the feature
    # can be disabled by specifying the following build option.
    parser_build.add_argument(
        "--disable-stack-grow-feature", action="store_true", help="disable default stack grow feature for cjnative BE"
    )
    parser_build.add_argument(
        "--hwasan", action="store_true",
        help="build with hardware asan, only available when the target is ohos-aarch64 or ohos-x86_64"
    )
    parser_build.add_argument(
        "--gcc-toolchain", dest="gcc_toolchain", help="Specify toolchain for cjc, stdlib & BE build"
    )
    parser_build.add_argument(
        "--target", dest="target", type=str, choices=TARGET_DICTIONARY.keys(),
        help="build a second stdlib for the target triple specified"
    )
    parser_build.add_argument(
        "-L", "--target-lib", dest="target_lib", type=str, action='append', default=[],
        help="link libraries under this path for all products"
    )
    parser_build.add_argument(
        "--target-toolchain", dest="target_toolchain", type=str,
        help="use the tools under the given path to cross-compile stdlib"
    )
    parser_build.add_argument(
        "-I", "--include", dest="include", type=str, action='append', default=[],
        help="search header files in given paths"
    )
    parser_build.add_argument(
        "--target-sysroot", dest="target_sysroot", type=str,
        help="pass this argument to C/CXX compiler as --sysroot"
    )
    parser_build.add_argument(
        "--android-ndk", dest="android_ndk", type=str,
        help="Specify the path to the android NDK"
    )
    product_types=['all', 'cjc', 'libs']
    parser_build.add_argument(
        "--product", dest="product", choices=product_types,
        help="specify which part of Cangjie to build"
    )
    parser_build.add_argument(
        "--build-cjdb", action="store_true",
        help="build cjc with cjdb"
    )
    parser_build.add_argument(
        "--use-oh-llvm-repo", action="store_true",
        help="use OpenHarmony llvm repo with Cangjie llvm patch instead of cangjie llvm repo for building"
    )
    parser_build.add_argument(
        "--cjlib-sanitizer-support",
        type=str,
        choices=["asan", "tsan", "hwasan"],
        dest="sanitizer_support",
        help="Enable cangjie sanitizer support for cangjie libraries."
    )
    parser_build.add_argument(
        "--enable-sanitize-option", action="store_true",
        help="enable --sanitize option for cjc"
    )
    parser_build.set_defaults(func=build)

    parser_install = subparsers.add_parser("install", help="install targets")
    parser_install.add_argument(
        "--host", dest="host", type=str, choices=TARGET_DICTIONARY.keys(),
        help="Generate installation package for the host. When --prefix is specified, this option will not take effect"
    )
    parser_install.add_argument(
        "--prefix", dest="prefix", help="target install prefix"
    )
    parser_install.set_defaults(func=install)

    parser_test = subparsers.add_parser("test", help="run unittests", add_help=False)
    parser_test.set_defaults(func=test)

    args = parser.parse_args()
    args.func(args)

def check_compiler(args):
    # If user didn't specify --target-toolchain, we search for an available compiler in $PATH.
    # If user did specify --target-toolchain, we search in user given path ONLY. By doing so
    # user could see a 'compiler not found' error if the given path is incorrect.
    toolchain_path = args.target_toolchain if args.target_toolchain else None
    if toolchain_path and (not os.path.exists(toolchain_path)):
        LOG.error("the given toolchain path does not exist, {}".format(toolchain_path))
    # The gcc with the MinGW triple prefix is used for Windows native compiling.
    if IS_WINDOWS and args.target is None:
        c_compiler = shutil.which("x86_64-w64-mingw32-gcc.exe", path=toolchain_path)
        cxx_compiler = shutil.which("x86_64-w64-mingw32-g++.exe", path=toolchain_path)
    else: # On other platform, clang is always the first choice.
        c_compiler = shutil.which("clang", path=toolchain_path)
        cxx_compiler = shutil.which("clang++", path=toolchain_path)
    # If clang is not available and we are cross compiling, we check for gcc cross compiler.
    if (c_compiler == None or cxx_compiler == None) and args.target:
        c_compiler = shutil.which(args.target + "-gcc", path=toolchain_path)
        cxx_compiler = shutil.which(args.target + "-g++", path=toolchain_path)
    # If none of above is available, we search for generic gcc compiler.

    if c_compiler == None or cxx_compiler == None:
        c_compiler = shutil.which("gcc", path=toolchain_path)
        cxx_compiler = shutil.which("g++", path=toolchain_path)

    if c_compiler == None or cxx_compiler == None:
        if toolchain_path:
            LOG.error("cannot find available compiler in the given toolchain path")
        else:
            LOG.error("cannot find available compiler in $PATH")
        LOG.error("clang/clang++ or gcc/g++ is required to build cangjie compiler")

    # If cross-compiling cjc, a native compiler is also needed by cross-building LLVM,
    # because it needs some native tools like llvm-tblgen to generate codes.
    # In this case, CMake will use the value in $CC as the native compiler.
    if args.target and args.product == "cjc" and not c_compiler.endswith("clang"):
        os.environ["CC"] = shutil.which("clang") + (" --gcc-toolchain={}".format(args.gcc_toolchain) if args.gcc_toolchain else "")
        os.environ["CXX"] = shutil.which("clang++") + (" --gcc-toolchain={}".format(args.gcc_toolchain) if args.gcc_toolchain else "")
    else:
        os.environ["CC"] = c_compiler
        os.environ["CXX"] = cxx_compiler

if __name__ == "__main__":
    LOG = init_log("root")
    os.environ["LANG"] = "C.UTF-8"

    main()
