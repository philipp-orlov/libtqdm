// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// What a bar costs a loop. Build with TQDM_BUILD_BENCHMARKS=ON in Release, and
// send stderr where you like: redirected to a file it measures the log path.
//
//   bench_update                 tables below, on a pretend terminal
//   bench_update --terminal      a live bar on this very stderr, nanoseconds on stdout

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <tqdm/tqdm.hpp>
#include <vector>

namespace {

using Steady = std::chrono::steady_clock;

volatile uint64_t sinkValue = 0;

template <typename F>
double nsPerOp(uint64_t operations, F&& body) {
    const auto start = Steady::now();
    body();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Steady::now() - start);
    return static_cast<double>(elapsed.count()) / static_cast<double>(operations);
}

// A sink that throws the bytes away, as a very fast terminal would.
class NullSink final : public tqdm::Sink {
public:
    bool write(std::string_view bytes) noexcept override {
        bytes_ += bytes.size();
        ++frames_;
        return true;
    }
    tqdm::Capabilities capabilities() const noexcept override { return {true, true, true}; }
    tqdm::TerminalSize size() const noexcept override { return {100, 30}; }
    uint64_t frames() const { return frames_; }
    uint64_t bytes() const { return bytes_; }

private:
    uint64_t frames_ = 0;
    uint64_t bytes_ = 0;
};

void row(const char* name, double ns) { std::printf("  %-44s %10.2f ns/update\n", name, ns); }

} // namespace

int main(int argc, char** argv) {
    constexpr uint64_t kUpdates = 50000000;

    if (argc > 1 && std::strcmp(argv[1], "--terminal") == 0) {
        tqdm::Bar bar(tqdm::Options().total(kUpdates).desc("live"));
        const double ns = nsPerOp(kUpdates, [&] {
            for (uint64_t i = 0; i < kUpdates; ++i)
                bar.update();
        });
        std::printf("%.2f\n", ns);
        return 0;
    }

    std::printf("single thread, %llu updates\n", static_cast<unsigned long long>(kUpdates));

    row("empty loop", nsPerOp(kUpdates, [&] {
            for (uint64_t i = 0; i < kUpdates; ++i)
                sinkValue = i;
        }));

    row("disabled bar", [&] {
        tqdm::Bar bar(tqdm::Options().disable().total(kUpdates));
        return nsPerOp(kUpdates, [&] {
            for (uint64_t i = 0; i < kUpdates; ++i)
                bar.update();
        });
    }());

    auto nullSink = std::make_shared<NullSink>();
    row("bar on a terminal that discards", [&] {
        tqdm::Bar bar(tqdm::Options().display(tqdm::Display::create(nullSink)).total(kUpdates).desc("x"));
        return nsPerOp(kUpdates, [&] {
            for (uint64_t i = 0; i < kUpdates; ++i)
                bar.update();
        });
    }());
    std::printf("  ... which took %llu frames, %llu bytes\n", static_cast<unsigned long long>(nullSink->frames()),
                static_cast<unsigned long long>(nullSink->bytes()));

    row("range-for over a vector, with a bar", [&] {
        std::vector<uint64_t> items(kUpdates / 10, 1);
        uint64_t sum = 0;
        auto display = tqdm::Display::create(std::make_shared<NullSink>());
        const double ns = nsPerOp(items.size(), [&] {
            for (const uint64_t item : tqdm::iter(items, tqdm::Options().display(display)))
                sum += item;
        });
        sinkValue = sum;
        return ns;
    }());

    const auto threads = std::max(2u, std::min(16u, std::thread::hardware_concurrency()));
    std::printf("%u threads, %llu updates each\n", threads, static_cast<unsigned long long>(kUpdates / 10));
    const auto parallel = [&](bool local) {
        tqdm::Bar bar(tqdm::Options()
                          .display(tqdm::Display::create(std::make_shared<NullSink>()))
                          .total(kUpdates / 10 * threads));
        return nsPerOp(kUpdates / 10, [&] {
            std::vector<std::thread> workers;
            for (unsigned t = 0; t < threads; ++t) {
                workers.emplace_back([&] {
                    if (local) {
                        tqdm::Bar::Local counter(bar);
                        for (uint64_t i = 0; i < kUpdates / 10; ++i)
                            counter.update();
                    } else {
                        for (uint64_t i = 0; i < kUpdates / 10; ++i)
                            bar.update();
                    }
                });
            }
            for (std::thread& worker : workers)
                worker.join();
        });
    };
    row("Bar::update, wall time per update per thread", parallel(false));
    row("Bar::Local, wall time per update per thread", parallel(true));
}
