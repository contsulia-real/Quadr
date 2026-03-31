cmake_minimum_required(VERSION 4.1)

add_library(quadr_logic STATIC
        src/logic_common.c
        src/logic_encode.c
        src/logic_decode.c
        src/logic_info.c
        src/logic_bench.c
        src/logic_verify.c
        src/logic_probe.c
        src/logic_pack.c
        src/logic_unpack.c
        src/logic_list.c
)

target_include_directories(quadr_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quadr_logic PUBLIC quadr_core)

if(NOT WIN32)
    target_link_libraries(quadr_logic PRIVATE m)
endif()

# ── zlib ──────────────────────────────────────────────────────────────────
find_package(zlib-ng CONFIG QUIET)
if(zlib-ng_FOUND)
    target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_ZLIBNG)
    target_link_libraries(quadr_logic PRIVATE zlib-ng::zlib)
else()
    find_library(ZLIBNG_LIB NAMES zlib-ng zlibng zlibng_static PATHS ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib)
    find_path(ZLIBNG_INC zlib-ng.h PATHS ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include)
    if(ZLIBNG_LIB AND ZLIBNG_INC)
        target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_ZLIBNG)
        target_include_directories(quadr_logic PRIVATE ${ZLIBNG_INC})
        target_link_libraries(quadr_logic PRIVATE ${ZLIBNG_LIB})
    else()
        find_package(ZLIB QUIET)
        if(ZLIB_FOUND)
            if(TARGET ZLIB::ZLIB)
                target_link_libraries(quadr_logic PRIVATE ZLIB::ZLIB)
            else()
                target_include_directories(quadr_logic PRIVATE ${ZLIB_INCLUDE_DIRS})
                target_link_libraries(quadr_logic PRIVATE ${ZLIB_LIBRARIES})
            endif()
        else()
            find_library(ZLIB_LIB NAMES zlib zlibstatic libz libz_static)
            find_path(ZLIB_INC zlib.h)
            if(ZLIB_LIB AND ZLIB_INC)
                target_include_directories(quadr_logic PRIVATE ${ZLIB_INC})
                target_link_libraries(quadr_logic PRIVATE ${ZLIB_LIB})
            endif()
        endif()
    endif()
endif()

if(WIN32 AND MINGW)
    target_link_libraries(quadr_logic PRIVATE z)
endif()

# ── zstd ──────────────────────────────────────────────────────────────────
find_package(zstd CONFIG QUIET)
if(zstd_FOUND)
    target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_ZSTD)
    if(TARGET zstd::libzstd)
        target_link_libraries(quadr_logic PRIVATE zstd::libzstd)
    elseif(TARGET ZSTD::ZSTD)
        target_link_libraries(quadr_logic PRIVATE ZSTD::ZSTD)
    elseif(TARGET zstd::zstd)
        target_link_libraries(quadr_logic PRIVATE zstd::zstd)
    elseif(DEFINED zstd_LIBRARIES)
        target_link_libraries(quadr_logic PRIVATE ${zstd_LIBRARIES})
    endif()
else()
    find_library(ZSTD_LIB NAMES zstd zstd_static libzstd_static)
    find_path(ZSTD_INC zstd.h)
    if(ZSTD_LIB AND ZSTD_INC)
        target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_ZSTD)
        target_include_directories(quadr_logic PRIVATE ${ZSTD_INC})
        target_link_libraries(quadr_logic PRIVATE ${ZSTD_LIB})
    endif()
endif()

# ── lz4 ───────────────────────────────────────────────────────────────────
find_package(lz4 CONFIG QUIET)
if(lz4_FOUND)
    target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_LZ4 QUADR_HAVE_LZ4HC)
    target_link_libraries(quadr_logic PRIVATE lz4::lz4)
else()
    find_library(LZ4_LIB NAMES lz4 lz4_static liblz4_static)
    find_path(LZ4_INC lz4.h)
    find_path(LZ4HC_INC lz4hc.h)
    if(LZ4_LIB AND LZ4_INC)
        target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_LZ4)
        target_include_directories(quadr_logic PRIVATE ${LZ4_INC})
        target_link_libraries(quadr_logic PRIVATE ${LZ4_LIB})
        if(LZ4HC_INC)
            target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_LZ4HC)
        endif()
    endif()
endif()

# ── 7z / liblzma ──────────────────────────────────────────────────────────
find_package(XZ QUIET)
if(XZ_FOUND)
    target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_7Z)
    target_link_libraries(quadr_logic PRIVATE XZ::XZ)
else()
    find_library(LZMA_LIB NAMES lzma liblzma_static)
    find_path(LZMA_INC lzma.h)
    if(LZMA_LIB AND LZMA_INC)
        target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_7Z)
        target_include_directories(quadr_logic PRIVATE ${LZMA_INC})
        target_link_libraries(quadr_logic PRIVATE ${LZMA_LIB})
    endif()
endif()
