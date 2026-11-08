cmake_minimum_required(VERSION 3.16)

project(RoboKit LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    # 默认 Debug（带调试符号，支持断点）；发布时用 -DCMAKE_BUILD_TYPE=Release
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(ELNET_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../elNET")
set(IPGUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../ESDBox_IPGUI")

# ============================================================
# elNET（C 网络协议栈）-> C 静态库
# 源码/头文件/依赖库集合与 m:\ESDBox\Makefile 保持一致
# ============================================================
file(GLOB ELNET_C_SOURCES
    "${ELNET_ROOT}/src/*.c"
    "${ELNET_ROOT}/port/*.c"
    "${ELNET_ROOT}/components/cjson/*.c"
    "${ELNET_ROOT}/src/app_protocal/tftp/*.c"
)

add_library(elnet_c STATIC ${ELNET_C_SOURCES})

target_include_directories(elnet_c PUBLIC
    "${ELNET_ROOT}/inc"
    "${ELNET_ROOT}/port"
    "${ELNET_ROOT}/src"
    "${ELNET_ROOT}/port/Include"
    "${ELNET_ROOT}/port/Include/pcap"
    "${ELNET_ROOT}/components/cjson"
    "${ELNET_ROOT}/src/app_protocal/tftp"
    "${ELNET_ROOT}"
)

# ============================================================
# IPGUI（C 图形框架）-> C 静态库
# 关键点：.c 必须用 C 编译器编译（C99/C11 语法）；
#        头文件已补 extern "C" 保护，可被 C++ 直接调用
# ============================================================
file(GLOB IPGUI_C_SOURCES
    "${IPGUI_ROOT}/base/src/*.c"
    "${IPGUI_ROOT}/al/hal/*.c"
    # ipgui_input_dispatcher.c 排除：头文件声明 ipgui_err_t 而实现是 void，
    # IPGUI 自身声明冲突，接入输入分发前需先修正
    # "${IPGUI_ROOT}/al/hal/input/*.c"
    "${IPGUI_ROOT}/al/vfs/*.c"
    "${IPGUI_ROOT}/core/*.c"
    "${IPGUI_ROOT}/core/composite/*.c"
    "${IPGUI_ROOT}/core/composite/blend_color/*.c"
    "${IPGUI_ROOT}/core/composite/blend_gradient/*.c"
    "${IPGUI_ROOT}/core/composite/blend_image/*.c"
    "${IPGUI_ROOT}/core/gfx/*.c"
    "${IPGUI_ROOT}/core/image/*.c"
    "${IPGUI_ROOT}/core/image/decoder/*.c"
    "${IPGUI_ROOT}/core/image/decoder/bmp/*.c"
    "${IPGUI_ROOT}/core/image/decoder/gif/*.c"
    # ipgui_jpeg.c 排除：独立文件，依赖外部环境，需单独适配
    # "${IPGUI_ROOT}/core/image/decoder/jpeg/*.c"
    "${IPGUI_ROOT}/core/image/decoder/png/*.c"
    "${IPGUI_ROOT}/core/image/decoder/qoi/*.c"
    "${IPGUI_ROOT}/core/image/proc/*.c"
    "${IPGUI_ROOT}/core/ipml/*.c"
    "${IPGUI_ROOT}/core/misc/*.c"
    "${IPGUI_ROOT}/core/ui/animation/*.c"
    "${IPGUI_ROOT}/core/ui/animation/builtin_anim/*.c"
    "${IPGUI_ROOT}/core/ui/layout/*.c"
    "${IPGUI_ROOT}/core/ui/widget_manager/*.c"
    "${IPGUI_ROOT}/core/vector_render/*.c"
    "${IPGUI_ROOT}/port/ipgui_usr_putchar.c"
    "${IPGUI_ROOT}/port/sdl/sdl_draw.c"
)

add_library(ipgui_c STATIC ${IPGUI_C_SOURCES})

target_include_directories(ipgui_c PUBLIC
    "${ELNET_ROOT}/inc"
    "${IPGUI_ROOT}/Include"
    "${IPGUI_ROOT}/base/inc"
    "${IPGUI_ROOT}/base/src"
    "${IPGUI_ROOT}/al/hal"
    "${IPGUI_ROOT}/al/hal/input"
    "${IPGUI_ROOT}/al/vfs"
    "${IPGUI_ROOT}/core"
    "${IPGUI_ROOT}/core/composite"
    "${IPGUI_ROOT}/core/composite/blend_color"
    "${IPGUI_ROOT}/core/composite/blend_gradient"
    "${IPGUI_ROOT}/core/composite/blend_image"
    "${IPGUI_ROOT}/core/gfx"
    "${IPGUI_ROOT}/core/image"
    "${IPGUI_ROOT}/core/image/decoder"
    "${IPGUI_ROOT}/core/image/decoder/bmp"
    "${IPGUI_ROOT}/core/image/decoder/gif"
    "${IPGUI_ROOT}/core/image/decoder/jpeg"
    "${IPGUI_ROOT}/core/image/decoder/png"
    "${IPGUI_ROOT}/core/image/decoder/qoi"
    "${IPGUI_ROOT}/core/image/proc"
    "${IPGUI_ROOT}/core/ipml"
    "${IPGUI_ROOT}/core/misc"
    "${IPGUI_ROOT}/core/ui/animation"
    "${IPGUI_ROOT}/core/ui/animation/builtin_anim"
    "${IPGUI_ROOT}/core/ui/layout"
    "${IPGUI_ROOT}/core/ui/widget_manager"
    "${IPGUI_ROOT}/core/vector_render"
    "${IPGUI_ROOT}/port/sdl"
    "${IPGUI_ROOT}/port/sdl/include/SDL2"
)

# ============================================================
# RPLIDAR SDK（思岚 A1 激光雷达）-> C++ 静态库
# SDK 原样编译，不做任何修改；仅选取 Windows 平台所需源文件
# （参考 workspaces/vc14/rplidar_driver.vcxproj 的源文件清单）
# ============================================================
set(RPLIDAR_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/rplidar_sdk-master/sdk")

add_library(rplidar_sdk STATIC
    "${RPLIDAR_SDK_ROOT}/src/arch/win32/net_serial.cpp"
    "${RPLIDAR_SDK_ROOT}/src/arch/win32/net_socket.cpp"
    "${RPLIDAR_SDK_ROOT}/src/arch/win32/timer.cpp"
    "${RPLIDAR_SDK_ROOT}/src/dataunpacker/dataunpacker.cpp"
    "${RPLIDAR_SDK_ROOT}/src/dataunpacker/unpacker/handler_capsules.cpp"
    "${RPLIDAR_SDK_ROOT}/src/dataunpacker/unpacker/handler_hqnode.cpp"
    "${RPLIDAR_SDK_ROOT}/src/dataunpacker/unpacker/handler_normalnode.cpp"
    "${RPLIDAR_SDK_ROOT}/src/hal/thread.cpp"
    "${RPLIDAR_SDK_ROOT}/src/rplidar_driver.cpp"
    "${RPLIDAR_SDK_ROOT}/src/sl_async_transceiver.cpp"
    "${RPLIDAR_SDK_ROOT}/src/sl_crc.cpp"
    "${RPLIDAR_SDK_ROOT}/src/sl_lidarprotocol_codec.cpp"
    "${RPLIDAR_SDK_ROOT}/src/sl_lidar_driver.cpp"
    "${RPLIDAR_SDK_ROOT}/src/sl_serial_channel.cpp"
    "${RPLIDAR_SDK_ROOT}/src/sl_tcp_channel.cpp"
    "${RPLIDAR_SDK_ROOT}/src/sl_udp_channel.cpp"
)

target_include_directories(rplidar_sdk PUBLIC
    "${RPLIDAR_SDK_ROOT}/include"
    "${RPLIDAR_SDK_ROOT}/src"
    "${RPLIDAR_SDK_ROOT}/src/dataunpacker"
    "${RPLIDAR_SDK_ROOT}/src/dataunpacker/unpacker"
)

# MSVC 默认定义 WIN32/WIN64，MinGW 只定义带下划线的 _WIN32/_WIN64。
# SDK 的 sl_types.h / hal/types.h 等用无下划线的 WIN32/WIN64 判断字长，
# 因此这里补齐定义以匹配 MSVC 行为。
#
# 另：SDK 的 arch_win32.h 把 _WIN32_WINNT 设为 0x0501(Windows XP)，但用的是
# #ifndef 守卫，这里在编译器层提前定义即可覆盖。提高到 0x0600(Vista) 是因为
# MinGW 的 ws2tcpip.h 中 AI_NUMERICSERV（net_socket.cpp 用到）被
# #if (_WIN32_WINNT >= 0x0600) 包裹，0x0501 下不可用。
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_compile_definitions(rplidar_sdk PUBLIC WIN64=1 WIN32=1 _WIN32_WINNT=0x0600)
else()
    target_compile_definitions(rplidar_sdk PUBLIC WIN32=1 _WIN32_WINNT=0x0600)
endif()

# net_socket.cpp 使用 Winsock2（WSAStartup/socket 等），需链接 ws2_32
target_link_libraries(rplidar_sdk PUBLIC ws2_32)

# SDK 原生面向 MSVC：MSVC 的 nullptr_t 在全局命名空间，MinGW 仅在 std:: 下。
# sl_async_transceiver.cpp 直接用 nullptr_t 不加 std:: 前缀会编译失败。
# 用 -include 在每个 SDK 源文件前强制包含兼容头（内含与 sl_lidar_driver.cpp
# 相同的 _GXX_NULLPTR_T 守卫 typedef，不会重复定义），不改动 SDK 源文件。
target_compile_options(rplidar_sdk PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/src/rplidar_compat/compat.hpp")

# ============================================================
# RoboKit（C++ 主程序）
# 当前演示：RPLIDAR A1 激光雷达扫描数据采集
# ============================================================
add_executable(app main.cpp)

# main.cpp 包含 src/ 下的头文件（如 planner_rrt.hpp）
target_include_directories(app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(app PRIVATE ipgui_c elnet_c rplidar_sdk)

# 使用工程附带的 SDL2（导入库方式，运行时需要 SDL2.dll）
target_link_libraries(app PRIVATE "${IPGUI_ROOT}/port/sdl/lib/libSDL2.dll.a")

# 拷贝 SDL2.dll 到可执行文件目录
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${IPGUI_ROOT}/port/sdl/bin/SDL2.dll"
        "$<TARGET_FILE_DIR:app>"
    COMMENT "Copy SDL2.dll"
)

# elNET 依赖 Windows pcap（与 m:\ESDBox\Makefile 的 link_library 一致）
target_link_libraries(app PRIVATE "${ELNET_ROOT}/port/Lib/x64/wpcap.lib")
target_link_libraries(app PRIVATE "${ELNET_ROOT}/port/Lib/x64/Packet.lib")
target_link_libraries(app PRIVATE m)
