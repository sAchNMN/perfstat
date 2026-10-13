# ============================================================================
# 项目：PerfStat —— 系统性能统计小工具
# 架构：C++ 后端 + 内嵌 Vue 前端资源的单文件 exe
# 环境：MinGW-w64 g++ 16.2.0 / CMake 4.4.2 / Ninja 1.13.2
# ============================================================================
cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# 步骤1：项目定义与全局编译配置
# ---------------------------------------------------------------------------
project(perfstat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# 步骤2：第三方单头/单文件库
# 说明：零包管理，httplib.h / json.hpp / sqlite3.c,h 手动放入 src/third_party
# ---------------------------------------------------------------------------
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/third_party")

# ---------------------------------------------------------------------------
# 步骤3：后端可执行目标
# 说明：源文件按目录递归收集，新增 .cpp 无需改动本文件
# ---------------------------------------------------------------------------
file(GLOB_RECURSE BACKEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(perfstat ${BACKEND_SOURCES})

target_include_directories(perfstat PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${THIRD_PARTY_DIR}"
)

# ---------------------------------------------------------------------------
# 步骤4：Windows 系统库依赖
#   psapi    -> GetProcessMemoryInfo
#   ws2_32   -> cpp-httplib 套接字
#   advapi32 -> 服务 (SCM) 与注册表
# ---------------------------------------------------------------------------
if(WIN32)
    target_link_libraries(perfstat PRIVATE psapi ws2_32 advapi32)
endif()

# ---------------------------------------------------------------------------
# 步骤5：内嵌前端资源
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFrontend.cmake")
