// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// UTF-8, widths, truncation and number formatting: the pure bottom layers.

#include <clocale>
#include <cmath>
#include <limits>
#include <string>

#include "check.hpp"
#include "tqdm/format.hpp"
#include "tqdm/unicode.hpp"

using namespace tqdm;
using unicode::displayWidth;

namespace {

template <typename F>
std::string textOf(F&& f) {
    std::string out;
    f(out);
    return out;
}

std::string duration(double s, bool up = false) {
    return textOf([&](std::string& o) { fmt::appendDuration(o, s, up); });
}
std::string fixed(double v, int d) {
    return textOf([&](std::string& o) { fmt::appendFixed(o, v, d); });
}
std::string value(double v) {
    return textOf([&](std::string& o) { fmt::appendValue(o, v); });
}
std::string scaled(double v, fmt::Scale s) {
    return textOf([&](std::string& o) { fmt::appendScaled(o, v, s); });
}
std::string count(uint64_t v, fmt::Scale s) {
    return textOf([&](std::string& o) { fmt::appendCount(o, v, s); });
}
std::string rate(double r, const char* unit = "it", fmt::Scale s = fmt::Scale::None) {
    return textOf([&](std::string& o) { fmt::appendRate(o, r, unit, s); });
}

} // namespace

TEST(decode_accepts_every_length) {
    const std::string text = "a\xC3\xA9\xE2\x96\x88\xF0\x9F\x98\x80";
    size_t pos = 0;
    CHECK(unicode::decode(text, pos) == U'a');
    CHECK(unicode::decode(text, pos) == U'é');
    CHECK(unicode::decode(text, pos) == U'█');
    CHECK(unicode::decode(text, pos) == U'\U0001F600');
    CHECK_EQ(pos, text.size());
}

TEST(decode_rejects_what_utf8_forbids) {
    for (const char* bad :
         {"\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xFF", "\x80", "\xE2\x96", "\xF0\x9F\x98"}) {
        const std::string text = bad;
        size_t pos = 0;
        CHECK(unicode::decode(text, pos) == unicode::kReplacement);
        CHECK_EQ(pos, size_t{1});
    }
}

TEST(any_bytes_at_all_are_measured_without_incident) {
    std::string text;
    for (int round = 0; round < 3; ++round)
        for (int c = 0; c < 256; ++c)
            text.push_back(static_cast<char>(c));
    for (size_t cut = 0; cut < text.size(); cut += 7) {
        (void)displayWidth(std::string_view(text).substr(cut));
        const std::string shorter = unicode::truncate(std::string_view(text).substr(cut), 5, "~");
        CHECK(displayWidth(shorter) <= 5);
    }
}

TEST(widths) {
    CHECK_EQ(displayWidth(""), 0);
    CHECK_EQ(displayWidth("abc"), 3);
    CHECK_EQ(displayWidth("\xE2\x96\x88"), 1);                                 // Block.
    CHECK_EQ(displayWidth("\xE6\x97\xA5\xE6\x9C\xAC"), 4);                     // CJK.
    CHECK_EQ(displayWidth("e\xCC\x81"), 1);                                    // Combining acute.
    CHECK_EQ(displayWidth("\xF0\x9F\x98\x80"), 2);                             // Emoji.
    CHECK_EQ(displayWidth("\xE2\x9D\xA4\xEF\xB8\x8F"), 2);                     // Heart + VS16.
    CHECK_EQ(displayWidth("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"), 2);             // Thumb + skin tone.
    CHECK_EQ(displayWidth("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"), 2); // ZWJ sequence.
    CHECK_EQ(displayWidth("\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA"), 2);             // A flag.
    CHECK_EQ(displayWidth("\xF0\x9F\x87\xA9\xF0\x9F\x87\xAA\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7"), 4);
    CHECK_EQ(displayWidth("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8"), 2); // Hangul jamo.
}

TEST(escape_sequences_take_no_cells) {
    CHECK_EQ(displayWidth("\x1b[31mred\x1b[0m"), 3);
    CHECK_EQ(displayWidth("\x1b[38;2;1;2;3mx"), 1);
    CHECK_EQ(displayWidth("\x1b]8;;https://a.b\x1b\\link\x1b]8;;\x1b\\"), 4); // OSC 8.
    CHECK_EQ(displayWidth("\x1b]0;title\ax"), 1);
    CHECK_EQ(displayWidth("\x1b[31"), 0); // Unterminated.
    CHECK_EQ(displayWidth("\x1b"), 0);
}

TEST(truncate_counts_cells_and_keeps_clusters_whole) {
    CHECK_EQ(unicode::truncate("hello", 5), "hello");
    CHECK_EQ(unicode::truncate("hello!", 5), "hello");
    CHECK_EQ(unicode::truncate("hello!", 5, "~"), "hell~");
    CHECK_EQ(unicode::truncate("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", 3), "\xE6\x97\xA5");
    CHECK_EQ(unicode::truncate("abc", 0, "~~"), "");
    const std::string colored = unicode::truncate("\x1b[31mabcdef\x1b[0m", 3);
    CHECK_EQ(displayWidth(colored), 3);
    CHECK(colored.size() >= 4 && colored.compare(colored.size() - 4, 4, "\x1b[0m") == 0);
}

TEST(strip_and_sanitize) {
    CHECK_EQ(unicode::stripEscapes("\x1b[1ma\x1b[0mb\x1b]0;t\a"), "ab");
    CHECK_EQ(unicode::sanitizeLine("a\nb\tc\rd\x7f"), "a b c d ");
    CHECK_EQ(unicode::sanitizeLine("\x1b[31mx\x1b[0m"), "\x1b[31mx\x1b[0m");
    CHECK_EQ(unicode::sanitizeLine("\xE2\x96\x88"), "\xE2\x96\x88");
}

TEST(durations) {
    CHECK_EQ(duration(0), "00:00");
    CHECK_EQ(duration(59.9), "00:59");
    CHECK_EQ(duration(59.1, true), "01:00");
    CHECK_EQ(duration(3599), "59:59");
    CHECK_EQ(duration(3600), "1:00:00");
    CHECK_EQ(duration(90061), "1d 01:01:01");
    CHECK_EQ(duration(-1), "?");
    CHECK_EQ(duration(1e300), "?");
    CHECK_EQ(duration(std::numeric_limits<double>::infinity()), "?");
    CHECK_EQ(duration(std::nan("")), "?");
}

TEST(fixed_point) {
    CHECK_EQ(fixed(0, 2), "0.00");
    CHECK_EQ(fixed(1.005, 0), "1");
    CHECK_EQ(fixed(2.5, 1), "2.5");
    CHECK_EQ(fixed(9.996, 2), "10.00");
    CHECK_EQ(fixed(0.05, 2), "0.05");
    CHECK_EQ(fixed(-1.5, 1), "-1.5");
    CHECK_EQ(fixed(-0.001, 1), "0.0");
    CHECK_EQ(fixed(std::nan(""), 1), "?");
    CHECK_EQ(fixed(1e30, 1), "1.000e+30");
}

TEST(values_in_a_postfix) {
    CHECK_EQ(value(0), "0");
    CHECK_EQ(value(0.25), "0.25");
    CHECK_EQ(value(12.5), "12.5");
    CHECK_EQ(value(1234), "1234");
    CHECK_EQ(value(-3.5), "-3.5");
    CHECK_EQ(value(0.012345), "0.01235");
    CHECK_EQ(value(1234567), "1.235e+06");
    CHECK_EQ(value(0.00012), "1.200e-04");
    CHECK_EQ(value(9999999), "1.000e+07");
    CHECK_EQ(value(std::numeric_limits<double>::quiet_NaN()), "?");
}

TEST(scaling) {
    CHECK_EQ(scaled(512, fmt::Scale::Si), "512");
    CHECK_EQ(scaled(1234, fmt::Scale::Si), "1.23k");
    CHECK_EQ(scaled(12340, fmt::Scale::Si), "12.3k");
    CHECK_EQ(scaled(123400, fmt::Scale::Si), "123k");
    CHECK_EQ(scaled(999600, fmt::Scale::Si), "1.00M");
    CHECK_EQ(scaled(1536, fmt::Scale::Binary), "1.50Ki");
    CHECK_EQ(scaled(1048576, fmt::Scale::Binary), "1.00Mi");
    CHECK_EQ(scaled(1023.9, fmt::Scale::Binary), "1.00Ki");
    CHECK_EQ(scaled(1e30, fmt::Scale::Si), "1000000000000E");
    CHECK_EQ(count(999, fmt::Scale::Si), "999");
    CHECK_EQ(count(1000, fmt::Scale::Si), "1.00k");
    CHECK_EQ(count(18446744073709551615u, fmt::Scale::None), "18446744073709551615");
    CHECK_EQ(count(18446744073709551615u, fmt::Scale::Binary), "16.0Ei");
}

TEST(rates) {
    CHECK_EQ(rate(0), "? it/s");
    CHECK_EQ(rate(std::nan("")), "? it/s");
    CHECK_EQ(rate(12.34), "12.3 it/s");
    CHECK_EQ(rate(1.0), "1.00 it/s");
    CHECK_EQ(rate(2500), "2500 it/s");
    CHECK_EQ(rate(1234567), "1.23 Mit/s");
    CHECK_EQ(rate(0.4), "2.50 s/it");
    CHECK_EQ(rate(0.0000001), "? it/s");
    CHECK_EQ(rate(4404019, "B", fmt::Scale::Binary), "4.20 MiB/s");
    CHECK_EQ(rate(0.5, "B", fmt::Scale::Binary), "0.50 B/s");
    CHECK_EQ(rate(0.2, "file"), "5.00 s/file");
}

TEST(no_decimal_comma_whatever_the_locale) {
    // Whichever comma locale this machine has; if none, the check is still true.
    for (const char* name : {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "ru_RU.UTF-8"})
        if (std::setlocale(LC_ALL, name) != nullptr)
            break;
    CHECK_EQ(fixed(1.5, 1), "1.5");
    CHECK_EQ(rate(12.34), "12.3 it/s");
    std::setlocale(LC_ALL, "C");
}

int main(int argc, char** argv) { return runAll(argc, argv); }
