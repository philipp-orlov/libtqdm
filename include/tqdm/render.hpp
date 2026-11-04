// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Turns a format string and a Snapshot into one line of text. A pure function:
// no clock, no terminal, no locks, which is why every look a bar can have is a
// plain string comparison in the tests.
//
//   {desc}       "label: ", or nothing without a label
//   {label}      the label alone
//   {percent}    " 42%"; {percent:.1} for " 42.5%"
//   {bar}        fills whatever width the rest of the line leaves; {bar:20} is fixed
//   {n} {total} {remaining}   counts, scaled as the bar is; "?" when not known
//   {n:unit} {total:unit} {remaining:unit}   the same with the unit: "215 MiB"
//   {elapsed} {eta}           durations
//   {rate}       "12.3 it/s"
//   {unit}       "it"
//   {spinner}    the current frame
//   {postfix}    " text", or nothing without one
//   {bold} {dim} {italic} {underline} {reverse} {reset}
//   {red} {green} {yellow} {blue} {magenta} {cyan} {white} {gray}
//   {{ }}        literal braces
//
// Anything else stays in the line as written: a typo in a format shows up on
// screen and does not take the program down.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tqdm/config.hpp"
#include "tqdm/format.hpp"
#include "tqdm/theme.hpp"
#include "tqdm/unicode.hpp"

namespace tqdm {

enum class State { Running, Done, Failed };

// A bar's numbers at one moment.
struct Snapshot {
    uint64_t n = 0;
    uint64_t total = 0;   // 0: not known.
    double elapsed = 0;   // Seconds.
    double rate = 0;      // Units a second, smoothed; average once finished. 0: not known.
    double eta = -1;      // Seconds left. Negative: not known.
    double fraction = -1; // 0 to 1, as drawn. Negative: not known.
    State state = State::Running;
};

namespace detail {

struct Token {
    enum class Kind {
        Literal,
        Desc,
        Label,
        Percent,
        Bar,
        N,
        Total,
        Remaining,
        Elapsed,
        Eta,
        Rate,
        Unit,
        Spinner,
        Postfix,
        Style
    };
    Kind kind = Kind::Literal;
    std::string text; // A literal, or a style's escape sequence.
    int arg = -1;     // {bar:N}, {percent:.N}.
};

inline const char* styleEscape(std::string_view name) noexcept {
    static constexpr std::pair<std::string_view, const char*> kStyles[] = {
        {"reset", "\x1b[0m"},     {"bold", "\x1b[1m"},    {"dim", "\x1b[2m"},      {"italic", "\x1b[3m"},
        {"underline", "\x1b[4m"}, {"reverse", "\x1b[7m"}, {"red", "\x1b[31m"},     {"green", "\x1b[32m"},
        {"yellow", "\x1b[33m"},   {"blue", "\x1b[34m"},   {"magenta", "\x1b[35m"}, {"cyan", "\x1b[36m"},
        {"white", "\x1b[37m"},    {"gray", "\x1b[90m"}};
    for (const auto& style : kStyles)
        if (style.first == name)
            return style.second;
    return nullptr;
}

// Fills `token` from what is between the braces; false if it means nothing.
inline bool parsePlaceholder(std::string_view body, Token& token) {
    using Kind = Token::Kind;
    static constexpr std::pair<std::string_view, Kind> kNames[] = {{"desc", Kind::Desc},
                                                                   {"label", Kind::Label},
                                                                   {"percent", Kind::Percent},
                                                                   {"bar", Kind::Bar},
                                                                   {"n", Kind::N},
                                                                   {"total", Kind::Total},
                                                                   {"remaining", Kind::Remaining},
                                                                   {"elapsed", Kind::Elapsed},
                                                                   {"eta", Kind::Eta},
                                                                   {"rate", Kind::Rate},
                                                                   {"unit", Kind::Unit},
                                                                   {"spinner", Kind::Spinner},
                                                                   {"postfix", Kind::Postfix}};

    if (const char* escape = styleEscape(body)) {
        token.kind = Kind::Style;
        token.text = escape;
        return true;
    }
    const size_t colon = body.find(':');
    const std::string_view name = body.substr(0, colon);
    for (const auto& entry : kNames) {
        if (entry.first != name)
            continue;
        token.kind = entry.second;
        if (colon == std::string_view::npos)
            return true;
        std::string_view spec = body.substr(colon + 1);
        if ((entry.second == Kind::N || entry.second == Kind::Total || entry.second == Kind::Remaining) &&
            spec == "unit") {
            token.arg = 1;
            return true;
        }
        if (entry.second == Kind::Percent && !spec.empty() && spec.front() == '.')
            spec.remove_prefix(1);
        else if (entry.second != Kind::Bar)
            return false;
        if (spec.empty() || spec.size() > 4)
            return false;
        int value = 0;
        for (const char c : spec) {
            if (c < '0' || c > '9')
                return false;
            value = value * 10 + (c - '0');
        }
        token.arg = value;
        return true;
    }
    return false;
}

TQDM_COLD inline std::vector<Token> parseFormat(std::string_view format) {
    std::vector<Token> tokens;
    std::string literal;
    const auto flush = [&] {
        if (!literal.empty()) {
            Token token;
            token.text = std::move(literal);
            tokens.push_back(std::move(token));
            literal.clear();
        }
    };
    for (size_t pos = 0; pos < format.size();) {
        const char c = format[pos];
        if ((c == '{' || c == '}') && pos + 1 < format.size() && format[pos + 1] == c) {
            literal.push_back(c);
            pos += 2;
            continue;
        }
        if (c == '{') {
            const size_t close = format.find('}', pos + 1);
            Token token;
            if (close != std::string_view::npos && parsePlaceholder(format.substr(pos + 1, close - pos - 1), token)) {
                flush();
                tokens.push_back(std::move(token));
                pos = close + 1;
                continue;
            }
        }
        literal.push_back(c);
        ++pos;
    }
    flush();
    return tokens;
}

inline bool uses(const std::vector<Token>& tokens, Token::Kind kind) noexcept {
    return std::any_of(tokens.begin(), tokens.end(), [kind](const Token& t) { return t.kind == kind; });
}

// A theme cannot break the drawing: a glyph that takes no cells would never
// advance it, so those are replaced or dropped here.
TQDM_COLD inline Theme usableTheme(Theme theme) {
    if (unicode::displayWidth(theme.fill) < 1)
        theme.fill = "#";
    if (unicode::displayWidth(theme.empty) < 1)
        theme.empty = " ";
    theme.partials.erase(std::remove_if(theme.partials.begin(), theme.partials.end(),
                                        [](const std::string& s) { return unicode::displayWidth(s) < 1; }),
                         theme.partials.end());
    if (unicode::displayWidth(theme.pulse) < 1)
        theme.pulse = theme.fill;
    return theme;
}

inline void appendCells(std::string& out, const std::string& glyph, int cells) {
    const int width = unicode::displayWidth(glyph);
    int used = 0;
    for (; used + width <= cells; used += width)
        out += glyph;
    out.append(static_cast<size_t>(cells - used), ' ');
}

// The inside of a bar, exactly `cells` wide whatever the glyphs measure.
inline void appendFilled(std::string& out, const Theme& theme, double fraction, int cells) {
    fraction = (std::min)(1.0, (std::max)(0.0, fraction));
    const double exact = fraction * cells;
    const int whole = static_cast<int>(exact);
    const int fillWidth = unicode::displayWidth(theme.fill);
    int used = 0;
    for (; used + fillWidth <= whole; used += fillWidth)
        out += theme.fill;
    if (used < cells && !theme.partials.empty()) {
        const std::string* partial = nullptr;
        if (theme.partials.size() == 1) {
            partial = fraction < 1 ? &theme.partials[0] : nullptr;
        } else {
            const auto slots = theme.partials.size() + 1;
            const auto slot = static_cast<size_t>((exact - whole) * static_cast<double>(slots));
            partial = slot > 0 ? &theme.partials[(std::min)(slot, slots - 1) - 1] : nullptr;
        }
        if (partial != nullptr) {
            const int width = unicode::displayWidth(*partial);
            if (used + width <= cells) {
                out += *partial;
                used += width;
            }
        }
    }
    appendCells(out, theme.empty, cells - used);
}

inline void appendPulse(std::string& out, const Theme& theme, double elapsed, int cells) {
    const int width = unicode::displayWidth(theme.pulse);
    if (width >= cells) {
        appendCells(out, theme.fill, cells);
        return;
    }
    const int span = cells - width;
    const auto step = static_cast<uint64_t>(std::max(0.0, elapsed) * 12);
    const int phase = static_cast<int>(step % static_cast<uint64_t>(2 * span));
    const int position = phase <= span ? phase : 2 * span - phase;
    appendCells(out, theme.empty, position);
    out += theme.pulse;
    appendCells(out, theme.empty, span - position);
}

inline void appendPercent(std::string& out, const Snapshot& snapshot, int decimals) {
    decimals = (std::min)(decimals, 6);
    const int pad = decimals > 0 ? 3 + 1 + decimals : 3;
    std::string text;
    if (snapshot.fraction < 0) {
        text = "?";
    } else {
        double scale = 1;
        for (int i = 0; i < decimals; ++i)
            scale *= 10;
        // Floored, so 100% means done. The nudge undoes binary noise: 29/100
        // must not read 28%.
        double value = std::floor(snapshot.fraction * 100 * scale + 1e-9) / scale;
        if (value >= 100 && snapshot.fraction < 1)
            value = 100 - 1 / scale;
        fmt::appendFixed(text, value, decimals);
    }
    out.append(static_cast<size_t>(std::max(0, pad - static_cast<int>(text.size()))), ' ');
    out += text;
    out.push_back('%');
}

} // namespace detail

// What a line is made of, apart from the numbers.
struct LineSpec {
    const std::vector<detail::Token>* tokens = nullptr;
    const Theme* theme = nullptr;
    const Spinner* spinner = nullptr;
    std::string_view label;
    std::string_view postfix;
    std::string_view unit;
    fmt::Scale scale = fmt::Scale::None;
    Color barColor;
    int barWidth = 0; // Fixed cells for a plain {bar}; 0 fills what is left.
    bool useColor = true;
    bool useUtf8 = true;
};

// `width` is the most cells the line may take, 0 for no limit. Out-of-room order:
// the bar shrinks, then goes, then the end of the line is cut.
TQDM_COLD inline void renderLine(const LineSpec& spec, const Snapshot& snapshot, int width, std::string& out) {
    using Kind = detail::Token::Kind;
    constexpr int kUnboundedBar = 20;
    constexpr int kSmallestBar = 4;

    const auto appendBar = [&](std::string& to, int cells) {
        const Theme& theme = *spec.theme;
        to += theme.left;
        spec.barColor.appendTo(to);
        if (snapshot.fraction < 0)
            detail::appendPulse(to, theme, snapshot.elapsed, cells);
        else
            detail::appendFilled(to, theme, snapshot.fraction, cells);
        if (spec.barColor)
            to.append("\x1b[0m");
        to += theme.right;
    };

    const auto appendCount = [&](uint64_t value, int withUnit) {
        if (withUnit > 0)
            fmt::appendAmount(out, value, spec.unit, spec.scale);
        else
            fmt::appendCount(out, value, spec.scale);
    };
    const auto appendUnknown = [&](int withUnit) {
        out.push_back('?');
        if (withUnit > 0 && !spec.unit.empty()) {
            out.push_back(' ');
            out += spec.unit;
        }
    };

    out.clear();
    size_t flexible[8];
    size_t flexibleCount = 0;
    bool styled = false;
    for (const detail::Token& token : *spec.tokens) {
        switch (token.kind) {
            case Kind::Literal: out += token.text; break;
            case Kind::Style:
                out += token.text;
                styled = true;
                break;
            case Kind::Desc:
                if (!spec.label.empty()) {
                    out += spec.label;
                    out.append(": ");
                }
                break;
            case Kind::Label: out += spec.label; break;
            case Kind::Postfix:
                if (!spec.postfix.empty()) {
                    out.push_back(' ');
                    out += spec.postfix;
                }
                break;
            case Kind::Percent: detail::appendPercent(out, snapshot, (std::max)(token.arg, 0)); break;
            case Kind::Bar: {
                const int fixed = token.arg > 0 ? token.arg : spec.barWidth;
                if (fixed > 0)
                    appendBar(out, fixed);
                else if (flexibleCount < 8)
                    flexible[flexibleCount++] = out.size();
                break;
            }
            case Kind::N: appendCount(snapshot.n, token.arg); break;
            case Kind::Total:
                if (snapshot.total == 0)
                    appendUnknown(token.arg);
                else
                    appendCount(snapshot.total, token.arg);
                break;
            case Kind::Remaining:
                if (snapshot.total == 0)
                    appendUnknown(token.arg);
                else
                    appendCount(snapshot.total > snapshot.n ? snapshot.total - snapshot.n : 0, token.arg);
                break;
            case Kind::Elapsed: fmt::appendDuration(out, snapshot.elapsed); break;
            case Kind::Eta: fmt::appendDuration(out, snapshot.eta, true); break;
            case Kind::Rate: fmt::appendRate(out, snapshot.rate, spec.unit, spec.scale); break;
            case Kind::Unit: out += spec.unit; break;
            case Kind::Spinner: {
                const Spinner& spinner = *spec.spinner;
                if (snapshot.state == State::Done && !spinner.done.empty()) {
                    out += spinner.done;
                } else if (snapshot.state == State::Failed && !spinner.failed.empty()) {
                    out += spinner.failed;
                } else if (!spinner.frames.empty()) {
                    const auto ms = static_cast<uint64_t>(std::max(0.0, snapshot.elapsed) * 1000);
                    const uint64_t interval = (std::max<uint32_t>)(spinner.intervalMs, 1);
                    out += spinner.frames[(ms / interval) % spinner.frames.size()];
                }
                break;
            }
        }
    }

    if (flexibleCount > 0) {
        const int caps = unicode::displayWidth(spec.theme->left) + unicode::displayWidth(spec.theme->right);
        const int count = static_cast<int>(flexibleCount);
        const int cells = width > 0 ? (width - unicode::displayWidth(out) - caps * count) / count : kUnboundedBar;
        if (cells >= kSmallestBar) {
            std::string bar;
            appendBar(bar, cells);
            for (size_t i = flexibleCount; i-- > 0;)
                out.insert(flexible[i], bar);
        }
    }
    if (styled)
        out.append("\x1b[0m");
    if (width > 0 && unicode::displayWidth(out) > width)
        out = unicode::truncate(out, width, spec.useUtf8 ? "\xE2\x80\xA6" : ".");
    if (!spec.useColor)
        out = unicode::stripEscapes(out);
}

} // namespace tqdm
