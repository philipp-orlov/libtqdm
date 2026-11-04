// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// What a progress bar can do to the program around it, and this one must not.

#include <chrono>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "support.hpp"
#include "tqdm/group.hpp"
#include "tqdm/iter.hpp"
#include "tqdm/ostream.hpp"

// operator new belongs to the runtime under the sanitizers.
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#define TQDM_TEST_COUNTS_ALLOCATIONS 0
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define TQDM_TEST_COUNTS_ALLOCATIONS 0
#endif
#endif
#ifndef TQDM_TEST_COUNTS_ALLOCATIONS
#define TQDM_TEST_COUNTS_ALLOCATIONS 1
#endif

#if TQDM_TEST_COUNTS_ALLOCATIONS
// GCC sees malloc through the inlined replacement and takes it for a mismatch.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
static size_t allocations = 0;
void* operator new(size_t size) {
    ++allocations;
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr)
        throw std::bad_alloc();
    return memory;
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
#endif

using namespace tqdm;
using check::advanceMs;
using check::Log;
using check::Terminal;

namespace {

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

// Drawing a bar never calls setlocale(), and never depends on what it returns.
TEST(the_locale_is_left_alone) {
    const std::string before = std::setlocale(LC_ALL, nullptr);
    Terminal t(80, 24, true);
    {
        Bar bar(Options().display(t.display).total(10).desc("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"));
        bar.update(10);
    }
    CHECK_EQ(std::string(std::setlocale(LC_ALL, nullptr)), before);
}

// No text in a bar can make it throw.
TEST(bad_utf8_is_just_text) {
    Terminal t(80, 24, true);
    {
        Bar bar(Options().display(t.display).total(10).desc("bad \xC3\x28 \xFF\xFE").postfix("\xE2\x82"));
        bar.setPostfix("\xF0\x90\x28\xBC");
        bar.update(10);
    }
    CHECK_EQ(t.screen().rowsUsed(), 1);
}

// An empty glyph cannot make the bar writer spin forever.
TEST(empty_glyphs_terminate) {
    Terminal t;
    {
        Theme theme = themes::classic();
        theme.partials = {""};
        theme.fill = "";
        theme.empty = "";
        Bar bar(Options().display(t.display).total(10).theme(theme));
        bar.update(5);
        t.display->refresh();
        bar.update(5);
    }
    CHECK_EQ(t.screen().rowsUsed(), 1);
}

// A line wider than the terminal must not wrap and leave another row behind on every redraw.
TEST(a_narrow_terminal_keeps_one_row) {
    Terminal t(30, 24);
    {
        Bar bar(Options().display(t.display).total(100).desc("a long description here").barWidth(50));
        for (int i = 0; i < 100; ++i) {
            advanceMs(200);
            bar.update();
        }
    }
    CHECK_EQ(t.screen().rowsUsed(), 1);
    CHECK_EQ(t.screen().wrappedLines(), 0);
}

// Bars that share a line region write to their own output, whichever it is.
TEST(any_output_for_any_arrangement) {
    std::ostringstream stream;
    {
        auto display = Display::create(std::make_shared<OstreamSink>(stream));
        Group group(display);
        group.add(Options().total(1).format("a {n}")).update();
        group.add(Options().total(1).format("b {n}")).update();
    }
    CHECK_EQ(stream.str(), "a 1\nb 1\n");
}

// Bars stay in the order they were added, whatever happens to them.
TEST(order_is_insertion_order) {
    Terminal t;
    Group group(t.display);
    Bar& a = group.add(Options().total(10).format("a {n}"));
    Bar& b = group.add(Options().total(10).format("b {n}"));
    Bar& c = group.add(Options().total(10).format("c {n}"));
    c.update(10);
    a.update(1);
    b.update(2);
    t.display->refresh();
    const auto lines = t.screen().lines();
    CHECK_EQ(lines[0], "a 1");
    CHECK_EQ(lines[1], "b 2");
    CHECK_EQ(lines[2], "c 10");
    group.finishAll();
}

// Spinners and bars mix in one group, and an update alone gets them drawn.
TEST(spinners_and_bars_mix_and_draw_on_update) {
    Terminal t;
    Group group(t.display);
    Bar& spin = group.add(Options::spinning("connect").spinner(spinners::ascii()));
    Bar& bar = group.add(Options().total(10).format("bar {n}"));
    advanceMs(1000);
    bar.update(3);
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{2});
    CHECK(contains(lines[0], "connect"));
    CHECK_EQ(lines[1], "bar 3");
    spin.finish();
    bar.finish();
}

// No estimate can overflow and print nonsense.
TEST(huge_numbers_print_sanely) {
    const auto huge = std::numeric_limits<uint64_t>::max();
    Terminal t;
    Bar bar(Options()
                .display(t.display)
                .total(huge)
                .format("{percent} {n}/{total} {eta} {rate}")
                .minInterval(std::chrono::milliseconds(0)));
    advanceMs(1);
    bar.update(1);
    advanceMs(1000);
    t.display->refresh();
    CHECK(contains(t.screen().all(), "  0% 1/18446744073709551615 ? "));
    bar.update(huge - 2);
    t.display->refresh();
    CHECK(contains(t.screen().all(), " 99% 18446744073709551614/18446744073709551615"));
    bar.update(1);
    CHECK(bar.finished());
    CHECK(contains(t.screen().all(), "100% 18446744073709551615/18446744073709551615"));
}

// A countdown does not go below zero.
TEST(countdown_does_not_underflow) {
    Terminal t;
    {
        Bar bar(Options().display(t.display).total(3).reverse().format("{percent} {remaining}"));
        bar.update(5); // More than there was.
    }
    CHECK(contains(t.screen().all(), "  0% 0"));
}

// An update takes no lock, renders nothing, flushes nothing and allocates
// nothing: a tight loop stays a tight loop.
TEST(the_hot_path_does_not_allocate) {
    Log log;
    Bar bar(Options().display(log.display).total(10000000).logInterval(std::chrono::hours(1)));
#if TQDM_TEST_COUNTS_ALLOCATIONS
    const size_t before = allocations;
#endif
    for (int i = 0; i < 1000000; ++i)
        bar.update();
#if TQDM_TEST_COUNTS_ALLOCATIONS
    CHECK_EQ(allocations, before);
#endif
    CHECK_EQ(bar.count(), uint64_t{1000000});
    bar.finish();
}

// The total can change while running.
TEST(the_total_can_change) {
    Terminal t;
    Bar bar(Options().display(t.display).total(10).format("{n}/{total}"));
    bar.setTotal(100);
    bar.update(10);
    CHECK(!bar.finished());
    bar.update(90);
    CHECK(bar.finished());
    CHECK_EQ(t.screen().lines().back(), "100/100");
}

// Messages and bars do not overwrite each other.
TEST(messages_and_bars_coexist) {
    Terminal t;
    {
        Bar bar(Options().display(t.display).total(3).format("bar {n}"));
        t.display->write("message one");
        bar.update();
        t.display->write("message two");
        bar.update(2);
    }
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{3});
    CHECK_EQ(lines[0], "message one");
    CHECK_EQ(lines[1], "message two");
    CHECK_EQ(lines[2], "bar 3");
}

// Code that already prints to a stream gets the same, without changing.
TEST(a_stream_can_be_routed_above_the_bars) {
    Terminal t;
    std::ostringstream stream;
    std::ostream& asOstream = stream; // ostringstream::rdbuf() does not say what is installed.
    std::streambuf* const original = asOstream.rdbuf();
    {
        Bar bar(Options().display(t.display).total(1).format("bar {n}"));
        {
            ScopedRedirect redirect(stream, t.display);
            stream << "part one, " << 42 << "\nsecond line\nunfinished";
            CHECK_EQ(t.screen().lines().size(), size_t{3});
            CHECK(asOstream.rdbuf() != original);
        }
        CHECK(asOstream.rdbuf() == original);
        CHECK_EQ(stream.str(), "");
        stream << "direct";
        CHECK_EQ(stream.str(), "direct");
        bar.update();
    }
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{4});
    CHECK_EQ(lines[0], "part one, 42");
    CHECK_EQ(lines[1], "second line");
    CHECK_EQ(lines[2], "unfinished");
    CHECK_EQ(lines[3], "bar 1");
}

// Output piped to a file has no carriage returns or escapes.
TEST(a_pipe_gets_clean_lines) {
    Log log;
    {
        Bar bar(Options().display(log.display).total(3).color(colors::kGreen).desc("job").format("{desc}{n}/{total}"));
        bar.update(3);
    }
    CHECK_EQ(log.sink->text(), "job: 3/3\n");
}

// Elapsed time runs from the start of the work, not from the first tick.
TEST(elapsed_starts_with_the_bar) {
    Terminal t;
    Bar bar(Options().display(t.display).total(2).format("{elapsed}"));
    advanceMs(5000);
    bar.update();
    t.display->refresh();
    CHECK_EQ(t.screen().lines().back(), "00:05");
    bar.finish();
}

int main(int argc, char** argv) { return runAll(argc, argv); }
