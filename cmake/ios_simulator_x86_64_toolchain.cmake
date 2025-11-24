get_filename_component(CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include("${CMAKE_DIR}/darwin_toolchain.cmake")
 
set(CMAKE_SYSTEM_NAME "ios")
set(CMAKE_SYSTEM_PROCESSOR "x86_64")
set(TRIPLE x86_64-apple-ios11-simulator)
set(CXX_COMPATIABLE_TRIPLE x86_64-apple-ios12-simulator)
set(TARGET_TRIPLE_DIRECTORY_PREFIX ios_simulator_x86_64)
 
add_compile_options(--target=${TRIPLE})
add_link_options(--target=${TRIPLE})

set(IOS ON)
set(IOS_PLATFORM SIMULATOR)
set(IOS_PLATFORM_LOCATION "iPhoneSimulator.platform")
set(CMAKE_IOS_DEVELOPER_ROOT "/Applications/Xcode.app/Contents/Developer/Platforms/${IOS_PLATFORM_LOCATION}/Developer")
set(CMAKE_IOS_SDK_ROOT "${CMAKE_IOS_DEVELOPER_ROOT}/SDKs/iPhoneSimulator17.5.sdk")
set(CMAKE_OSX_SYSROOT "${CMAKE_IOS_DEVELOPER_ROOT}/SDKs/iPhoneSimulator17.5.sdk")