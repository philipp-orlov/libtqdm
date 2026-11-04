// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// A small terminal: feed it what a Display wrote and ask what a person would
// see. Asserting on the screen and not on the bytes means a test says what must
// be true, and stays true when the escape sequences get smarter.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "tqdm/unicode.hpp"

namespace check {

class Screen {
public:
    Screen(int columns, int rows) : columns_(columns), rows_(rows) {
        cells_.assign(static_cast<size_t>(rows_), blankRow());
    }

    void feed(std::string_view bytes) {
        using namespace tqdm::unicode;
        for (size_t pos = 0; pos < bytes.size();) {
            const char c = bytes[pos];
            if (c == '\x1b') {
                const size_t length = escapeLength(bytes, pos);
                applyEscape(bytes.substr(pos, length));
                pos += length;
            } else if (c == '\r') {
                column_ = 0;
                ++pos;
            } else if (c == '\n') {
                lineFeed();
                ++pos;
            } else {
                const size_t begin = pos;
                const char32_t cp = decode(bytes, pos);
                put(bytes.substr(begin, pos - begin), codepointWidth(cp));
            }
        }
    }

    // Everything visible, oldest first: what scrolled off, then the screen, with
    // trailing blanks and blank rows at the end gone.
    std::vector<std::string> lines() const {
        std::vector<std::string> out = scrollback_;
        for (const auto& row : cells_)
            out.push_back(textOf(row));
        while (!out.empty() && out.back().empty())
            out.pop_back();
        return out;
    }

    std::string all() const {
        std::string out;
        for (const std::string& line : lines()) {
            out += line;
            out.push_back('\n');
        }
        return out;
    }

    bool cursorHidden() const { return cursorHidden_; }
    int rowsUsed() const { return static_cast<int>(lines().size()); }
    int cursorRow() const { return row_; }
    int cursorColumn() const { return column_; }
    int wrappedLines() const { return wrapped_; }
    bool autowrapOn() const { return autowrapChanges_ == 0; }

private:
    std::vector<std::string> blankRow() const { return std::vector<std::string>(static_cast<size_t>(columns_), " "); }

    static std::string textOf(const std::vector<std::string>& row) {
        std::string out;
        for (const std::string& cell : row)
            out += cell;
        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }

    void lineFeed() {
        if (row_ + 1 < rows_) {
            ++row_;
        } else {
            scrollback_.push_back(textOf(cells_.front()));
            cells_.erase(cells_.begin());
            cells_.push_back(blankRow());
        }
    }

    void put(std::string_view glyph, int width) {
        if (width == 0) {
            if (column_ > 0) {
                cells_[static_cast<size_t>(row_)][static_cast<size_t>(column_ - 1)] += glyph;
            }
            return;
        }
        if (column_ + width > columns_) {
            column_ = 0;
            ++wrapped_;
            lineFeed();
        }
        auto& row = cells_[static_cast<size_t>(row_)];
        row[static_cast<size_t>(column_)] = std::string(glyph);
        for (int i = 1; i < width; ++i)
            row[static_cast<size_t>(column_ + i)].clear();
        column_ += width;
    }

    void applyEscape(std::string_view escape) {
        if (escape.size() < 3 || escape[1] != '[')
            return;
        const char command = escape.back();
        const std::string_view parameters = escape.substr(2, escape.size() - 3);
        if (parameters == "?25") {
            cursorHidden_ = command == 'l';
            return;
        }
        // Autowrap off (?7l) is on purpose not honored: the tests want to know
        // about any line that would have wrapped.
        if (parameters == "?7") {
            autowrapChanges_ += command == 'l' ? 1 : -1;
            return;
        }
        int number = 0;
        for (const char c : parameters)
            if (std::isdigit(static_cast<unsigned char>(c)) != 0)
                number = number * 10 + (c - '0');
        if (command == 'A') {
            row_ = std::max(0, row_ - std::max(number, 1));
        } else if (command == 'K') {
            auto& row = cells_[static_cast<size_t>(row_)];
            std::fill(row.begin() + column_, row.end(), " ");
        } else if (command == 'J') {
            auto& row = cells_[static_cast<size_t>(row_)];
            std::fill(row.begin() + column_, row.end(), " ");
            for (int r = row_ + 1; r < rows_; ++r)
                cells_[static_cast<size_t>(r)] = blankRow();
        }
    }

    int columns_;
    int rows_;
    int row_ = 0;
    int column_ = 0;
    int wrapped_ = 0;
    int autowrapChanges_ = 0;
    bool cursorHidden_ = false;
    std::vector<std::vector<std::string>> cells_;
    std::vector<std::string> scrollback_;
};

} // namespace check
