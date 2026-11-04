// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Loops in loops stack up by themselves, and the inner bars clear away.

#include <chrono>
#include <thread>
#include <tqdm/tqdm.hpp>

int main() {
    for (int epoch : tqdm::range(3, tqdm::Options().desc("epoch"))) {
        (void)epoch;
        for (int batch : tqdm::range(40, tqdm::Options().desc("batch"))) {
            (void)batch;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}
