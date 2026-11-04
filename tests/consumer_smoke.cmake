# Copyright (c) 2026 Philipp Orlov
# SPDX-License-Identifier: Apache-2.0

# Installs the library and builds a project against it with find_package().
#   cmake -DSOURCE=... -DBINARY=... -DGENERATOR=... -P consumer_smoke.cmake
set(prefix "${BINARY}/consumer/prefix")
set(project "${BINARY}/consumer/project")
file(REMOVE_RECURSE "${BINARY}/consumer")
file(MAKE_DIRECTORY "${project}")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${ARGN}\n${output}")
    endif()
endfunction()

run("${CMAKE_COMMAND}" --install "${BINARY}" --prefix "${prefix}")

file(WRITE "${project}/CMakeLists.txt" "
cmake_minimum_required(VERSION 3.16)
project(consumer LANGUAGES CXX)
find_package(tqdm 0.1 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE tqdm::tqdm)
")
file(WRITE "${project}/main.cpp" "
#include <tqdm/tqdm.hpp>
int main() {
    for (int i : tqdm::range(3)) { (void)i; }
    return tqdm::Display::standardError() ? 0 : 1;
}
")
run("${CMAKE_COMMAND}" -S "${project}" -B "${project}/build" -G "${GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${prefix}")
run("${CMAKE_COMMAND}" --build "${project}/build")
run("${project}/build/consumer")
