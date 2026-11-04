# Copyright (c) 2026 Philipp Orlov
# SPDX-License-Identifier: Apache-2.0

set(examples quickstart manual download threads nested spinner logging styles custom_output
    parallel_for)
foreach(example ${examples})
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE tqdm::tqdm)
    target_compile_options(${example} PRIVATE ${TQDM_WARNINGS})
    if(BUILD_TESTING)
        add_test(NAME example_${example} COMMAND ${example})
        set_tests_properties(example_${example} PROPERTIES TIMEOUT 60)
    endif()
endforeach()
