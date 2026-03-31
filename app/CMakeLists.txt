cmake_minimum_required(VERSION 4.1)

add_executable(quadr main.c
        include/quadr_zlib_compat.h)
target_link_libraries(quadr PRIVATE quadr_core)

target_include_directories(quadr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CMAKE_EXE_LINKER_FLAGS "")

if(NOT WIN32)
    target_link_libraries(quadr PRIVATE m)
endif()

# ── zlib ──────────────────────────────────────────────────────────────────
# Prefer zlib-ng (vcpkg / CMake package). If not available, also accept
# a normal zlib installation (system or vcpkg 'zlib') so symbols like
# compress2/uncompress are linked on platforms that provide zlib instead
# of zlib-ng (common on Windows/vcpkg setups).
find_package(zlib-ng CONFIG QUIET)
if(zlib-ng_FOUND)
    target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZLIBNG)
    target_link_libraries(quadr PRIVATE zlib-ng::zlib)
    message(STATUS "quadr app: zlib-ng enabled (via CMake package)")
else()
    # Try to find a zlib-ng archive by name first (legacy behavior)
    find_library(ZLIBNG_LIB NAMES zlib-ng zlibng zlibng_static)
    find_path(ZLIBNG_INC zlib.h)
    if(ZLIBNG_LIB AND ZLIBNG_INC)
        target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZLIBNG)
        target_include_directories(quadr PRIVATE ${ZLIBNG_INC})
        target_link_libraries(quadr PRIVATE ${ZLIBNG_LIB})
        message(STATUS "quadr app: zlib-ng enabled (found ${ZLIBNG_LIB})")
    else()
        # Fallback: try the system/zlib package (CMake's FindZLIB or vcpkg 'zlib')
        find_package(ZLIB QUIET)
        if(ZLIB_FOUND)
            # Use the standard zlib (do NOT set QUADR_HAVE_ZLIBNG so
            # the code will use the system-zlib compatibility path).
            if(TARGET ZLIB::ZLIB)
                target_link_libraries(quadr PRIVATE ZLIB::ZLIB)
            else()
                target_include_directories(quadr PRIVATE ${ZLIB_INCLUDE_DIRS})
                target_link_libraries(quadr PRIVATE ${ZLIB_LIBRARIES})
            endif()
            message(STATUS "quadr app: zlib enabled (via CMake FindZLIB)")
        else()
            # Last resort: raw find_library/find_path for common zlib names
            find_library(ZLIB_LIB NAMES zlib zlibstatic libz libz_static)
            find_path(ZLIB_INC zlib.h)
            if(ZLIB_LIB AND ZLIB_INC)
                target_include_directories(quadr PRIVATE ${ZLIB_INC})
                target_link_libraries(quadr PRIVATE ${ZLIB_LIB})
                message(STATUS "quadr app: zlib enabled (found ${ZLIB_LIB})")
            endif()
        endif()
    endif()
endif()

# MinGW-specific last-resort fallback: try common zlib import names
# This helps when using bundled MinGW toolchains or when CMake/vcpkg
# didn't expose a proper imported target but the static lib is present.
if(NOT ZLIB_FOUND AND WIN32 AND MINGW)
    find_library(ZLIB_MINGW_LIB NAMES z zlib1 zlib libz libz.a)
    if(ZLIB_MINGW_LIB)
        target_link_libraries(quadr PRIVATE ${ZLIB_MINGW_LIB})
        message(STATUS "quadr app: zlib enabled (mingw fallback ${ZLIB_MINGW_LIB})")
    endif()
endif()

# Some MinGW setups accept the simple -lz flag; ensure it's present as a last
# resort so compress2/uncompress link correctly when zlib wasn't found above.
if(WIN32 AND MINGW)
    target_link_libraries(quadr PRIVATE z)
    message(STATUS "quadr app: added -lz (MinGW fallback)")
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

