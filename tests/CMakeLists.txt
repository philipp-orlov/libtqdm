# Copyright (c) 2026 Philipp Orlov
# SPDX-License-Identifier: Apache-2.0

function(tqdm_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEFINES;OPTIONS" ${ARGN})
    if(NOT ARG_SOURCES)
        set(ARG_SOURCES ${name}.cpp)
    endif()
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE tqdm::tqdm)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
    target_compile_options(${name} PRIVATE ${TQDM_WARNINGS} ${ARG_OPTIONS})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

tqdm_test(test_text)
tqdm_test(test_render)
tqdm_test(test_bar)
tqdm_test(test_iter)
tqdm_test(test_threads)
tqdm_test(test_regressions)
tqdm_test(test_platform)
tqdm_test(test_units SOURCES test_units_a.cpp test_units_b.cpp)

# The hooks a port relies on, exercised on this machine.
tqdm_test(test_backend_custom DEFINES "TQDM_PLATFORM_HEADER=\"custom_platform.hpp\"")
tqdm_test(test_backend_generic DEFINES TQDM_PLATFORM_GENERIC)
tqdm_test(test_platform_generic SOURCES test_platform.cpp DEFINES TQDM_PLATFORM_GENERIC)
tqdm_test(test_bar_no_threads SOURCES test_bar.cpp DEFINES TQDM_NO_THREADS)

# Each header compiles on its own, in every language version from the minimum up.
file(GLOB public_headers RELATIVE "${PROJECT_SOURCE_DIR}/include"
    CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/tqdm/*.hpp")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/self_contained")
foreach(standard 17 20 23)
    set(sources "")
    foreach(header ${public_headers})
        string(MAKE_C_IDENTIFIER "${header}" stem)
        set(source "${generated}/${standard}_${stem}.cpp")
        file(GENERATE OUTPUT "${source}" CONTENT "#include \"${header}\"\n")
        list(APPEND sources "${source}")
    endforeach()
    add_library(tqdm_self_contained_${standard} OBJECT ${sources})
    target_link_libraries(tqdm_self_contained_${standard} PRIVATE tqdm::tqdm)
    target_compile_options(tqdm_self_contained_${standard} PRIVATE ${TQDM_WARNINGS})
    set_target_properties(tqdm_self_contained_${standard} PROPERTIES
        CXX_STANDARD ${standard} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endforeach()

# Its spinner check needs the ticker thread to animate an idle line.
if(TQDM_BUILD_EXAMPLES AND UNIX AND NOT TQDM_NO_THREADS)
    add_test(NAME e2e_tmux
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/e2e_tmux.sh" "${PROJECT_BINARY_DIR}/examples")
    set_tests_properties(e2e_tmux PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180 RUN_SERIAL TRUE)
endif()

if(TQDM_INSTALL)
    add_test(NAME find_package_consumer
        COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${PROJECT_SOURCE_DIR}" "-DBINARY=${PROJECT_BINARY_DIR}"
            "-DGENERATOR=${CMAKE_GENERATOR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/consumer_smoke.cmake")
    set_tests_properties(find_package_consumer PROPERTIES TIMEOUT 180)
endif()
