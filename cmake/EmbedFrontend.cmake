# ============================================================================
# 模块：EmbedFrontend.cmake
# 作用：把 frontend/dist 的前端产物转成 C 数组头文件，供后端内嵌
# 时序：frontend 先 vite build 产出 dist/，本模块再生成 frontend_assets.h
# 状态：占位实现，M8 打包阶段补齐
# ============================================================================

set(FRONTEND_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frontend/dist")
set(EMBED_OUTPUT_DIR  "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(EMBED_HEADER      "${EMBED_OUTPUT_DIR}/frontend_assets.h")

# ---------------------------------------------------------------------------
# 步骤1：探测前端产物
# ---------------------------------------------------------------------------
if(EXISTS "${FRONTEND_DIST_DIR}/index.html")
    message(STATUS "[EmbedFrontend] frontend dist found: ${FRONTEND_DIST_DIR}")

    # -----------------------------------------------------------------------
    # 步骤2：生成 C 数组头文件
    # -----------------------------------------------------------------------
    # TODO(M8)：
    #   1) 遍历 dist/ 下全部文件
    #   2) 每份文件生成 { 路径, 字节数组, 长度 } 结构体
    #   3) 输出到 ${EMBED_HEADER} 并加入 perfstat 的 include 目录
    add_custom_command(
        OUTPUT  "${EMBED_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EMBED_OUTPUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${EMBED_HEADER}"
        COMMENT "[EmbedFrontend] generate placeholder header (real embedding at M8)"
        VERBATIM
    )
    add_custom_target(embed_frontend DEPENDS "${EMBED_HEADER}")
    add_dependencies(perfstat embed_frontend)
    target_include_directories(perfstat PRIVATE "${EMBED_OUTPUT_DIR}")
else()
    message(STATUS "[EmbedFrontend] no frontend dist found, skip embedding (dev mode serves via localhost)")
endif()
