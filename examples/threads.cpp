// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// One bar per thread, in a fixed order, with messages scrolling by above them.
// Nothing here is shared but the Group; the bars need no coordination.

#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <thread>
#include <tqdm/tqdm.hpp>
#include <vector>

constexpr uint64_t kChunk = 1048576;

int main() {
    tqdm::Group group;
    std::vector<std::thread> workers;

    for (int i = 0; i < 4; ++i) {
        const std::string name = "layer-" + std::to_string(i);
        tqdm::Bar& bar = group.add(tqdm::Options().bytes().total(100 * kChunk).desc(name));
        workers.emplace_back([&group, &bar, name, seed = i] {
            std::mt19937 random(static_cast<unsigned>(seed));
            while (!bar.finished()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5 + random() % 20));
                bar.update(kChunk);
            }
            group.write("finished " + name);
        });
    }
    for (std::thread& worker : workers)
        worker.join();
}
