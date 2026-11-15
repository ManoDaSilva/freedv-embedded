cmake_minimum_required(VERSION 3.16)

project(freedv-embedded VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FDVE_BUILD_TESTS "Build tests" ON)
option(FDVE_WITH_ALSA "Build the ALSA audio backend" ON)

# --- Upstream FreeDV code -----------------------------------------------------
# Nothing from FreeDV is copied into this repository. The modem (RADE), the
# vocoder (FARGAN, from Opus) and the audio pipeline come from the
# freedv-backend submodule, which in turn fetches freedv/rade_c at build time.
set(FDV_BACKEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/freedv-backend)
if(NOT EXISTS ${FDV_BACKEND_DIR}/CMakeLists.txt)
    message(FATAL_ERROR
        "external/freedv-backend is empty. Run: git submodule update --init")
endif()

# Reporting (FreeDV Reporter, PSK Reporter) is not used, so TLS is not needed.
set(DISABLE_TLS_SUPPORT ON CACHE BOOL "No TLS: reporting is not used" FORCE)
add_subdirectory(${FDV_BACKEND_DIR} EXCLUDE_FROM_ALL)

# librade is built by the backend as a shared library inside the build tree.
set(FDVE_RADE_LIB_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/freedv-backend/rade_build/src)

# --- Third party: TOML parser -------------------------------------------------
find_package(tomlplusplus 3.3 QUIET)
if(NOT tomlplusplus_FOUND)
    include(FetchContent)
    FetchContent_Declare(tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(tomlplusplus)
endif()

if(FDVE_WITH_ALSA)
    find_package(ALSA REQUIRED)
endif()

find_package(Threads REQUIRED)
include(GNUInstallDirs)
add_subdirectory(src)

if(FDVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(FILES config/freedv-embedded.example.toml
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/freedv-embedded)
