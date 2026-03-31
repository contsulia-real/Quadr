cmake_minimum_required(VERSION 4.1)

add_executable(quadr main.c)
target_link_libraries(quadr PRIVATE quadr_core)

set(CMAKE_EXE_LINKER_FLAGS "")

if(NOT WIN32)
    target_link_libraries(quadr PRIVATE m)
endif()

# ── zlib ──────────────────────────────────────────────────────────────────
# On Windows, vcpkg provides: cmake .. -DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZLIB)
    target_link_libraries(quadr PRIVATE ZLIB::ZLIB)
    message(STATUS "quadr app: zlib enabled")
endif()

# ── zstd ──────────────────────────────────────────────────────────────────
find_package(zstd CONFIG QUIET)   # vcpkg style
if(zstd_FOUND)
    target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZSTD)
    target_link_libraries(quadr PRIVATE zstd::libzstd)
    message(STATUS "quadr app: zstd enabled (via CMake package)")
else()
    # fallback: raw find_library for non-vcpkg installs
    find_library(ZSTD_LIB NAMES zstd zstd_static libzstd_static)
    find_path(ZSTD_INC zstd.h)
    if(ZSTD_LIB AND ZSTD_INC)
        target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZSTD)
        target_include_directories(quadr PRIVATE ${ZSTD_INC})
        target_link_libraries(quadr PRIVATE ${ZSTD_LIB})
        message(STATUS "quadr app: zstd enabled (found ${ZSTD_LIB})")
    endif()
endif()

# ── lz4 ───────────────────────────────────────────────────────────────────
find_package(lz4 CONFIG QUIET)    # vcpkg style
if(lz4_FOUND)
    target_compile_definitions(quadr PRIVATE QUADR_HAVE_LZ4 QUADR_HAVE_LZ4HC)
    target_link_libraries(quadr PRIVATE lz4::lz4)
    message(STATUS "quadr app: lz4 + lz4hc enabled (via CMake package)")
else()
    find_library(LZ4_LIB NAMES lz4 lz4_static liblz4_static)
    find_path(LZ4_INC lz4.h)
    find_path(LZ4HC_INC lz4hc.h)
    if(LZ4_LIB AND LZ4_INC)
        target_compile_definitions(quadr PRIVATE QUADR_HAVE_LZ4)
        target_include_directories(quadr PRIVATE ${LZ4_INC})
        target_link_libraries(quadr PRIVATE ${LZ4_LIB})
        message(STATUS "quadr app: lz4 enabled (found ${LZ4_LIB})")
        if(LZ4HC_INC)
            target_compile_definitions(quadr PRIVATE QUADR_HAVE_LZ4HC)
            message(STATUS "quadr app: lz4hc enabled")
        endif()
    endif()
endif()

install(TARGETS quadr RUNTIME DESTINATION bin)
