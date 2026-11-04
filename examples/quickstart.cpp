// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Wrap a loop. That is all most programs ever need.
//
//   quickstart [item count]

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <tqdm/tqdm.hpp>
#include <vector>

int main(int argc, char** argv) {
    const int count = argc > 1 ? std::atoi(argv[1]) * 10 : 30;

    std::vector<std::string> files(static_cast<size_t>(count), "data.bin");
    for (const std::string& file : tqdm::iter(files, tqdm::Options().desc("files").unit("file"))) {
        (void)file;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
