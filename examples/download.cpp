// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Bytes: scaled counts and rates. A transfer that resumes, and one whose size
// the server never said.

#include <chrono>
#include <cstdint>
#include <thread>
#include <tqdm/tqdm.hpp>

int main() {
    constexpr uint64_t kSize = 734003200;
    {
        tqdm::Bar bar(tqdm::Options().bytes().total(kSize).initial(kSize / 4).desc("ubuntu.iso"));
        for (uint64_t got = kSize / 4; got < kSize; got += 16777216) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            bar.update(got + 16777216 < kSize ? 16777216 : kSize - got);
        }
    }
    {
        tqdm::Bar bar(tqdm::Options().bytes().desc("stream"));
        for (int chunk = 0; chunk < 40; ++chunk) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            bar.update(1048576);
        }
    }
}
