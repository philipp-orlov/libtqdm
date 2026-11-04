// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Progress somewhere other than a terminal. A Sink takes the text anywhere; here
// it goes to the system's standard output with a prefix. And snapshot() gives
// the numbers themselves, for a GUI or a metrics endpoint.

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <tqdm/tqdm.hpp>

int main() {
    auto sink = std::make_shared<tqdm::FunctionSink>([](std::string_view line) {
        std::printf("[job 42] %.*s", static_cast<int>(line.size()), line.data());
        std::fflush(stdout);
    });
    auto display = tqdm::Display::create(sink);

    tqdm::Bar bar(tqdm::Options()
                      .display(display)
                      .total(50)
                      .logInterval(std::chrono::milliseconds(150))
                      .format("{percent} {n}/{total} eta {eta}"));
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        bar.update();

        if (i == 25) {
            const tqdm::Snapshot now = bar.snapshot();
            std::printf("{\"done\": %llu, \"of\": %llu, \"seconds\": %.2f}\n", static_cast<unsigned long long>(now.n),
                        static_cast<unsigned long long>(now.total), now.elapsed);
        }
    }
}
