// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// A hot parallel loop. One shared counter is cheap, but with many cores adding to
// it billions of times it is still one contended cache line: Bar::Local batches
// per thread and leaves the loop at full speed.

#include <algorithm>
#include <cstdint>
#include <string>
#include <thread>
#include <tqdm/tqdm.hpp>
#include <vector>

int main() {
    constexpr uint64_t kEach = 20000000;
    const unsigned threads = std::max(2u, std::thread::hardware_concurrency());

    tqdm::Bar bar(tqdm::Options().total(kEach * threads).desc("hashing").scale(tqdm::fmt::Scale::Si));
    std::vector<uint64_t> results(threads);
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < threads; ++t) {
        workers.emplace_back([&bar, &results, t] {
            tqdm::Bar::Local local(bar);
            uint64_t hash = 1469598103934665603u + t;
            for (uint64_t i = 0; i < kEach; ++i) {
                hash = (hash ^ i) * 1099511628211u;
                local.update();
            }
            results[t] = hash;
        });
    }
    for (std::thread& worker : workers)
        worker.join();
    tqdm::write("first result: " + std::to_string(results[0]));
}
