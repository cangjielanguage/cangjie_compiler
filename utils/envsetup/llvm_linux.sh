#!/bin/bash

# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# This script needs to be placed in the output directory of Cangjie compiler.
# ** NOTE: Please use `source' command to execute this script. **

# Get current shell name.
shell_path=$(readlink -f /proc/$$/exe)
shell_name=${shell_path##*/}

# Get the absolute path of this script according to different shells.
case "${shell_name}" in
    "zsh")
        # check whether compinit has been executed 
        if (( ${+_comps} )); then
            # if compinit already executed, delete completion functions of cjc, cjc-frontend firstly
            compdef -d cjc cjc-frontend
        else
            autoload -Uz compinit
            compinit
        fi

        # auto complete cjc, cjc-frontend
        compdef _gnu_generic cjc cjc-frontend
        script_dir=$(cd "$(dirname "$(readlink -f "${(%):-%N}")")"; pwd)
        ;;
    "bash")
        script_dir=$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"; pwd)
        ;;
    "sh")
        if [ -f "envsetup.sh" ]; then
            script_dir=$(pwd)
        else
            echo "[ERROR] Switch to the directory containing envsetup.sh and run it."
            return 1
        fi
        ;;
    *)
        echo "[ERROR] Unsupported shell: ${shell_name}, please switch to bash, sh or zsh."
        return 1
        ;;
esac

export CANGJIE_HOME=${script_dir}

sys_name=$(uname -s)
if [ "$sys_name" = "HarmonyOS" ]; then
    sys_name="_ohos"
    hw_arch=$(uname -m)
else
    sys_name=""
    hw_arch=$(arch)
fi
if [ "$hw_arch" = "" ]; then
    hw_arch="x86_64"
fi

export PATH=${CANGJIE_HOME}/bin:${CANGJIE_HOME}/tools/bin:$PATH:${HOME}/.cjpm/bin
export LD_LIBRARY_PATH=${CANGJIE_HOME}/runtime/lib/linux${sys_name}_${hw_arch}_cjnative:${CANGJIE_HOME}/tools/lib:${LD_LIBRARY_PATH}
unset hw_arch
unset sys_name
