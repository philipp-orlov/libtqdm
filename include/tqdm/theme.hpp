// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// What a bar and a spinner look like: colors, glyphs. Data only.
//
// This and every other header stays pure ASCII, with UTF-8 glyphs spelled as
// byte escapes, so nothing depends on the compiler's source character set
// (MSVC without /utf-8 would otherwise garble them).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "tqdm/format.hpp"

namespace tqdm {

// An SGR foreground color; `none` adds nothing to the line.
class Color {
public:
    constexpr Color() = default;

    // 30-37, 90-97: the terminal's own palette.
    static constexpr Color ansi(uint8_t code) { return Color(Kind::Ansi, code, 0, 0); }
    static constexpr Color index(uint8_t value) { return Color(Kind::Index, value, 0, 0); }
    static constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) { return Color(Kind::Rgb, r, g, b); }

    constexpr explicit operator bool() const { return kind_ != Kind::None; }

    void appendTo(std::string& out) const {
        if (kind_ == Kind::None)
            return;
        out.append("\x1b[");
        if (kind_ == Kind::Ansi) {
            fmt::appendUint(out, a_);
        } else if (kind_ == Kind::Index) {
            out.append("38;5;");
            fmt::appendUint(out, a_);
        } else {
            out.append("38;2;");
            fmt::appendUint(out, a_);
            out.push_back(';');
            fmt::appendUint(out, b_);
            out.push_back(';');
            fmt::appendUint(out, c_);
        }
        out.push_back('m');
    }

private:
    enum class Kind : uint8_t { None, Ansi, Index, Rgb };

    constexpr Color(Kind kind, uint8_t a, uint8_t b, uint8_t c) : kind_(kind), a_(a), b_(b), c_(c) {}

    Kind kind_ = Kind::None;
    uint8_t a_ = 0;
    uint8_t b_ = 0;
    uint8_t c_ = 0;
};

namespace colors {
inline constexpr Color kNone{};
inline constexpr Color kRed = Color::ansi(31);
inline constexpr Color kGreen = Color::ansi(32);
inline constexpr Color kYellow = Color::ansi(33);
inline constexpr Color kBlue = Color::ansi(34);
inline constexpr Color kMagenta = Color::ansi(35);
inline constexpr Color kCyan = Color::ansi(36);
inline constexpr Color kWhite = Color::ansi(37);
inline constexpr Color kGray = Color::ansi(90);
} // namespace colors

// The glyphs of a bar. `partials` split one cell, from least to most filled;
// with a single one it is a lead that always marks the front ([===>   ]). Any
// glyph may be several cells wide; the bar still comes out at its exact width.
struct Theme {
    std::string left;
    std::string right;
    std::string fill;
    std::string empty = " ";
    std::vector<std::string> partials;
    std::string pulse; // The block that bounces while the total is not known.
};

namespace themes {

// |#####3    |
inline Theme ascii() { return {"|", "|", "#", " ", {"1", "2", "3", "4", "5", "6", "7", "8", "9"}, "###"}; }

// [=====>    ]
inline Theme classic() { return {"[", "]", "=", " ", {">"}, "<=>"}; }

// [#####     ]
inline Theme hashes() { return {"[", "]", "#", " ", {}, "###"}; }

// Full blocks with eighths: the default on a UTF-8 terminal.
inline Theme blocks() {
    return {"|",
            "|",
            "\xE2\x96\x88",
            " ",
            {"\xE2\x96\x8F", "\xE2\x96\x8E", "\xE2\x96\x8D", "\xE2\x96\x8C", "\xE2\x96\x8B", "\xE2\x96\x8A",
             "\xE2\x96\x89"},
            "\xE2\x96\x88\xE2\x96\x88\xE2\x96\x88"};
}

// A heavy line over a light one.
inline Theme line() {
    return {"", "", "\xE2\x94\x81", "\xE2\x94\x80", {"\xE2\x95\xB8"}, "\xE2\x94\x81\xE2\x94\x81\xE2\x94\x81"};
}

// Shades fading in over a light background.
inline Theme shades() {
    return {"",
            "",
            "\xE2\x96\x88",
            "\xE2\x96\x91",
            {"\xE2\x96\x92", "\xE2\x96\x93"},
            "\xE2\x96\x93\xE2\x96\x88\xE2\x96\x93"};
}

// Braille dots filling a cell.
inline Theme dots() {
    return {"",
            "",
            "\xE2\xA3\xBF",
            "\xE2\xA3\x80",
            {"\xE2\xA3\x84", "\xE2\xA3\xA4", "\xE2\xA3\xA6", "\xE2\xA3\xB6", "\xE2\xA3\xB7"},
            "\xE2\xA3\xBF\xE2\xA3\xBF\xE2\xA3\xBF"};
}

} // namespace themes

// The frames of a spinner, and what replaces them when work ends.
struct Spinner {
    std::vector<std::string> frames;
    uint32_t intervalMs = 100;
    std::string done;
    std::string failed;
};

namespace spinners {

inline Spinner ascii() { return {{"-", "\\", "|", "/"}, 120, "*", "x"}; }

inline Spinner dots() {
    return {{"\xE2\xA0\x8B", "\xE2\xA0\x99", "\xE2\xA0\xB9", "\xE2\xA0\xB8", "\xE2\xA0\xBC", "\xE2\xA0\xB4",
             "\xE2\xA0\xA6", "\xE2\xA0\xA7", "\xE2\xA0\x87", "\xE2\xA0\x8F"},
            80,
            "\xE2\x9C\x93",
            "\xE2\x9C\x97"};
}

inline Spinner arc() {
    return {{"\xE2\x97\x9C", "\xE2\x97\xA0", "\xE2\x97\x9D", "\xE2\x97\x9E", "\xE2\x97\xA1", "\xE2\x97\x9F"},
            100,
            "\xE2\x9C\x93",
            "\xE2\x9C\x97"};
}

} // namespace spinners

} // namespace tqdm
