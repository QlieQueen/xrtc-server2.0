cmake_minimum_required(VERSION 3.5)

project(xrtcserver)

# 导出 compile_commands.json，供 VSCode C/C++ 扩展 IntelliSense 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions("-g -pipe -W -Wall -fPIC -std=gnu++14 -DWEBRTC_POSIX
    -DWEBRTC_LINUX")

include_directories(
    "./src"
    "./third_party/include"
    "../rtcbase/src"
)

link_directories(
    "./third_party/lib"
    "../rtcbase/out"
)

file(GLOB all_src
    "./src/*.cpp"
    "./src/base/*.cpp"
    "./src/server/*.cpp"
    "./src/stream/*.cpp"
    "./src/pc/*.cpp"
    "./src/ice/*.cpp"
    "./src/modules/rtp_rtcp/*.cpp"
)

add_executable(xrtcserver ${all_src})

# third_party 预编译库非 -fPIC，链接时关闭 PIE
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")

target_link_libraries(xrtcserver libyaml-cpp.a librtcbase.a
    libabsl_strings.a libabsl_throw_delegate.a libabsl_bad_optional_access.a
    libev.a libjsoncpp.a libssl.a libcrypto.a libsrtp2.a
    -lpthread -ldl
)



