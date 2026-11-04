// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Everything a Bar can be told at construction, as a value:
//
//   tqdm::Bar bar(tqdm::Options().total(1000).desc("Loading").unit("file"));
//
// Options is plain data, so a program can keep a house style in one and copy it.

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "tqdm/format.hpp"
#include "tqdm/theme.hpp"

namespace tqdm {

class Display;

class Options {
public:
    struct Values {
        uint64_t total = 0;
        uint64_t initial = 0;
        std::string desc;
        std::string postfix;
        std::string unit = "it";
        fmt::Scale scale = fmt::Scale::None;
        std::string format;
        std::optional<Theme> theme;
        std::optional<Spinner> spinner;
        Color color;
        std::optional<Color> doneColor;
        Color failedColor = colors::kRed;
        int barWidth = 0;
        int maxWidth = 0;
        std::chrono::milliseconds minInterval{100};
        std::chrono::milliseconds logInterval{30000};
        double smoothing = 5;
        std::optional<bool> leave;
        bool reverse = false;
        bool autoFinish = true;
        bool disable = false;
        std::shared_ptr<Display> display;
    };

    // What done looks like; 0 (the default) when it is not known. A bar without
    // a total counts up and shows a spinner.
    Options& total(uint64_t value) { return set(values_.total, value); }
    // Where it starts when resuming. The rate only counts what comes after.
    Options& initial(uint64_t value) { return set(values_.initial, value); }
    Options& desc(std::string value) { return set(values_.desc, std::move(value)); }
    Options& postfix(std::string value) { return set(values_.postfix, std::move(value)); }
    Options& unit(std::string value) { return set(values_.unit, std::move(value)); }
    Options& scale(fmt::Scale value) { return set(values_.scale, value); }
    // Counts in bytes: 12.3 MiB/1.00 GiB at 4.20 MiB/s.
    Options& bytes() { return unit("B").scale(fmt::Scale::Binary); }
    // The whole line; see tqdm/render.hpp for what it can hold.
    Options& format(std::string value) { return set(values_.format, std::move(value)); }
    Options& theme(Theme value) { return set(values_.theme, std::optional<Theme>(std::move(value))); }
    Options& spinner(Spinner value) { return set(values_.spinner, std::optional<Spinner>(std::move(value))); }
    Options& color(Color value) { return set(values_.color, value); }
    Options& doneColor(Color value) { return set(values_.doneColor, std::optional<Color>(value)); }
    Options& failedColor(Color value) { return set(values_.failedColor, value); }
    // Cells inside the bar. The default takes what the rest of the line leaves.
    Options& barWidth(int value) { return set(values_.barWidth, value); }
    // The longest line, however wide the terminal.
    Options& maxWidth(int value) { return set(values_.maxWidth, value); }
    // The least time between redraws.
    Options& minInterval(std::chrono::milliseconds value) { return set(values_.minInterval, value); }
    // The least time between lines when the output is a log and not a terminal.
    Options& logInterval(std::chrono::milliseconds value) { return set(values_.logInterval, value); }
    // How many seconds of history the rate and estimate lean on; 0 for the
    // average over the whole run.
    Options& smoothing(double seconds) { return set(values_.smoothing, seconds); }
    // Stay on screen when finished, or vanish. The default stays only if no
    // other bar was running at construction, so an inner loop's bar vanishes.
    Options& leave(bool value) { return set(values_.leave, std::optional<bool>(value)); }
    // Draw the bar and percentage emptying: a countdown.
    Options& reverse(bool value = true) { return set(values_.reverse, value); }
    // Finish when the count reaches the total (the default). Turn this off when
    // the total is still moving, and call finish() yourself.
    Options& autoFinish(bool value) { return set(values_.autoFinish, value); }
    // Count, but show nothing. TQDM_DISABLE=1 in the environment does the same
    // for every bar on the standard streams.
    Options& disable(bool value = true) { return set(values_.disable, value); }
    // Draw there and not on standard error.
    Options& display(std::shared_ptr<Display> value) { return set(values_.display, std::move(value)); }

    // A line for work that has no numbers: "<spinner> Connecting 00:03".
    static Options spinning(std::string label) {
        Options options;
        options.desc(std::move(label)).format("{spinner} {label} {elapsed}{postfix}");
        return options;
    }

    const Values& values() const noexcept { return values_; }

private:
    template <typename T, typename U>
    Options& set(T& field, U&& value) {
        field = std::forward<U>(value);
        return *this;
    }

    Values values_;
};

} // namespace tqdm
