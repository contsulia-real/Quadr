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

# ── zlib-ng (vcpkg) ───────────────────────────────────────────────────────
find_package(zlib-ng CONFIG REQUIRED)
target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_ZLIBNG)
target_link_libraries(quadr_logic PRIVATE zlib-ng::zlib)

# ── zstd (vcpkg) ──────────────────────────────────────────────────────────
find_package(zstd CONFIG REQUIRED)
target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_ZSTD)
target_link_libraries(quadr_logic PRIVATE zstd::libzstd)

# ── lz4 (vcpkg) ───────────────────────────────────────────────────────────
find_package(lz4 CONFIG REQUIRED)
target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_LZ4 QUADR_HAVE_LZ4HC)
target_link_libraries(quadr_logic PRIVATE lz4::lz4)

# ── liblzma / 7z (vcpkg) ──────────────────────────────────────────────────
find_package(liblzma CONFIG REQUIRED)
target_compile_definitions(quadr_logic PRIVATE QUADR_HAVE_7Z)
target_link_libraries(quadr_logic PRIVATE liblzma::liblzma)
