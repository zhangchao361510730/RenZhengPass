# ====================================================================
# 文件名: CMakeLists.txt (多文件项目版)
# ====================================================================

# 设置CMake最低版本要求
cmake_minimum_required(VERSION 3.10)

# 定义项目名称
project(TextCaptureServer CXX)

# 设置C++标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 为 "Debug" 构建类型添加 -g 编译标志以支持GDB
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

# --- 核心改动 ---
# 1. 将 "include" 目录添加到头文件搜索路径
include_directories(include)

# 2. 定义源文件列表
set(SOURCES
    src/main.cpp
    src/TextCaptureServer.cpp
)

# 3. 从源文件列表创建可执行文件
add_executable(text_capture_server ${SOURCES})
# --- 结束改动 ---

# 查找并链接所需的库
find_package(Threads REQUIRED)
find_package(X11 REQUIRED)

target_link_libraries(text_capture_server PRIVATE
    Threads::Threads
    X11::X11
    X11::Xtst
)

# 打印状态信息
message(STATUS "项目配置完成。使用 'make' 命令来构建。")
message(STATUS "要启用GDB调试, 请使用: cmake -DCMAKE_BUILD_TYPE=Debug ..")