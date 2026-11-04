// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// How many terminal cells a UTF-8 string takes. Independent of the process
// locale, never throws, and any byte sequence is valid input: a broken one
// counts as one replacement character per bad byte.
//
// Escape sequences take no cells. Clusters are handled as terminals draw them:
// combining marks, variation selector 16 (text glyph becomes a two-cell emoji),
// zero-width joiner sequences, skin tones and flag pairs.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>

#include "tqdm/unicode_tables.hpp"

namespace tqdm::unicode {

inline constexpr char32_t kReplacement = 0xFFFD;

// The code point at `pos`, moving `pos` past it. Rejects overlong forms,
// surrogates and anything above U+10FFFF.
inline char32_t decode(std::string_view text, size_t& pos) noexcept {
    const auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(pos);
    if (lead < 0x80) {
        ++pos;
        return lead;
    }
    size_t extra = 0;
    char32_t cp = 0;
    char32_t smallest = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        cp = lead & 0x1Fu;
        smallest = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        cp = lead & 0x0Fu;
        smallest = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        cp = lead & 0x07u;
        smallest = 0x10000;
    } else {
        ++pos;
        return kReplacement;
    }
    if (text.size() - pos <= extra) {
        ++pos;
        return kReplacement;
    }
    for (size_t i = 1; i <= extra; ++i) {
        const unsigned char next = byte(pos + i);
        if ((next & 0xC0u) != 0x80u) {
            ++pos;
            return kReplacement;
        }
        cp = (cp << 6) | (next & 0x3Fu);
    }
    if (cp < smallest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++pos;
        return kReplacement;
    }
    pos += extra + 1;
    return cp;
}

// The length of the escape sequence that starts at text[pos] == ESC: CSI, OSC
// (hyperlinks, titles) or a two-byte one. An unterminated one runs to the end.
inline size_t escapeLength(std::string_view text, size_t pos) noexcept {
    const size_t size = text.size();
    size_t i = pos + 1;
    if (i >= size)
        return 1;
    if (text[i] == '[') {
        for (++i; i < size; ++i) {
            const auto c = static_cast<unsigned char>(text[i]);
            if (c >= 0x40 && c <= 0x7E)
                return i - pos + 1;
        }
        return size - pos;
    }
    if (text[i] == ']') {
        for (++i; i < size; ++i) {
            if (text[i] == '\a')
                return i - pos + 1;
            if (text[i] == '\x1b' && i + 1 < size && text[i + 1] == '\\')
                return i - pos + 2;
        }
        return size - pos;
    }
    return 2;
}

namespace detail {

template <size_t N>
constexpr bool inTable(const tables::Range (&table)[N], char32_t cp) noexcept {
    size_t low = 0;
    size_t high = N;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (cp > table[middle].last)
            low = middle + 1;
        else if (cp < table[middle].first)
            high = middle;
        else
            return true;
    }
    return false;
}

} // namespace detail

// The cells of one code point on its own: 0, 1 or 2. Controls take 0.
inline int codepointWidth(char32_t cp) noexcept {
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0))
        return 0;
    if (cp < 0x300)
        return 1; // The common case, before any table.
    if (detail::inTable(tables::kZeroWidth, cp))
        return 0;
    return detail::inTable(tables::kWide, cp) ? 2 : 1;
}

// Adds code points one at a time; add() returns how many cells each one adds.
class WidthCounter {
public:
    int add(char32_t cp) noexcept {
        if (cp == 0x200D) { // Zero-width joiner: the next one joins this cluster.
            joined_ = true;
            return 0;
        }
        if (joined_) {
            joined_ = false;
            return 0;
        }
        if (cp == 0xFE0F) { // Variation selector 16: emoji presentation.
            if (last_ == 1) {
                last_ = 2;
                return 1;
            }
            return 0;
        }
        if (cp >= 0x1F3FB && cp <= 0x1F3FF && last_ == 2) // Skin tone on an emoji.
            return 0;
        if (cp >= 0x1F1E6 && cp <= 0x1F1FF) { // Two regional indicators are one flag.
            flagOpen_ = !flagOpen_;
            last_ = 2;
            return flagOpen_ ? 2 : 0;
        }
        flagOpen_ = false;
        const int width = codepointWidth(cp);
        if (width > 0)
            last_ = width;
        return width;
    }

private:
    int last_ = 0;
    bool joined_ = false;
    bool flagOpen_ = false;
};

inline int displayWidth(std::string_view text) noexcept {
    int width = 0;
    WidthCounter counter;
    for (size_t pos = 0; pos < text.size();) {
        if (text[pos] == '\x1b') {
            pos += escapeLength(text, pos);
            continue;
        }
        width += counter.add(decode(text, pos));
    }
    return width;
}

// At most `maxWidth` cells, whole clusters only, ending in `ellipsis` when
// something was cut. Escape sequences are kept, and a cut string that had any
// ends with a reset so a color cannot leak out of it.
inline std::string truncate(std::string_view text, int maxWidth, std::string_view ellipsis = {}) {
    if (displayWidth(text) <= maxWidth)
        return std::string(text);
    const int ellipsisWidth = displayWidth(ellipsis);
    const bool fitsEllipsis = ellipsisWidth <= maxWidth;
    const int limit = fitsEllipsis ? maxWidth - ellipsisWidth : maxWidth;
    std::string out;
    out.reserve(text.size());
    bool hadEscape = false;
    int width = 0;
    WidthCounter counter;
    for (size_t pos = 0; pos < text.size();) {
        if (text[pos] == '\x1b') {
            const size_t length = escapeLength(text, pos);
            out.append(text.substr(pos, length));
            hadEscape = true;
            pos += length;
            continue;
        }
        const size_t begin = pos;
        const int cells = counter.add(decode(text, pos));
        if (width + cells > limit)
            break;
        width += cells;
        out.append(text.substr(begin, pos - begin));
    }
    if (fitsEllipsis)
        out.append(ellipsis);
    if (hadEscape)
        out.append("\x1b[0m");
    return out;
}

inline std::string stripEscapes(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t pos = 0; pos < text.size();)
        if (text[pos] == '\x1b')
            pos += escapeLength(text, pos);
        else
            out.push_back(text[pos++]);
    return out;
}

// Makes text safe to put on one line: every control character but those inside
// an escape sequence becomes a space, so a stray newline in a description
// cannot break the region.
inline std::string sanitizeLine(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t pos = 0; pos < text.size();) {
        const auto c = static_cast<unsigned char>(text[pos]);
        if (c == 0x1B) {
            const size_t length = escapeLength(text, pos);
            out.append(text.substr(pos, length));
            pos += length;
        } else {
            out.push_back(c < 0x20 || c == 0x7F ? ' ' : text[pos]);
            ++pos;
        }
    }
    return out;
}

} // namespace tqdm::unicode
