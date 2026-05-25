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
    src/disk_io.c
    src/format.c
    src/allocator.c
    src/dir_sys.c
    src/file_sys.c
)

# 包含头文件目录
target_include_directories(OS_design PRIVATE
    include
    src
)

# 链接 pthread（如需要）
find_package(Threads REQUIRED)
target_link_libraries(OS_design PRIVATE Threads::Threads)
