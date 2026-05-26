# 最低 CMake 版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称和版本
project(OS_design VERSION 1.0 LANGUAGES C)

# 设置 C 标准
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 添加可执行文件
add_executable(OS_design
    src/main.c
    # 文件系统层
    src/fs/disk_io.c
    src/fs/format.c
    src/fs/allocator.c
    src/fs/dir_sys.c
    src/fs/file_sys.c
    # 内核层
    src/kernel/memory.c
    src/kernel/cpu.c
    src/kernel/process.c
    src/kernel/scheduler.c
    src/kernel/syscall.c
    # 用户管理
    src/user/user_mgmt.c
    src/user/env.c
    # 预置程序
    src/binaries.c
)

# 包含头文件目录
target_include_directories(OS_design PRIVATE
    include
    src
)

# 链接 pthread
find_package(Threads REQUIRED)
target_link_libraries(OS_design PRIVATE Threads::Threads)
