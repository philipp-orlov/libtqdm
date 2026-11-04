// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Numbers and durations as text. Locale-independent and free of iostreams and
// printf: a decimal comma set by the host program does not reach the bar, and
// nothing here is slower than it has to be. NaN and infinity print as "?".

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace tqdm::fmt {

// How counts and rates shrink: none (1234567), si (1.23M) or binary (1.18Mi).
enum class Scale { None, Si, Binary };

inline void appendUint(std::string& out, uint64_t value) {
    char digits[20];
    int size = 0;
    do {
        digits[size++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (size > 0)
        out.push_back(digits[--size]);
}

namespace detail {

inline void appendPadded(std::string& out, uint64_t value, int width) {
    std::string digits;
    appendUint(digits, value);
    for (int i = static_cast<int>(digits.size()); i < width; ++i)
        out.push_back('0');
    out += digits;
}

} // namespace detail

// Rounded to `decimals` places. Beyond 1e15 the digits stop meaning much, so it
// is printed by appendValue(), in scientific notation.
inline void appendValue(std::string& out, double value);

inline void appendFixed(std::string& out, double value, int decimals) {
    if (!std::isfinite(value)) {
        out.push_back('?');
        return;
    }
    double scale = 1;
    for (int i = 0; i < decimals; ++i)
        scale *= 10;
    const double magnitude = std::fabs(value);
    if (magnitude * scale >= 1e15) {
        appendValue(out, value);
        return;
    }
    const auto rounded = static_cast<uint64_t>(magnitude * scale + 0.5);
    if (value < 0 && rounded != 0)
        out.push_back('-');
    const auto factor = static_cast<uint64_t>(scale);
    appendUint(out, rounded / factor);
    if (decimals > 0) {
        out.push_back('.');
        detail::appendPadded(out, rounded % factor, decimals);
    }
}

// Four significant digits, no trailing zeros, for values in a postfix: 0.2513,
// 12.5, 1234, 1.235e+06.
inline void appendValue(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out.push_back('?');
        return;
    }
    const double magnitude = std::fabs(value);
    if (magnitude == 0) {
        out.push_back('0');
        return;
    }
    if (magnitude >= 1e6 || magnitude < 1e-3) {
        int exponent = static_cast<int>(std::floor(std::log10(magnitude)));
        double mantissa = magnitude / std::pow(10.0, exponent);
        if (mantissa + 0.0005 >= 10) {
            mantissa /= 10;
            ++exponent;
        }
        if (value < 0)
            out.push_back('-');
        appendFixed(out, mantissa, 3);
        out.push_back('e');
        out.push_back(exponent < 0 ? '-' : '+');
        detail::appendPadded(out, static_cast<uint64_t>(exponent < 0 ? -exponent : exponent), 2);
        return;
    }
    const int decimals = magnitude >= 1000   ? 0
                         : magnitude >= 100  ? 1
                         : magnitude >= 10   ? 2
                         : magnitude >= 1    ? 3
                         : magnitude >= 0.1  ? 4
                         : magnitude >= 0.01 ? 5
                                             : 6;
    std::string text;
    appendFixed(text, value, decimals);
    if (decimals > 0) {
        while (text.back() == '0')
            text.pop_back();
        if (text.back() == '.')
            text.pop_back();
    }
    out += text;
}

// Three significant digits and a prefix: 512, 1.23k, 45.6Mi. Values under the
// first prefix and `whole` print as integers. `spaced` puts a space before the
// prefix, for a unit that follows: "45.6 Mi" + "B".
inline void appendScaled(std::string& out, double value, Scale scale, bool whole = false, bool spaced = false) {
    if (!std::isfinite(value) || value < 0) {
        out.push_back('?');
        return;
    }
    static constexpr std::string_view kSi[] = {"", "k", "M", "G", "T", "P", "E"};
    static constexpr std::string_view kBinary[] = {"", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei"};
    const double base = scale == Scale::Binary ? 1024.0 : 1000.0;
    const auto& prefixes = scale == Scale::Binary ? kBinary : kSi;
    int index = 0;
    while (value >= base && index < 6) {
        value /= base;
        ++index;
    }
    int decimals = value >= 100 ? 0 : value >= 10 ? 1 : 2;
    if (index == 0 && whole)
        decimals = 0;
    // 999.6 rounds up to the next prefix, not to a four-digit number.
    double step = 0.5;
    for (int i = 0; i < decimals; ++i)
        step /= 10;
    if (value + step >= base && index < 6) {
        value /= base;
        ++index;
        decimals = 2;
    }
    appendFixed(out, value, decimals);
    if (spaced)
        out.push_back(' ');
    out += prefixes[index];
}

inline void appendCount(std::string& out, uint64_t value, Scale scale) {
    if (scale == Scale::None)
        appendUint(out, value);
    else
        appendScaled(out, static_cast<double>(value), scale, true);
}

// A count with its unit: 1834 file, 215 MiB. No space without a unit.
inline void appendAmount(std::string& out, uint64_t value, std::string_view unit, Scale scale) {
    const bool spaced = !unit.empty();
    if (scale != Scale::None) {
        appendScaled(out, static_cast<double>(value), scale, true, spaced);
    } else {
        appendUint(out, value);
        if (spaced)
            out.push_back(' ');
    }
    out += unit;
}

// 12.3 it/s; 1.23 Mit/s once plain digits get long; 2.50 s/it when slower than one
// a second; ? it/s when not known yet. No space without a unit: 12.3/s.
inline void appendRate(std::string& out, double rate, std::string_view unit, Scale scale) {
    const bool spaced = !unit.empty();
    const auto appendPlain = [&](const char* text) {
        out += text;
        if (spaced)
            out.push_back(' ');
    };
    if (!std::isfinite(rate) || rate <= 0) {
        appendPlain("?");
    } else if (scale != Scale::None) {
        appendScaled(out, rate, scale, false, spaced);
    } else if (rate >= 10000) {
        appendScaled(out, rate, Scale::Si, false, spaced);
    } else if (rate >= 1000) {
        appendFixed(out, rate, 0);
        appendPlain("");
    } else if (rate >= 1) {
        appendScaled(out, rate, Scale::None, false, spaced);
    } else {
        const double seconds = 1 / rate;
        if (seconds >= 1e6) {
            appendPlain("?");
        } else {
            appendScaled(out, seconds, Scale::Si, false, true);
            out.append("s/");
            out += unit;
            return;
        }
    }
    out += unit;
    out.append("/s");
}

// MM:SS, H:MM:SS, Nd HH:MM:SS. "?" for anything negative, not finite or absurd
// (100 days and up): no overflow, whatever an estimate comes to.
inline void appendDuration(std::string& out, double seconds, bool roundUp = false) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds >= 8640000) {
        out.push_back('?');
        return;
    }
    auto total = static_cast<uint64_t>(roundUp ? std::ceil(seconds) : std::floor(seconds));
    const uint64_t days = total / 86400;
    total %= 86400;
    const uint64_t hours = total / 3600;
    total %= 3600;
    if (days > 0) {
        appendUint(out, days);
        out.append("d ");
        detail::appendPadded(out, hours, 2);
        out.push_back(':');
    } else if (hours > 0) {
        appendUint(out, hours);
        out.push_back(':');
    }
    detail::appendPadded(out, total / 60, 2);
    out.push_back(':');
    detail::appendPadded(out, total % 60, 2);
}

} // namespace tqdm::fmt
