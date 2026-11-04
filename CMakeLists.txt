# Copyright (c) 2026 Philipp Orlov
# SPDX-License-Identifier: Apache-2.0

# libtqdm: header-only progress bars for C++17. See README.md.
#
#   add_subdirectory(libtqdm)
#   target_link_libraries(my_target PRIVATE tqdm::tqdm)
#
# or, once installed, find_package(tqdm CONFIG REQUIRED) and the same target.
# Or just add include/ to the include path: there is nothing to build.

cmake_minimum_required(VERSION 3.16...3.28)
project(libtqdm VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(TQDM_TOP_LEVEL ON)
else()
    set(TQDM_TOP_LEVEL OFF)
endif()

option(TQDM_BUILD_TESTS "Build the tests" ${TQDM_TOP_LEVEL})
option(TQDM_BUILD_EXAMPLES "Build the examples" ${TQDM_TOP_LEVEL})
option(TQDM_BUILD_BENCHMARKS "Build the benchmarks" OFF)
option(TQDM_INSTALL "Generate the install rules" ${TQDM_TOP_LEVEL})
option(TQDM_NO_THREADS "Leave out the thread that keeps idle lines moving" OFF)
set(TQDM_PLATFORM_HEADER "" CACHE STRING
    "A backend of your own, as an include path (see include/tqdm/platform.hpp)")
option(TQDM_PLATFORM_GENERIC "Use the standard-C backend: log lines only" OFF)

add_library(tqdm INTERFACE)
add_library(tqdm::tqdm ALIAS tqdm)
target_include_directories(tqdm INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(tqdm INTERFACE cxx_std_17)

if(TQDM_NO_THREADS)
    target_compile_definitions(tqdm INTERFACE TQDM_NO_THREADS)
endif()
if(TQDM_PLATFORM_GENERIC)
    target_compile_definitions(tqdm INTERFACE TQDM_PLATFORM_GENERIC)
endif()
if(TQDM_PLATFORM_HEADER)
    target_compile_definitions(tqdm INTERFACE "TQDM_PLATFORM_HEADER=\"${TQDM_PLATFORM_HEADER}\"")
endif()

# std::thread and std::mutex: part of libc on current glibc, -pthread on older
# ones and elsewhere. This is the only dependency there is.
find_package(Threads REQUIRED)
target_link_libraries(tqdm INTERFACE Threads::Threads)

if(MSVC)
    # C4324: a type padded on purpose, to keep the hot counters on their own cache lines.
    set(TQDM_WARNINGS /W4 /WX /utf-8 /wd4324)
else()
    set(TQDM_WARNINGS -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Wold-style-cast -Wcast-qual -Wnon-virtual-dtor -Woverloaded-virtual -Werror)
endif()

if(TQDM_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
if(TQDM_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(TQDM_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(TQDM_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    set(config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/tqdm")

    install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES LICENSE NOTICE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/libtqdm")
    install(TARGETS tqdm EXPORT tqdmTargets)
    install(EXPORT tqdmTargets NAMESPACE tqdm:: DESTINATION "${config_dir}")

    configure_package_config_file(cmake/tqdmConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/tqdmConfig.cmake" INSTALL_DESTINATION "${config_dir}")
    # Compatible within a major version; not tied to a compiler, it has no binaries.
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/tqdmConfigVersion.cmake"
        COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tqdmConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/tqdmConfigVersion.cmake" DESTINATION "${config_dir}")
endif()
