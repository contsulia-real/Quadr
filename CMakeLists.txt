cmake_minimum_required(VERSION 4.1)

project(Quadr C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_EXE_LINKER_FLAGS "")

# ── Default build type ────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── Compiler flags (MinGW / Linux GCC/Clang only) ─────────────────────────
set(QUADR_WARN_FLAGS  -Wall -Wextra -Wno-unused-function -Wno-comment)
set(QUADR_OPT_FLAGS   -O3)
set(QUADR_DBG_FLAGS   -O0 -g -fsanitize=address,undefined)
set(QUADR_AVX2_FLAG   -mavx2)
set(QUADR_SSE2_FLAG   -msse2)
set(QUADR_SSSE3_FLAG  -mssse3)

# Export flags for subdirectories
set(QUADR_AVX2_FLAG  "${QUADR_AVX2_FLAG}"  CACHE INTERNAL "")
set(QUADR_SSE2_FLAG  "${QUADR_SSE2_FLAG}"  CACHE INTERNAL "")
set(QUADR_SSSE3_FLAG "${QUADR_SSSE3_FLAG}" CACHE INTERNAL "")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(${QUADR_WARN_FLAGS} ${QUADR_DBG_FLAGS})
else()
    add_compile_options(${QUADR_WARN_FLAGS} ${QUADR_OPT_FLAGS})
endif()

enable_testing()
add_subdirectory(libs/quadr-core)
add_subdirectory(libs/quadr-logic)
add_subdirectory(app)
