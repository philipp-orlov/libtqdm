// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Many threads at one display, real clock, ticker running. Worth running under
// ThreadSanitizer (the CI does): most of what this finds, it finds there.

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "support.hpp"
#include "tqdm/group.hpp"
#include "tqdm/iter.hpp"

using namespace tqdm;

namespace {

struct LiveTerminal {
    std::shared_ptr<MemorySink> sink =
        std::make_shared<MemorySink>(Capabilities{true, true, true}, TerminalSize{100, 40});
    Display::Ptr display = Display::create(sink);
};

} // namespace

TEST(one_bar_many_writers_no_update_is_lost) {
    LiveTerminal t;
    constexpr int kThreads = 8;
    constexpr int kEach = 200000;
    {
        Bar bar(Options().display(t.display).total(kThreads * kEach));
        std::vector<std::thread> workers;
        for (int i = 0; i < kThreads; ++i) {
            workers.emplace_back([&] {
                for (int j = 0; j < kEach; ++j)
                    bar.update();
            });
        }
        for (auto& worker : workers)
            worker.join();
        CHECK_EQ(bar.count(), uint64_t{kThreads * kEach});
        CHECK(bar.finished());
    }
    check::Screen screen(100, 40);
    screen.feed(t.sink->text());
    CHECK_EQ(screen.rowsUsed(), 1);
    CHECK(screen.all().find("1600000/1600000") != std::string::npos);
    CHECK(!screen.cursorHidden());
}

TEST(everything_at_once) {
    LiveTerminal t;
    constexpr int kThreads = 6;
    {
        Group group(t.display);
        std::vector<std::thread> workers;
        std::atomic<int> done{0};
        for (int i = 0; i < kThreads; ++i) {
            workers.emplace_back([&, i] {
                Bar& mine = group.add(Options()
                                          .total(20000)
                                          .desc("worker " + std::to_string(i))
                                          .minInterval(std::chrono::milliseconds(0)));
                for (int j = 0; j < 20000; ++j) {
                    mine.update();
                    if (j % 512 == 0) {
                        mine.setPostfix("j", static_cast<double>(j));
                        mine.setDescription("worker " + std::to_string(i) + " on " + std::to_string(j));
                        (void)mine.snapshot();
                    }
                    if (j % 4096 == 0) {
                        group.write("worker " + std::to_string(i) + " says hello");
                        Bar passing(Options().display(t.display).total(3).leave(false));
                        passing.update(3);
                    }
                }
                done.fetch_add(1);
            });
        }
        for (auto& worker : workers)
            worker.join();
        CHECK_EQ(done.load(), kThreads);
        CHECK(group.allFinished());
    }
    check::Screen screen(100, 40);
    screen.feed(t.sink->text());
    CHECK(!screen.cursorHidden());
    CHECK_EQ(screen.wrappedLines(), 0);
    int hellos = 0;
    int bars = 0;
    for (const std::string& line : screen.lines()) {
        hellos += line.find("says hello") != std::string::npos ? 1 : 0;
        bars += line.find("20000/20000") != std::string::npos ? 1 : 0;
    }
    CHECK_EQ(hellos, kThreads * 5); // Every message arrives whole.
    CHECK_EQ(bars, kThreads);       // And every bar's last line.
}

#if !defined(TQDM_NO_THREADS)
TEST(the_ticker_moves_a_line_that_nothing_updates) {
    LiveTerminal t;
    {
        Bar bar(Options::spinning("waiting").display(t.display).spinner(spinners::ascii()));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
    // The sink belongs to the display until the last bar is gone.
    const std::string& text = t.sink->text();
    CHECK(text.find("\\ waiting") != std::string::npos);
    CHECK(text.find("| waiting") != std::string::npos);
}
#endif

TEST(tickers_come_and_go_with_the_bars) {
    LiveTerminal t;
    for (int i = 0; i < 200; ++i) {
        Bar bar(Options().display(t.display).total(2));
        bar.update(2);
    }
    // And two threads racing to be the first and the last bar on a display.
    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i) {
        workers.emplace_back([&] {
            for (int j = 0; j < 200; ++j) {
                Bar bar(Options().display(t.display).total(1).leave(false));
                bar.update();
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    CHECK(true); // Getting here without a hang or a sanitizer report is the test.
}

TEST(a_display_outlives_whoever_made_it) {
    auto sink = std::make_shared<MemorySink>(Capabilities{true, false, false}, TerminalSize{80, 24});
    std::unique_ptr<Bar> bar;
    {
        auto display = Display::create(sink);
        bar = std::make_unique<Bar>(Options().display(display).total(10));
    }
    bar->update(10);
    bar.reset();
    CHECK(sink->text().find("10/10") != std::string::npos);
}

TEST(a_loop_in_each_thread) {
    LiveTerminal t;
    std::atomic<uint64_t> sum{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            for (int value : range(5000, Options().display(t.display)))
                sum.fetch_add(static_cast<uint64_t>(value), std::memory_order_relaxed);
        });
    }
    for (auto& worker : workers)
        worker.join();
    CHECK_EQ(sum.load(), uint64_t{4} * (4999 * 5000 / 2));
}

int main(int argc, char** argv) { return runAll(argc, argv); }
