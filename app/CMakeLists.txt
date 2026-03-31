cmake_minimum_required(VERSION 4.1)

add_executable(quadr
        main.c

        cmd/cmd_bench.c
        cmd/cmd_common.c
        cmd/cmd_decode.c
        cmd/cmd_encode.c
        cmd/cmd_help.c
        cmd/cmd_info.c
        cmd/cmd_list.c
        cmd/cmd_pack.c
        cmd/cmd_probe.c
        cmd/cmd_unpack.c
        cmd/cmd_verify.c

        quadr_console.c
)
target_link_libraries(quadr PRIVATE quadr_logic)

target_include_directories(quadr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CMAKE_EXE_LINKER_FLAGS "")

if(NOT WIN32)
    target_link_libraries(quadr PRIVATE m)
endif()

# ── zlib-ng (vcpkg) ───────────────────────────────────────────────────────
find_package(zlib-ng CONFIG REQUIRED)
target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZLIBNG)
target_link_libraries(quadr PRIVATE zlib-ng::zlib)

# ── zstd (vcpkg) ──────────────────────────────────────────────────────────
find_package(zstd CONFIG REQUIRED)
target_compile_definitions(quadr PRIVATE QUADR_HAVE_ZSTD)
target_link_libraries(quadr PRIVATE zstd::libzstd)

# ── lz4 (vcpkg) ───────────────────────────────────────────────────────────
find_package(lz4 CONFIG REQUIRED)
target_compile_definitions(quadr PRIVATE QUADR_HAVE_LZ4 QUADR_HAVE_LZ4HC)
target_link_libraries(quadr PRIVATE lz4::lz4)

# ── liblzma / 7z (vcpkg) ──────────────────────────────────────────────────
find_package(liblzma CONFIG REQUIRED)
target_compile_definitions(quadr PRIVATE QUADR_HAVE_7Z)
target_link_libraries(quadr PRIVATE liblzma::liblzma)

