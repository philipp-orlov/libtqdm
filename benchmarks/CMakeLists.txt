# Copyright (c) 2026 Philipp Orlov
# SPDX-License-Identifier: Apache-2.0

add_executable(bench_update bench_update.cpp)
target_link_libraries(bench_update PRIVATE tqdm::tqdm)
target_compile_options(bench_update PRIVATE ${TQDM_WARNINGS})
