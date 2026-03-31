cmake_minimum_required(VERSION 3.31.6)

add_executable(quadr main.c
        include/quadr_zlib_compat.h)
target_link_libraries(quadr PRIVATE quadr_core)

target_include_directories(quadr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CMAKE_EXE_LINKER_FLAGS "")

if(NOT WIN32)
    target_link_libraries(quadr PRIVATE m)
endif()

# ── zlib ──────────────────────────────────────────────────────────────────
# zlib-ng only (do not fallback to system zlib)
# Try common vcpkg/cmake package first, then fallback to raw find_library/find_path
find_package(zlib-ng CONFIG QUIET)
if(zlib-ng_FOUND)
    target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZLIBNG)
    target_link_libraries(quadr PRIVATE zlib-ng::zlib)
    message(STATUS "quadr app: zlib-ng enabled (via CMake package)")
else()
    find_library(ZLIBNG_LIB NAMES zlib-ng zlibng zlibng_static)
    find_path(ZLIBNG_INC zlib.h)
    if(ZLIBNG_LIB AND ZLIBNG_INC)
        target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZLIBNG)
        target_include_directories(quadr PRIVATE ${ZLIBNG_INC})
        target_link_libraries(quadr PRIVATE ${ZLIBNG_LIB})
        message(STATUS "quadr app: zlib-ng enabled (found ${ZLIBNG_LIB})")
    endif()
endif()

# ── zstd ──────────────────────────────────────────────────────────────────
find_package(zstd CONFIG QUIET)   # vcpkg style
if(zstd_FOUND)
    target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZSTD)
    # Different zstd packages may export different imported target names.
    if(TARGET zstd::libzstd)
        target_link_libraries(quadr PRIVATE zstd::libzstd)
    elseif(TARGET ZSTD::ZSTD)
        target_link_libraries(quadr PRIVATE ZSTD::ZSTD)
    elseif(TARGET zstd::zstd)
        target_link_libraries(quadr PRIVATE zstd::zstd)
    elseif(DEFINED zstd_LIBRARIES)
        # fallback to variables set by find_package implementations
        target_link_libraries(quadr PRIVATE ${zstd_LIBRARIES})
    else()
        message(WARNING "zstd package found but no known imported target; skipping link")
    endif()
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

# ── 7z / liblzma ───────────────────────────────────────────────────────────
# Prefer a CMake package; fallback to find_library/find_path
find_package(XZ QUIET)
if(XZ_FOUND)
    target_compile_definitions(quadr PRIVATE QUADR_HAVE_7Z)
    target_link_libraries(quadr PRIVATE XZ::XZ)
    message(STATUS "quadr app: liblzma/7z enabled (via CMake package)")
else()
    find_library(LZMA_LIB NAMES lzma liblzma_static)
    find_path(LZMA_INC lzma.h)
    if(LZMA_LIB AND LZMA_INC)
        target_compile_definitions(quadr PRIVATE QUADR_HAVE_7Z)
        target_include_directories(quadr PRIVATE ${LZMA_INC})
        target_link_libraries(quadr PRIVATE ${LZMA_LIB})
        message(STATUS "quadr app: liblzma/7z enabled (found ${LZMA_LIB})")
    endif()
endif()

