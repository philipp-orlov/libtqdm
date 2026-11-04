// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Whole bars against a pretend terminal and a pretend clock: what ends up on
// the screen, in which order, and how much gets written to put it there.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "support.hpp"
#include "tqdm/group.hpp"

using namespace tqdm;
using check::advanceMs;
using check::Log;
using check::Terminal;

namespace {

Options on(const Terminal& t) {
    Options options;
    options.display(t.display).theme(themes::hashes());
    return options;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

} // namespace

TEST(draws_at_once_and_ends_on_a_fresh_line) {
    Terminal t;
    {
        Bar bar(on(t).total(10).barWidth(10));
        CHECK(contains(t.screen().all(), "0% [          ] 0/10"));
        CHECK_EQ(t.screen().cursorRow(), 0);
        advanceMs(1000);
        bar.update(5);
        CHECK(contains(t.screen().all(), " 50% [#####     ] 5/10 [00:01<00:01, 5.00 it/s]"));
        advanceMs(1000);
        bar.update(5);
        CHECK(bar.finished());
    }
    const auto screen = t.screen();
    CHECK_EQ(screen.rowsUsed(), 1);
    CHECK(contains(screen.all(), "100% [##########] 10/10 [00:02<00:00, 5.00 it/s]"));
    CHECK_EQ(screen.cursorRow(), 1);
    CHECK_EQ(screen.cursorColumn(), 0);
}

TEST(the_cursor_is_never_left_hidden) {
    Terminal t;
    {
        Bar bar(on(t).total(1000));
        CHECK(!t.screen().cursorHidden());
        for (int i = 0; i < 40; ++i) {
            advanceMs(150);
            bar.update();
            t.display->write("message");
            CHECK(!t.screen().cursorHidden());
            CHECK(t.screen().autowrapOn());
        }
    }
    CHECK(!t.screen().cursorHidden());
    CHECK(t.screen().autowrapOn());
    // And frames really do hide it, or this says nothing.
    CHECK(contains(t.sink->text(), "\x1b[?25l"));
}

TEST(a_million_updates_are_a_handful_of_frames) {
    Terminal t;
    {
        Bar bar(on(t).total(1000000));
        for (int i = 0; i < 1000000; ++i)
            bar.update();
    }
    // The clock stands still: first frame and last frame, and not one more.
    CHECK(t.sink->writes() <= 2);
    CHECK(contains(t.screen().all(), "1000000/1000000"));
}

TEST(redraws_keep_to_the_interval) {
    Terminal t;
    {
        Bar bar(on(t).total(100000).minInterval(std::chrono::milliseconds(100)));
        for (int i = 0; i < 10000; ++i) {
            advanceMs(1); // 10 seconds, so 100 frames at most.
            bar.update();
        }
    }
    CHECK(t.sink->writes() <= 102);
    CHECK(t.sink->writes() >= 50);
}

TEST(frames_stay_small_and_unchanged_ones_are_not_sent) {
    Terminal t;
    Bar bar(on(t).total(1000).format("{percent}").autoFinish(false));
    const size_t writes = t.sink->writes();
    for (int i = 0; i < 9; ++i) {
        advanceMs(200);
        bar.update(); // Still under 1%: the line reads the same.
    }
    CHECK_EQ(t.sink->writes(), writes);
    bar.finish();
}

TEST(unknown_total_counts_and_finishes_with_a_full_stop) {
    Terminal t;
    {
        Bar bar(on(t).unit("file").spinner(spinners::ascii()));
        advanceMs(1000);
        bar.update(7);
        CHECK(contains(t.screen().all(), "- 7 file [00:01, 7.00 file/s]"));
        bar.finish();
    }
    CHECK(contains(t.screen().all(), "* 7 file [00:01, 7.00 file/s]"));
}

TEST(rate_and_eta_follow_a_change_of_pace) {
    Terminal t;
    Bar bar(on(t).total(1000000).smoothing(2).minInterval(std::chrono::milliseconds(0)));
    for (int i = 0; i < 100; ++i) {
        advanceMs(100);
        bar.update(100);
        t.display->refresh();
    }
    double rate = bar.snapshot().rate;
    CHECK(std::fabs(rate - 1000) < 1); // 100 per 100 ms, steady.
    CHECK(std::fabs(bar.snapshot().eta - 990) < 2);
    for (int i = 0; i < 100; ++i) {
        advanceMs(100);
        bar.update(10);
        t.display->refresh();
    }
    rate = bar.snapshot().rate;
    CHECK(std::fabs(rate - 100) < 15); // Ten seconds later, five time constants in.
    bar.finish();
}

TEST(a_stall_shows_in_the_rate) {
    Terminal t;
    Bar bar(on(t).total(100000).smoothing(1).minInterval(std::chrono::milliseconds(0)));
    advanceMs(1000);
    bar.update(1000);
    t.display->refresh();
    CHECK(bar.snapshot().rate > 900);
    for (int i = 0; i < 10; ++i) {
        advanceMs(1000);
        t.display->refresh();
    }
    CHECK(bar.snapshot().rate < 1);
    bar.finish();
}

TEST(smoothing_zero_is_the_plain_average) {
    Terminal t;
    Bar bar(on(t).total(100000).smoothing(0).minInterval(std::chrono::milliseconds(0)));
    advanceMs(1000);
    bar.update(1000);
    t.display->refresh();
    advanceMs(1000);
    t.display->refresh();
    CHECK(std::fabs(bar.snapshot().rate - 500) < 1e-6);
    bar.finish();
}

TEST(initial_counts_for_position_but_not_for_rate) {
    Terminal t;
    Bar bar(on(t).total(1000).initial(500).barWidth(10));
    CHECK(contains(t.screen().all(), " 50% [#####     ] 500/1000"));
    advanceMs(1000);
    bar.update(100);
    t.display->refresh();
    CHECK(contains(t.screen().all(), "600/1000 [00:01<00:04, 100 it/s]"));
    bar.update(400);
    CHECK(contains(t.screen().all(), "1000/1000 [00:01<00:00, 500 it/s]"));
}

TEST(finish_stops_the_clock) {
    Terminal t;
    Bar bar(on(t).total(10));
    advanceMs(1000);
    bar.update(10);
    advanceMs(60000);
    CHECK(std::fabs(bar.snapshot().elapsed - 1.0) < 1e-9);
}

TEST(finishing_early_keeps_the_count_it_got_to) {
    Terminal t;
    {
        Bar bar(on(t).total(100).barWidth(10));
        advanceMs(1000);
        bar.update(50);
        bar.finish();
        bar.finish(); // Once is enough; twice is harmless.
    }
    CHECK(contains(t.screen().all(), " 50% [#####     ] 50/100"));
    CHECK_EQ(t.screen().rowsUsed(), 1);
}

TEST(failing_says_so_and_sticks) {
    Terminal t(80, 24, false, true);
    {
        Bar bar(on(t).total(100).color(colors::kGreen));
        bar.update(10);
        bar.fail("disk full");
        CHECK_EQ(static_cast<int>(bar.state()), static_cast<int>(State::Failed));
        bar.finish();
        CHECK_EQ(static_cast<int>(bar.state()), static_cast<int>(State::Failed));
    }
    CHECK(contains(t.screen().all(), "disk full"));
    CHECK(contains(t.sink->text(), "\x1b[31m"));
}

TEST(an_exception_through_the_scope_marks_a_failure) {
    Terminal t(80, 24, false, true);
    try {
        Bar bar(on(t).total(100));
        bar.update();
        throw std::runtime_error("boom");
    } catch (const std::runtime_error&) {
    }
    CHECK(contains(t.sink->text(), "\x1b[31m"));
    Terminal t2(80, 24, false, true);
    {
        Bar bar(on(t2).total(100));
        bar.update();
    }
    CHECK(!contains(t2.sink->text(), "\x1b[31m"));
}

TEST(a_bar_inside_a_catch_is_not_a_failure) {
    Terminal t(80, 24, false, true);
    try {
        throw std::runtime_error("handled");
    } catch (const std::runtime_error&) {
        Bar bar(on(t).total(1));
        bar.update();
    }
    CHECK(!contains(t.sink->text(), "\x1b[31m"));
}

TEST(reverse_is_a_countdown) {
    Terminal t;
    Bar bar(on(t).total(10).reverse().barWidth(10).format("{percent}{bar} {remaining}"));
    CHECK(contains(t.screen().all(), "100%[##########] 10"));
    bar.update(4);
    t.display->refresh();
    CHECK(contains(t.screen().all(), " 60%[######    ] 6"));
    bar.update(6);
    CHECK(contains(t.screen().all(), "  0%[          ] 0"));
}

TEST(setters_show_up_and_cannot_break_the_line) {
    Terminal t;
    Bar bar(on(t).total(10).format("{desc}{n}{postfix}").desc("one"));
    bar.setDescription("two");
    bar.setPostfix("three");
    t.display->refresh();
    CHECK_EQ(t.screen().lines().back(), "two: 0 three");
    bar.setDescription("line\nbreak");
    bar.setPostfix("carriage\rreturn");
    t.display->refresh();
    CHECK_EQ(t.screen().rowsUsed(), 1);
    CHECK_EQ(t.screen().lines().back(), "line break: 0 carriage return");
    bar.finish();
}

TEST(postfix_fields_keep_their_order) {
    Terminal t;
    Bar bar(on(t).total(10).format("{n}{postfix}"));
    bar.setPostfix("loss", 0.25);
    bar.setPostfix("name", "resnet");
    bar.setPostfix("loss", 0.125);
    t.display->refresh();
    CHECK_EQ(t.screen().lines().back(), "0 loss=0.125, name=resnet");
    bar.setPostfix("plain");
    t.display->refresh();
    CHECK_EQ(t.screen().lines().back(), "0 plain");
    bar.finish();
}

TEST(set_count_goes_both_ways) {
    Terminal t;
    Bar bar(on(t).total(100).format("{n}").minInterval(std::chrono::milliseconds(0)));
    advanceMs(10);
    bar.setCount(60);
    CHECK_EQ(t.screen().lines().back(), "60");
    advanceMs(10);
    bar.setCount(20);
    CHECK_EQ(t.screen().lines().back(), "20");
    advanceMs(10);
    bar.update(5);
    CHECK_EQ(t.screen().lines().back(), "25");
    bar.finish();
}

TEST(reset_starts_over_even_after_finishing) {
    Terminal t;
    Bar bar(on(t).total(5).format("{n}/{total}"));
    bar.update(5);
    CHECK(bar.finished());
    bar.reset(9);
    CHECK(!bar.finished());
    CHECK_EQ(bar.count(), uint64_t{0});
    bar.update(9);
    CHECK(bar.finished());
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{2});
    CHECK_EQ(lines[0], "5/5");
    CHECK_EQ(lines[1], "9/9");
}

TEST(disabled_counts_but_says_nothing) {
    Terminal t;
    {
        Bar bar(on(t).total(10).disable());
        bar.update(3);
        bar.setPostfix("x");
        CHECK_EQ(bar.count(), uint64_t{3});
        bar.reset();
    }
    CHECK_EQ(t.sink->writes(), size_t{0});

    auto sink = std::make_shared<MemorySink>(Capabilities{true, false, false}, TerminalSize{80, 24});
    DisplayConfig config;
    config.disabled = true;
    config.ticker = false;
    {
        Bar bar(Options().display(Display::create(sink, config)).total(10));
        bar.update(10);
    }
    CHECK_EQ(sink->writes(), size_t{0});
}

TEST(bars_stack_in_the_order_they_came) {
    Terminal t;
    Bar a(on(t).total(10).format("a {n}"));
    Bar b(on(t).total(10).format("b {n}"));
    Bar c(on(t).total(10).format("c {n}"));
    advanceMs(1000);
    b.update(5);
    t.display->refresh();
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{3});
    CHECK_EQ(lines[0], "a 0");
    CHECK_EQ(lines[1], "b 5");
    CHECK_EQ(lines[2], "c 0");
}

TEST(a_nested_bar_vanishes_and_the_outer_one_stays) {
    Terminal t;
    {
        Bar outer(on(t).total(3).format("outer {n}"));
        for (int i = 0; i < 3; ++i) {
            advanceMs(1000);
            {
                Bar inner(on(t).total(4).format("inner {n}"));
                for (int j = 0; j < 4; ++j) {
                    advanceMs(100);
                    inner.update();
                }
            }
            outer.update();
        }
    }
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{1});
    CHECK_EQ(lines[0], "outer 3");
}

TEST(leave_can_be_chosen_either_way) {
    Terminal t;
    {
        Bar gone(on(t).total(1).format("gone").leave(false));
        gone.update();
    }
    CHECK(t.screen().lines().empty());
    {
        Bar outer(on(t).total(1).format("outer"));
        Bar kept(on(t).total(1).format("kept").leave(true));
        kept.update();
        outer.update();
    }
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{2});
    CHECK_EQ(lines[0], "outer");
    CHECK_EQ(lines[1], "kept");
}

TEST(a_finished_line_waits_for_the_ones_above) {
    Terminal t;
    auto first = std::make_unique<Bar>(on(t).total(2).format("first {n}").leave(true));
    {
        Bar second(on(t).total(2).format("second {n}").leave(true));
        second.update(2);
    }
    t.display->refresh(); // The second object is gone; its line must not be.
    auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{2});
    CHECK_EQ(lines[1], "second 2");
    first->update(2);
    first.reset();
    lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{2});
    CHECK_EQ(lines[0], "first 2");
    CHECK_EQ(lines[1], "second 2");
    CHECK_EQ(t.screen().cursorRow(), 2);
}

TEST(write_goes_above_the_bars) {
    Terminal t;
    {
        Bar a(on(t).total(2).format("a {n}"));
        Bar b(on(t).total(2).format("b {n}").leave(true));
        t.display->write("one");
        t.display->write("two\n");
        t.display->write("three\nfour");
        const auto lines = t.screen().lines();
        CHECK_EQ(lines.size(), size_t{6});
        CHECK_EQ(lines[0], "one");
        CHECK_EQ(lines[1], "two");
        CHECK_EQ(lines[2], "three");
        CHECK_EQ(lines[3], "four");
        CHECK_EQ(lines[4], "a 0");
        CHECK_EQ(lines[5], "b 0");
        a.update(2);
        b.update(2);
    }
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{6});
    CHECK_EQ(lines[4], "a 2");
    CHECK_EQ(t.screen().cursorColumn(), 0);
}

TEST(write_with_no_bars_is_just_a_line) {
    Terminal t;
    t.display->write("alone");
    t.display->write("again");
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{2});
    CHECK_EQ(lines[1], "again");
    CHECK_EQ(t.screen().cursorRow(), 2);
}

TEST(a_long_message_overwrites_nothing) {
    Terminal t(20, 10);
    Bar a(on(t).total(2).format("aaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    Bar b(on(t).total(2).format("bbbb"));
    t.display->write(std::string(45, 'm'));
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{5}); // Three rows of message, two of bars.
    CHECK_EQ(lines[3].substr(0, 4), "aaaa");
    CHECK_EQ(lines[4], "bbbb");
}

TEST(a_region_on_a_scrolling_terminal_stays_together) {
    Terminal t(40, 5);
    Bar a(on(t).total(1000).format("a {n}"));
    Bar b(on(t).total(1000).format("b {n}"));
    for (int i = 0; i < 20; ++i)
        t.display->write("log " + std::to_string(i));
    a.update(7);
    t.display->refresh();
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{22});
    CHECK_EQ(lines[0], "log 0");
    CHECK_EQ(lines[19], "log 19");
    CHECK_EQ(lines[20], "a 7");
    CHECK_EQ(lines[21], "b 0");
}

TEST(a_region_taller_than_the_terminal_shows_what_fits) {
    Terminal t(40, 5);
    std::vector<std::unique_ptr<Bar>> bars;
    for (int i = 0; i < 10; ++i) {
        bars.push_back(std::make_unique<Bar>(on(t).total(100).format("bar" + std::to_string(i) + " {n}")));
    }
    for (int round = 0; round < 3; ++round) {
        advanceMs(1000);
        for (auto& bar : bars)
            bar->update();
        t.display->refresh();
    }
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{4});
    CHECK_EQ(lines[0], "bar0 3");
    CHECK_EQ(lines[2], "bar2 3");
    CHECK_EQ(lines[3], "... 7 more");
    bars.clear();
    CHECK_EQ(t.screen().all().find("more"), std::string::npos);
}

TEST(no_line_ever_wraps) {
    for (int columns : {1, 2, 5, 10, 20, 40, 41, 79, 80}) {
        Terminal t(columns, 24);
        {
            Bar bar(on(t).total(100000).desc("a description of some length").postfix("post"));
            for (int i = 0; i < 20; ++i) {
                advanceMs(200);
                bar.update(5000);
            }
        }
        const auto screen = t.screen();
        CHECK_EQ(screen.wrappedLines(), 0);
        CHECK(screen.rowsUsed() <= 1);
    }
}

TEST(a_wide_description_does_not_wrap_either) {
    Terminal t(20, 24, true);
    {
        Bar bar(
            on(t).total(10).desc("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE6\x97\xA5"
                                 "\xE6\x9C\xAC\xE8\xAA\x9E\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"));
        bar.update(10);
    }
    CHECK_EQ(t.screen().wrappedLines(), 0);
    CHECK_EQ(t.screen().rowsUsed(), 1);
}

TEST(the_bar_follows_the_terminal_width) {
    Terminal t(80, 24);
    Bar bar(on(t).total(10).format("{bar}"));
    t.display->refresh();
    CHECK_EQ(unicode::displayWidth(t.screen().lines().back()), 79);
    t.sink->resize({60, 24});
    t.columns = 60;
    t.display->refresh();
    const std::string frames = t.sink->text();
    // Caps take two cells of the line. For a real terminal rewrapping the old
    // line under a resize, see tests/e2e_tmux.sh.
    CHECK(contains(frames, "[" + std::string(77, ' ') + "]"));
    CHECK(contains(frames, "[" + std::string(57, ' ') + "]"));
    bar.finish();
}

TEST(max_width_and_terminal_of_unknown_size) {
    Terminal t;
    {
        Bar bar(on(t).total(10).format("{bar}").maxWidth(30));
        t.display->refresh();
        CHECK_EQ(unicode::displayWidth(t.screen().lines().back()), 30);
    }
    auto sink = std::make_shared<MemorySink>(Capabilities{true, false, false}, TerminalSize{0, 0});
    DisplayConfig config;
    config.ticker = false;
    {
        Bar bar(Options().display(Display::create(sink, config)).total(10).format("{bar}").theme(themes::hashes()));
        bar.finish();
    }
    check::Screen screen(200, 24);
    screen.feed(sink->text());
    CHECK_EQ(unicode::displayWidth(screen.lines().back()), 79); // Assume 80 columns.
}

TEST(a_log_gets_lines_and_never_an_escape) {
    Log log;
    {
        Bar bar(Options()
                    .display(log.display)
                    .total(100)
                    .theme(themes::hashes())
                    .barWidth(10)
                    .logInterval(std::chrono::seconds(10))
                    .color(colors::kRed));
        for (int i = 0; i < 99; ++i) {
            advanceMs(500);
            bar.update();
        }
        bar.update();
    }
    const std::string& text = log.sink->text();
    CHECK(!contains(text, "\x1b"));
    CHECK(!contains(text, "\r"));
    const auto lines = static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
    CHECK(lines >= 4 && lines <= 6); // 50 seconds at one line per 10, and the end.
    CHECK(contains(text, "100% [##########] 100/100"));
}

TEST(a_short_job_in_a_log_is_one_line) {
    Log log;
    {
        Bar bar(Options().display(log.display).total(1000).format("{n}/{total}"));
        for (int i = 0; i < 1000; ++i)
            bar.update();
    }
    CHECK_EQ(log.sink->text(), "1000/1000\n");
}

TEST(a_nested_bar_in_a_log_keeps_quiet) {
    Log log;
    {
        Bar outer(Options().display(log.display).total(2).format("outer {n}"));
        for (int i = 0; i < 2; ++i) {
            Bar inner(Options().display(log.display).total(1).format("inner {n}"));
            inner.update();
            outer.update();
        }
    }
    CHECK_EQ(log.sink->text(), "outer 2\n");
}

TEST(write_in_a_log_is_a_line) {
    Log log;
    log.display->write("hello");
    log.display->write("world\n");
    CHECK_EQ(log.sink->text(), "hello\nworld\n");
}

TEST(a_failing_output_stops_the_drawing_not_the_program) {
    int calls = 0;
    auto sink = std::make_shared<FunctionSink>(
        [&](std::string_view) {
            ++calls;
            throw std::runtime_error("gone");
        },
        Capabilities{true, false, false}, TerminalSize{80, 24});
    DisplayConfig config;
    config.clock = check::fakeClock;
    config.ticker = false;
    {
        Bar bar(Options().display(Display::create(sink, config)).total(100));
        for (int i = 0; i < 100; ++i) {
            advanceMs(1000);
            bar.update();
        }
        CHECK(bar.finished());
    }
    CHECK_EQ(calls, 1);
}

TEST(a_failed_output_stays_quiet_and_keeps_working) {
    auto sink = std::make_shared<FunctionSink>([](std::string_view) { throw std::runtime_error("gone"); },
                                               Capabilities{true, false, false}, TerminalSize{80, 24});
    DisplayConfig config;
    config.clock = check::fakeClock;
    config.ticker = false;
    const auto display = Display::create(sink, config);
    display->write("this one breaks it");
    for (int i = 0; i < 1000; ++i) {
        display->write(std::string(1000, 'x'));
        Bar bar(Options().display(display).total(1).leave(true));
        bar.update();
    }
    Bar last(Options().display(display).total(1));
    CHECK(!last.finished()); // Still works, in that it does nothing.
    last.finish();
}

TEST(the_cursor_ends_on_the_last_line_whichever_line_changed) {
    Terminal t(40, 24);
    Bar a(on(t).total(1000).format("a {n}"));
    Bar b(on(t).total(1000).format("b is the last line"));
    advanceMs(1000);
    a.update(5);
    t.display->refresh();
    const auto screen = t.screen();
    CHECK_EQ(screen.cursorRow(), 1);
    CHECK_EQ(screen.cursorColumn(), 18);
}

namespace {

class Header final : public Line {
public:
    void render(uint64_t, int width, std::string& out) override {
        out = "== status: " + text + " (" + std::to_string(width) + ")";
    }
    std::string text = "starting";
};

} // namespace

TEST(any_line_can_join_a_region) {
    Terminal t;
    Header header;
    t.display->attach(header);
    Bar bar(on(t).total(10).format("{n}"));
    header.text = "running";
    t.display->refresh();
    auto lines = t.screen().lines();
    CHECK_EQ(lines[0], "== status: running (79)");
    CHECK_EQ(lines[1], "0");
    t.display->detach(header, false);
    t.display->refresh();
    lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{1});
    CHECK_EQ(lines[0], "0");
    bar.finish();
}

TEST(group_owns_orders_and_finishes) {
    Terminal t;
    {
        Group group(t.display);
        Bar& a = group.add(Options().total(10).format("a {n}"));
        Bar& b = group.add(Options().total(10).format("b {n}"));
        CHECK_EQ(group.size(), size_t{2});
        CHECK(&group[1] == &b);
        b.update(10);
        CHECK(!group.allFinished());
        a.update(3);
        group.write("message");
        group.finishAll();
        CHECK(group.allFinished());
    }
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{3});
    CHECK_EQ(lines[0], "message");
    CHECK_EQ(lines[1], "a 3");
    CHECK_EQ(lines[2], "b 10");
}

TEST(a_group_can_be_filled_in_as_it_goes) {
    Terminal t;
    Group group(t.display);
    for (int i = 0; i < 5; ++i) {
        Bar& bar = group.add(Options().total(1).format("job" + std::to_string(i)));
        if (i % 2 == 0)
            bar.update();
    }
    group.finishAll();
    const auto lines = t.screen().lines();
    CHECK_EQ(lines.size(), size_t{5});
    for (size_t i = 0; i < 5; ++i)
        CHECK_EQ(lines[i], "job" + std::to_string(i));
}

TEST(a_custom_format_works_for_total_or_no_total) {
    Terminal t;
    Bar bar(on(t).format("[{n} of {total}] {bar:4}"));
    t.display->refresh();
    CHECK(contains(t.screen().all(), "[0 of ?]"));
    bar.setTotal(4);
    bar.update(1);
    t.display->refresh();
    CHECK(contains(t.screen().all(), "[1 of 4] [#   ]"));
    bar.finish();
}

TEST(the_total_can_move) {
    Terminal t;
    Bar bar(on(t).total(10).format("{n}/{total}").autoFinish(false));
    bar.update(10);
    CHECK(!bar.finished());
    CHECK_EQ(t.screen().lines().back(), "10/10");
    bar.setTotal(20);
    advanceMs(1000);
    bar.update(1);
    CHECK_EQ(t.screen().lines().back(), "11/20");
    bar.finish();
}

TEST(lowering_the_total_finishes_on_the_next_update) {
    Terminal t;
    Bar bar(on(t).total(10).format("{n}/{total}"));
    bar.update(5);
    bar.setTotal(5);
    bar.update(0);
    CHECK(bar.finished());
}

TEST(local_batches_and_flushes) {
    Terminal t;
    Bar bar(on(t).total(1000).autoFinish(false));
    {
        Bar::Local local(bar, 100);
        for (int i = 0; i < 99; ++i)
            local.update();
        CHECK_EQ(bar.count(), uint64_t{0});
        local.update();
        CHECK_EQ(bar.count(), uint64_t{100});
        local.update(5);
    }
    CHECK_EQ(bar.count(), uint64_t{105});
    bar.finish();
}

int main(int argc, char** argv) { return runAll(argc, argv); }
