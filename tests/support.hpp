// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// A pretend terminal and a pretend clock, for tests of whole bars.

#include <cstdint>
#include <memory>
#include <string>

#include "check.hpp"
#include "screen.hpp"
#include "tqdm/bar.hpp"
#include "tqdm/display.hpp"
#include "tqdm/sink.hpp"

namespace check {

inline uint64_t fakeNs = 1'000'000'000;

inline uint64_t fakeClock() { return fakeNs; }

inline void advanceMs(uint64_t ms) { fakeNs += ms * 1'000'000; }

struct Terminal {
    int columns;
    int rows;
    std::shared_ptr<tqdm::MemorySink> sink;
    tqdm::Display::Ptr display;

    explicit Terminal(int columns_ = 80, int rows_ = 24, bool utf8 = false, bool color = false)
        : columns(columns_), rows(rows_),
          sink(std::make_shared<tqdm::MemorySink>(tqdm::Capabilities{true, color, utf8},
                                                  tqdm::TerminalSize{columns_, rows_})) {
        tqdm::DisplayConfig config;
        config.clock = fakeClock;
        config.ticker = false;
        display = tqdm::Display::create(sink, config);
    }

    Screen screen() const {
        Screen screen(columns, rows);
        screen.feed(sink->text());
        return screen;
    }
};

// A log: no terminal abilities.
struct Log {
    std::shared_ptr<tqdm::MemorySink> sink = std::make_shared<tqdm::MemorySink>();
    tqdm::Display::Ptr display;

    Log() {
        tqdm::DisplayConfig config;
        config.clock = fakeClock;
        display = tqdm::Display::create(sink, config);
    }
};

} // namespace check
