# ============================================================================
# 项目：PerfStat —— 系统性能统计小工具
# 架构：C++ 后端 + 内嵌 Vue 前端资源的单文件 exe
# 环境：MinGW-w64 g++ 16.2.0 / CMake 4.4.2 / Ninja 1.13.2
# ============================================================================
cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# 步骤1：项目定义与全局编译配置
# ---------------------------------------------------------------------------
project(perfstat VERSION 0.1.0 LANGUAGES C CXX)

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

# ---------------------------------------------------------------------------
# 步骤3.1：第三方 C 源（SQLite amalgamation）
# 说明：sqlite3.c 是 C 源，故 project() 已启用 C 语言；不并入上面的 GLOB，
#       避免把 third_party 下其他 .cpp（如未来加的单测）误当成主程序源。
# ---------------------------------------------------------------------------
set(SQLITE3_SOURCE "${THIRD_PARTY_DIR}/sqlite3.c")

add_executable(perfstat ${BACKEND_SOURCES} ${SQLITE3_SOURCE})

target_include_directories(perfstat PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${THIRD_PARTY_DIR}"
)

# sqlite3.c 为第三方代码，单独关掉警告，避免其告警污染本工程构建输出
set_source_files_properties("${SQLITE3_SOURCE}" PROPERTIES
    COMPILE_OPTIONS "-w"
)

# SQLite 编译期裁剪：只保留本项目用得到的特性，减小体积
target_compile_definitions(perfstat PRIVATE
    SQLITE_THREADSAFE=1          # 串行化线程模式，采集线程与写库线程并发访问
    SQLITE_OMIT_LOAD_EXTENSION   # 不用扩展加载
    SQLITE_OMIT_DEPRECATED       # 去掉弃用接口
    SQLITE_DQS=0                 # 禁止双引号当标识符，强制规范 SQL
    SQLITE_DEFAULT_MEMSTATUS=0   # 关内存统计，省一次锁
)

# ---------------------------------------------------------------------------
# 步骤4：Windows 系统库依赖
#   psapi    -> GetProcessMemoryInfo
#   ws2_32   -> cpp-httplib 套接字
#   advapi32 -> 服务 (SCM) 与注册表
# ---------------------------------------------------------------------------
if(WIN32)
    target_link_libraries(perfstat PRIVATE psapi ws2_32 advapi32)

    # 目标平台 Windows 10+（DESIGN 定为 Windows 11）：
    # cpp-httplib 依赖 CreateFile2 等 Win8+ API，并显式拒绝低版本 SDK 目标，
    # 故必须把 _WIN32_WINNT/WINVER 抬到 0x0A00，否则 httplib 直接 #error
    target_compile_definitions(perfstat PRIVATE
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
endif()

# ---------------------------------------------------------------------------
# 步骤5：内嵌前端资源
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFrontend.cmake")
