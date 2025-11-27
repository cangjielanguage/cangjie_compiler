# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

set(CMAKE_SYSTEM_NAME Linux)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VERBOSE_MAKEFILEON ON)
set(EXTRA_WARNING_SETTINGS "-Wsign-compare")
set(WARNINGS_SETTINGS "-Wall ${EXTRA_WARNING_SETTINGS} -Werror -Wdate-time ${CUSTOM_WARNING_SETTINGS}")
set(C_OTHER_FLAGS "-fsigned-char")
set(CXX_OTHER_FLAGS "-Weffc++")

set(OTHER_FLAGS "-fno-omit-frame-pointer -pipe -fno-common -fno-strict-aliasing -fstack-protector-all")
set(GCOV_FLAGS "-fno-inline -O0 -fprofile-arcs -ftest-coverage -fno-exceptions")
set(ASAN_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
set(TSAN_FLAGS "-fsanitize=thread")
set(HWASAN_FLAGS "-shared-libsan -fsanitize=hwaddress -fno-omit-frame-pointer -fno-emulated-tls -fno-lto -fno-whole-program-vtables")

set(LINK_FLAGS "-Wl,-z,relro,-z,now,-z,noexecstack")
set(STRIP_FLAG "-s")
set(SAFE_EXE_LINK_FLAG "-pie")

set(LINK_FLAGS_BUILD_ID "-Wl,--build-id=none")

set(C_FLAGS "${WARNINGS_SETTINGS} ${C_OTHER_FLAGS} ${OTHER_FLAGS}")
set(CPP_FLAGS "${WARNINGS_SETTINGS} ${CXX_OTHER_FLAGS} ${OTHER_FLAGS}")

option(CANGJIE_ENABLE_GCOV "Enable gcov (debug, Linux builds only)" OFF)
if(CANGJIE_ENABLE_GCOV)
    set(C_FLAGS "${C_FLAGS} ${GCOV_FLAGS}")
    set(CPP_FLAGS "${CPP_FLAGS} ${GCOV_FLAGS}")
    add_compile_definitions(CANGJIE_ENABLE_GCOV)
endif()
if(CANGJIE_ENABLE_ASAN_COV)
    set(C_FLAGS "${C_FLAGS} ${ASAN_FLAGS} -fsanitize-coverage=trace-pc-guard,trace-cmp,trace-div,trace-gep")
    set(CPP_FLAGS "${CPP_FLAGS} ${ASAN_FLAGS} -fsanitize-coverage=trace-pc-guard,trace-cmp,trace-div,trace-gep")
endif()

option(CANGJIE_ENABLE_ASAN "Enable asan (relwithbebinfo or debug, Linux builds only)" OFF)
if(CANGJIE_ENABLE_ASAN)
    set(C_FLAGS "${C_FLAGS} ${ASAN_FLAGS}")
    set(CPP_FLAGS "${CPP_FLAGS} ${ASAN_FLAGS}")
endif()

option(CANGJIE_ENABLE_HWASAN "Enable hardware asan (relwithdebinfo or debug, OHOS builds only)" OFF)
if(CANGJIE_ENABLE_HWASAN)
    set(C_FLAGS "${C_FLAGS} ${HWASAN_FLAGS}")
    set(CPP_FLAGS "${CPP_FLAGS} ${HWASAN_FLAGS}")
endif()

set(CMAKE_C_FLAGS "${C_FLAGS}")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")
set(CMAKE_C_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O2")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os")
set(CMAKE_C_FLAGS_MINSIZERELWITHDEBINFO "-Os -g")
set(CMAKE_CXX_FLAGS "${CPP_FLAGS}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O2")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -fstandalone-debug")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os")
set(CMAKE_CXX_FLAGS_MINSIZERELWITHDEBINFO "-Os -g")
if(CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_BUILD_TYPE MATCHES MinSizeRel)
    set(CMAKE_EXE_LINKER_FLAGS "${LINK_FLAGS} ${LINK_FLAGS_BUILD_ID} ${STRIP_FLAG}")
    set(CMAKE_SHARED_LINKER_FLAGS "${LINK_FLAGS} ${LINK_FLAGS_BUILD_ID} ${STRIP_FLAG}")
else()
    set(CMAKE_EXE_LINKER_FLAGS "${LINK_FLAGS} ${LINK_FLAGS_BUILD_ID}")
endif()

set(LINKER_OPTION_PREFIX "-Wl,")
set(MAKE_SO_STACK_PROTECTOR_OPTION)
set(LLVM_BUILD_C_COMPILER ${CMAKE_C_COMPILER})
set(LLVM_BUILD_CXX_COMPILER ${CMAKE_CXX_COMPILER})

if(CANGJIE_TARGET_SYSROOT)
    set(CMAKE_SYSROOT ${CANGJIE_TARGET_SYSROOT})
endif()
