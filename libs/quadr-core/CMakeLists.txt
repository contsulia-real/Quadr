cmake_minimum_required(VERSION 4.1)

include(CheckCSourceCompiles)

set(CMAKE_EXE_LINKER_FLAGS "")

# ── SIMD capability probes (GCC/Clang only) ───────────────────────────────

# SSE2
set(CMAKE_REQUIRED_FLAGS "${QUADR_SSE2_FLAG}")
check_c_source_compiles("
    #include <emmintrin.h>
    int main(void){__m128i x=_mm_setzero_si128();(void)x;return 0;}
" QUADR_HAVE_SSE2_RAW)
unset(CMAKE_REQUIRED_FLAGS)

# AVX2
set(CMAKE_REQUIRED_FLAGS "${QUADR_AVX2_FLAG}")
check_c_source_compiles("
    #include <immintrin.h>
    int main(void){__m256i x=_mm256_setzero_si256();(void)x;return 0;}
" QUADR_HAVE_AVX2_RAW)
unset(CMAKE_REQUIRED_FLAGS)

# NEON (AArch64 — automatic, no flag needed)
check_c_source_compiles("
    #include <arm_neon.h>
    int main(void){uint8x16_t x=vdupq_n_u8(0);(void)x;return 0;}
" QUADR_HAVE_NEON_RAW)

# ── Library target ────────────────────────────────────────────────────────
add_library(quadr_core STATIC
    quadr_core.c
    quadr_delta_simd.c
    quadr_entropy_simd.c
    quadr_stream.c
    quadr_xxh3.c
    quadr_backend.c
    quadr_archive.c
)

target_include_directories(quadr_core PUBLIC include)

if (NOT WIN32)
    target_link_libraries(quadr_core PUBLIC m)
endif ()

# ── Apply SIMD definitions and per-file flags ─────────────────────────────
if(QUADR_HAVE_AVX2_RAW)
    target_compile_definitions(quadr_core PRIVATE
        QUADR_HAVE_AVX2=1
        QUADR_HAVE_SSE2=1)
    if(QUADR_AVX2_FLAG)
        set_source_files_properties(
            quadr_delta_simd.c
            quadr_entropy_simd.c
            PROPERTIES COMPILE_OPTIONS "${QUADR_AVX2_FLAG}")
    endif()
    message(STATUS "quadr-core: AVX2 enabled")

elseif(QUADR_HAVE_SSE2_RAW)
    target_compile_definitions(quadr_core PRIVATE QUADR_HAVE_SSE2=1)
    if(QUADR_SSE2_FLAG)
        set_source_files_properties(
            quadr_delta_simd.c
            quadr_entropy_simd.c
            PROPERTIES COMPILE_OPTIONS "${QUADR_SSE2_FLAG}")
    endif()
    message(STATUS "quadr-core: SSE2 enabled")

elseif(QUADR_HAVE_NEON_RAW)
    target_compile_definitions(quadr_core PRIVATE QUADR_HAVE_NEON=1)
    message(STATUS "quadr-core: NEON enabled")

else()
    message(STATUS "quadr-core: scalar fallback only")
endif()

# ── Unit tests ────────────────────────────────────────────────────────────
option(QUADR_BUILD_TESTS "Build unit tests" ON)
if(QUADR_BUILD_TESTS)
    add_executable(test_quadr tests/test_quadr.c)
    target_link_libraries(test_quadr PRIVATE quadr_core)
    if(NOT WIN32)
        target_link_libraries(test_quadr PRIVATE m)
    endif()
    add_test(NAME quadr_core_tests COMMAND test_quadr)
endif()
