cmake_minimum_required(VERSION 4.1)

project(Quadr VERSION 1.5.0 LANGUAGES C CXX)

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

# ── Packaging ───────────────────────────────────────────────────────────────

# Determine platform string for archive name
if(WIN32)
    set(QUADR_PKG_PLATFORM "windows-x64")
    set(QUADR_PKG_EXT "zip")
elseif(APPLE)
    set(QUADR_PKG_PLATFORM "macos-x64")
    set(QUADR_PKG_EXT "tar.gz")
else()
    set(QUADR_PKG_PLATFORM "linux-x64")
    set(QUADR_PKG_EXT "tar.gz")
endif()

set(QUADR_PKG_NAME "quadr-${CMAKE_PROJECT_VERSION}-${QUADR_PKG_PLATFORM}")
set(QUADR_PKG_DIR "${CMAKE_BINARY_DIR}/package/${QUADR_PKG_NAME}")

# Install quadr executable
install(TARGETS quadr DESTINATION .)

# Collect runtime DLL dependencies on Windows
if(WIN32)
    get_target_property(QUADR_BIN_DIR quadr RUNTIME_OUTPUT_DIRECTORY)
    if(NOT QUADR_BIN_DIR)
        set(QUADR_BIN_DIR "${CMAKE_BINARY_DIR}/app")
    endif()

    set(QUADR_RUNTIME_DLLS
        zlib1.dll libzlib.dll
        libzstd.dll zstd.dll
        liblz4.dll lz4.dll
        liblzma.dll lzma.dll
        libgcc_s_seh-1.dll
        libwinpthread-1.dll
        libstdc++-6.dll
        libssp-0.dll
    )

    foreach(_dll ${QUADR_RUNTIME_DLLS})
        find_file(QUADR_DLL_${_dll} NAMES ${_dll}
            PATHS "${QUADR_BIN_DIR}" "${CMAKE_BINARY_DIR}/bin"
            PATH_SUFFIXES "bin"
            NO_DEFAULT_PATH)
        find_file(QUADR_DLL_${_dll}_sys NAMES ${_dll})
        if(QUADR_DLL_${_dll})
            install(FILES "${QUADR_DLL_${_dll}}" DESTINATION . OPTIONAL)
        elseif(QUADR_DLL_${_dll}_sys)
            install(FILES "${QUADR_DLL_${_dll}_sys}" DESTINATION . OPTIONAL)
        endif()
    endforeach()
endif()


# ── Packaging ───────────────────────────────────────────────────────────────

# Determine platform string for archive name
if(WIN32)
    set(QUADR_PKG_PLATFORM "windows-x64")
    set(QUADR_PKG_EXT "zip")
elseif(APPLE)
    set(QUADR_PKG_PLATFORM "macos-x64")
    set(QUADR_PKG_EXT "tar.gz")
else()
    set(QUADR_PKG_PLATFORM "linux-x64")
    set(QUADR_PKG_EXT "tar.gz")
endif()

set(QUADR_PKG_NAME "quadr-${CMAKE_PROJECT_VERSION}-${QUADR_PKG_PLATFORM}")
set(QUADR_PKG_DIR "${CMAKE_BINARY_DIR}/package/${QUADR_PKG_NAME}")

# Install quadr executable
install(TARGETS quadr DESTINATION .)



# Collect runtime DLL dependencies on Windows
if(WIN32)
    # Get the directory of the built quadr.exe
    get_target_property(QUADR_BIN_DIR quadr RUNTIME_OUTPUT_DIRECTORY)
    if(NOT QUADR_BIN_DIR)
        set(QUADR_BIN_DIR "${CMAKE_BINARY_DIR}/app")
    endif()

    # Common MinGW/GCC runtime and library DLLs
    set(QUADR_RUNTIME_DLLS
        zlib1.dll libzlib.dll
        libzstd.dll zstd.dll
        liblz4.dll lz4.dll
        liblzma.dll lzma.dll
        libgcc_s_seh-1.dll
        libwinpthread-1.dll
        libstdc++-6.dll
        libssp-0.dll
    )

    # Try to find each DLL and add to install list
    foreach(_dll ${QUADR_RUNTIME_DLLS})
        # Search in binary output dir first, then PATH
        find_file(QUADR_DLL_${_dll} NAMES ${_dll}
            PATHS "${QUADR_BIN_DIR}" "${CMAKE_BINARY_DIR}/bin"
            PATH_SUFFIXES "bin"
            NO_DEFAULT_PATH)
        find_file(QUADR_DLL_${_dll}_sys NAMES ${_dll})
        if(QUADR_DLL_${_dll})
            install(FILES "${QUADR_DLL_${_dll}}" DESTINATION . OPTIONAL)
        elseif(QUADR_DLL_${_dll}_sys)
            install(FILES "${QUADR_DLL_${_dll}_sys}" DESTINATION . OPTIONAL)
        endif()
    endforeach()

endif()

# Custom package target
add_custom_target(package-release
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/package"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${QUADR_PKG_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${QUADR_PKG_DIR}"
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --prefix "${QUADR_PKG_DIR}" --config Release
    COMMAND ${CMAKE_COMMAND} -E echo "Packaging ${QUADR_PKG_NAME}.${QUADR_PKG_EXT}..."
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/package" tar "cf" "${CMAKE_BINARY_DIR}/${QUADR_PKG_NAME}.${QUADR_PKG_EXT}" "${QUADR_PKG_NAME}"
    COMMAND ${CMAKE_COMMAND} -E echo "Package created: ${CMAKE_BINARY_DIR}/${QUADR_PKG_NAME}.${QUADR_PKG_EXT}"
    DEPENDS quadr
    COMMENT "Creating release package..."
)
