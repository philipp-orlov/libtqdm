// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// renderLine() alone: format strings, bars, themes, limits. No clock, no terminal.

#include <string>
#include <vector>

#include "check.hpp"
#include "tqdm/render.hpp"

using namespace tqdm;
using unicode::displayWidth;

namespace {

struct Fixture {
    Theme theme = detail::usableTheme(themes::classic());
    Spinner spinner = spinners::ascii();
    std::string label;
    std::string postfix;
    std::string unit = "it";
    fmt::Scale scale = fmt::Scale::None;
    Color color;
    bool useColor = true;
    bool useUtf8 = false;
    int barWidth = 0;

    std::string render(const std::string& format, const Snapshot& snapshot, int width = 0) const {
        const auto tokens = detail::parseFormat(format);
        LineSpec spec;
        spec.tokens = &tokens;
        spec.theme = &theme;
        spec.spinner = &spinner;
        spec.label = label;
        spec.postfix = postfix;
        spec.unit = unit;
        spec.scale = scale;
        spec.barColor = color;
        spec.barWidth = barWidth;
        spec.useColor = useColor;
        spec.useUtf8 = useUtf8;
        std::string out;
        renderLine(spec, snapshot, width, out);
        return out;
    }
};

Snapshot at(uint64_t n, uint64_t total) {
    Snapshot s;
    s.n = n;
    s.total = total;
    s.fraction = total == 0 ? -1 : static_cast<double>(n) / static_cast<double>(total);
    return s;
}

std::string filled(const Theme& theme, double fraction, int cells) {
    std::string out;
    detail::appendFilled(out, theme, fraction, cells);
    return out;
}

} // namespace

TEST(literals_and_escaped_braces) {
    Fixture f;
    CHECK_EQ(f.render("plain", at(0, 1)), "plain");
    CHECK_EQ(f.render("{{n}} = {n}", at(7, 10)), "{n} = 7");
    CHECK_EQ(f.render("a}}b", at(0, 1)), "a}b");
}

TEST(a_typo_in_a_format_shows_up_and_nothing_breaks) {
    Fixture f;
    CHECK_EQ(f.render("{nope} {n", at(1, 2)), "{nope} {n");
    CHECK_EQ(f.render("{", at(1, 2)), "{");
    CHECK_EQ(f.render("}", at(1, 2)), "}");
    CHECK_EQ(f.render("{}", at(1, 2)), "{}");
    CHECK_EQ(f.render("{bar:x} {percent:3} {n:4} {bar:99999}", at(1, 2)), "{bar:x} {percent:3} {n:4} {bar:99999}");
}

TEST(percent_is_floored_and_steady_in_width) {
    Fixture f;
    CHECK_EQ(f.render("{percent}", at(0, 100)), "  0%");
    CHECK_EQ(f.render("{percent}", at(29, 100)), " 29%"); // 28.999... in binary.
    CHECK_EQ(f.render("{percent}", at(999, 1000)), " 99%");
    CHECK_EQ(f.render("{percent}", at(100, 100)), "100%");
    CHECK_EQ(f.render("{percent}", at(0, 0)), "  ?%");
    CHECK_EQ(f.render("{percent:.1}", at(425, 1000)), " 42.5%");
    CHECK_EQ(f.render("{percent:.2}", at(1, 3)), " 33.33%");
    Snapshot nearly = at(999999999, 1000000000);
    nearly.fraction = 0.9999999999;
    CHECK_EQ(f.render("{percent}", nearly), " 99%");
    CHECK_EQ(f.render("{percent:.1}", nearly), " 99.9%");
}

TEST(numbers_and_durations) {
    Fixture f;
    Snapshot s = at(25, 100);
    s.elapsed = 65;
    s.eta = 195.5;
    s.rate = 0.3846;
    CHECK_EQ(f.render("{n}/{total} {remaining} {elapsed}<{eta} {rate} {unit}", s),
             "25/100 75 01:05<03:16 2.60 s/it it");
    CHECK_EQ(f.render("{n}/{total} {remaining} {eta} {rate}", at(5, 0)), "5/? ? ? ? it/s");
    s = at(3 * 1024 * 1024, 10 * 1024 * 1024);
    s.rate = 1536;
    f.scale = fmt::Scale::Binary;
    f.unit = "B";
    CHECK_EQ(f.render("{n}{unit}/{total}{unit} {rate}", s), "3.00MiB/10.0MiB 1.50 KiB/s");
}

TEST(desc_and_postfix_bring_their_own_separators) {
    Fixture f;
    CHECK_EQ(f.render("{desc}x{postfix}", at(0, 1)), "x");
    f.label = "load";
    f.postfix = "ok";
    CHECK_EQ(f.render("{desc}x{postfix}", at(0, 1)), "load: x ok");
    CHECK_EQ(f.render("{label}", at(0, 1)), "load");
}

TEST(classic_bar_positions) {
    const Theme t = themes::classic();
    CHECK_EQ(filled(t, 0, 10), ">         ");
    CHECK_EQ(filled(t, 0.25, 10), "==>       ");
    CHECK_EQ(filled(t, 0.999, 10), "=========>");
    CHECK_EQ(filled(t, 1, 10), "==========");
    CHECK_EQ(filled(t, 2, 10), "==========");  // Clamped.
    CHECK_EQ(filled(t, -1, 10), ">         "); // Clamped.
}

TEST(partial_cells_step_through_the_glyphs) {
    Theme t;
    t.fill = "#";
    t.partials = {"1", "2", "3"};
    CHECK_EQ(filled(t, 0, 2), "  ");
    CHECK_EQ(filled(t, 0.125, 2), "1 ");
    CHECK_EQ(filled(t, 0.25, 2), "2 ");
    CHECK_EQ(filled(t, 0.375, 2), "3 ");
    CHECK_EQ(filled(t, 0.5, 2), "# ");
    CHECK_EQ(filled(t, 0.625, 2), "#1");
    CHECK_EQ(filled(t, 1, 2), "##");
}

TEST(every_theme_is_exactly_the_width_at_every_fraction) {
    std::vector<Theme> all = {themes::ascii(), themes::classic(), themes::hashes(), themes::blocks(),
                              themes::line(),  themes::shades(),  themes::dots()};
    // Glyphs of mixed widths, the case that goes wrong.
    Theme wide;
    wide.fill = "\xE6\x97\xA5";
    wide.empty = "\xE3\x80\x80";
    wide.partials = {"\xF0\x9F\x98\x80"};
    all.push_back(wide);
    Theme wider;
    wider.fill = "ab";
    wider.empty = "cde";
    wider.partials = {"f", "ghij"};
    all.push_back(wider);

    for (const Theme& t : all) {
        for (int cells : {1, 2, 3, 7, 10, 33, 100})
            for (int step = 0; step <= 200; ++step)
                CHECK_EQ(displayWidth(filled(t, step / 200.0, cells)), cells);
    }
}

TEST(a_glyph_of_no_width_cannot_hang_the_drawing) {
    Theme broken;
    broken.fill = "";
    broken.empty = "";
    broken.partials = {"", "\xCC\x81"}; // Empty, combining mark.
    broken.pulse = "";
    const Theme t = detail::usableTheme(broken);
    CHECK(t.partials.empty());
    CHECK_EQ(displayWidth(filled(t, 0.5, 10)), 10);
    std::string out;
    detail::appendPulse(out, t, 1.0, 10);
    CHECK_EQ(displayWidth(out), 10);
}

TEST(the_bar_fills_what_the_line_leaves) {
    Fixture f;
    const std::string line = f.render("x{bar}y", at(5, 10), 22);
    CHECK_EQ(line, "x[=========>        ]y");
    CHECK_EQ(displayWidth(line), 22);
}

TEST(two_flexible_bars_share_the_room) {
    Fixture f;
    CHECK_EQ(f.render("{bar}|{bar}", at(10, 10), 25), "[==========]|[==========]");
}

TEST(a_count_can_carry_its_unit) {
    Fixture f;
    f.unit = "file";
    CHECK_EQ(f.render("{n:unit}/{total:unit} {remaining:unit}", at(1834, 2000)), "1834 file/2000 file 166 file");
    CHECK_EQ(f.render("{total:unit} {remaining:unit}", at(5, 0)), "? file ? file");
    f.unit = "B";
    f.scale = fmt::Scale::Binary;
    CHECK_EQ(f.render("{n:unit}/{total:unit}", at(3145728, 10485760)), "3.00 MiB/10.0 MiB");
    CHECK_EQ(f.render("{n:unit}", at(512, 0)), "512 B");
    CHECK_EQ(f.render("{n}{unit}", at(3145728, 0)), "3.00MiB"); // Written apart, stays apart.
    CHECK_EQ(f.render("{n:units}", at(1, 0)), "{n:units}");     // A typo shows up, as any other.
}

TEST(no_unit_leaves_no_dangling_space) {
    Fixture f;
    f.unit = "";
    CHECK_EQ(f.render("{n:unit}|{total:unit}", at(5, 0)), "5|?");
    Snapshot s = at(5, 10);
    s.rate = 12.34;
    CHECK_EQ(f.render("{rate}", s), "12.3/s");
    s.rate = 0;
    CHECK_EQ(f.render("{rate}", s), "?/s");
}

TEST(every_theme_is_one_space_from_the_percent) {
    for (const Theme& t : {themes::ascii(), themes::classic(), themes::hashes(), themes::blocks(), themes::line(),
                           themes::shades(), themes::dots()})
        CHECK(t.left.empty() || t.left != " ");
}

TEST(fixed_bars_and_no_limit) {
    Fixture f;
    CHECK_EQ(f.render("{bar:4}", at(4, 4), 100), "[====]");
    CHECK_EQ(displayWidth(f.render("{bar}", at(1, 2))), 22); // No width to fill: 20 + caps.
    f.barWidth = 6;
    CHECK_EQ(f.render("{bar}", at(1, 2), 100), "[===>  ]");
}

TEST(out_of_room_the_bar_goes_and_the_end_is_cut) {
    Fixture f;
    const std::string format = "abc {bar} 10/10 [00:01<00:00]";
    for (int width = 1; width <= 60; ++width)
        CHECK(displayWidth(f.render(format, at(5, 10), width)) <= width);
    CHECK_EQ(f.render(format, at(5, 10), 30), "abc [==> ] 10/10 [00:01<00:00]");
    CHECK_EQ(f.render(format, at(5, 10), 29), "abc  10/10 [00:01<00:00]"); // No room for 4 cells.
    CHECK_EQ(f.render(format, at(5, 10), 10), "abc  10/1.");
    f.useUtf8 = true;
    CHECK_EQ(f.render(format, at(5, 10), 10), "abc  10/1\xE2\x80\xA6");
}

TEST(colors_are_there_only_when_they_can_be_seen) {
    Fixture f;
    f.color = colors::kGreen;
    CHECK_EQ(f.render("{bar:2}", at(2, 2)), "[\x1b[32m==\x1b[0m]");
    CHECK_EQ(f.render("{red}x", at(0, 1)), "\x1b[31mx\x1b[0m");
    f.color = Color::index(200);
    CHECK_EQ(f.render("{bar:1}", at(1, 1)), "[\x1b[38;5;200m=\x1b[0m]");
    f.color = Color::rgb(1, 2, 3);
    CHECK_EQ(f.render("{bar:1}", at(1, 1)), "[\x1b[38;2;1;2;3m=\x1b[0m]");
    f.useColor = false;
    f.label = "\x1b[35mname\x1b[0m";
    CHECK_EQ(f.render("{bold}{desc}{bar:1}", at(1, 1)), "name: [=]");
}

TEST(a_cut_line_does_not_leak_color) {
    Fixture f;
    const std::string line = f.render("{red}abcdefghij", at(0, 1), 5);
    CHECK_EQ(displayWidth(line), 5);
    CHECK(line.compare(line.size() - 4, 4, "\x1b[0m") == 0);
}

TEST(spinner_frames_by_time_and_end_states) {
    Fixture f;
    f.spinner = {{"a", "b", "c"}, 100, "D", "F"};
    Snapshot s = at(0, 0);
    CHECK_EQ(f.render("{spinner}", s), "a");
    s.elapsed = 0.1;
    CHECK_EQ(f.render("{spinner}", s), "b");
    s.elapsed = 0.25;
    CHECK_EQ(f.render("{spinner}", s), "c");
    s.elapsed = 0.3;
    CHECK_EQ(f.render("{spinner}", s), "a");
    s.state = State::Done;
    CHECK_EQ(f.render("{spinner}", s), "D");
    s.state = State::Failed;
    CHECK_EQ(f.render("{spinner}", s), "F");
    f.spinner = {{}, 0, "", ""};
    s.state = State::Running;
    CHECK_EQ(f.render("{spinner}", s), ""); // Nothing to show, nothing to divide by.
}

TEST(the_pulse_bounces_and_keeps_the_width) {
    Fixture f;
    Snapshot s = at(0, 0);
    std::string first;
    for (int step = 0; step < 100; ++step) {
        s.elapsed = step / 12.0;
        const std::string line = f.render("{bar:10}", s);
        CHECK_EQ(displayWidth(line), 12);
        if (step == 0) {
            first = line;
            CHECK_EQ(line, "[<=>       ]");
        }
        if (step == 7)
            CHECK_EQ(line, "[       <=>]");
        if (step == 14)
            CHECK_EQ(line, first);
    }
}

int main(int argc, char** argv) { return runAll(argc, argv); }
