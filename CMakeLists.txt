# 最低 CMake 版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称和版本
project(OS_design VERSION 1.0 LANGUAGES CXX)

# 设置 CXX 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 添加可执行文件
add_executable(${PROJECT_NAME}
    src/main.cpp
    # 添加其他源文件
)

# 包含头文件目录
target_include_directories(${PROJECT_NAME} PRIVATE
    include
)

