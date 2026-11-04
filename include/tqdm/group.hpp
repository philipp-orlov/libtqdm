// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// A set of bars that belong together and are owned in one place. Bars stay in
// the order they were added, which add() may do at any time and from any
// thread. Nothing here is needed just to stack bars: all bars on a stream stack
// anyway. A Group is for when something has to hold on to them.

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#include "tqdm/bar.hpp"
#include "tqdm/display.hpp"
#include "tqdm/options.hpp"

namespace tqdm {

class Group {
public:
    explicit Group(Display::Ptr display = Display::standardError()) : display_(std::move(display)) {}

    Group(const Group&) = delete;
    Group& operator=(const Group&) = delete;

    // The reference stays good for the Group's lifetime. Finished bars stay on
    // screen unless the options say otherwise.
    Bar& add(Options options = {}) {
        if (options.values().display == nullptr)
            options.display(display_);
        if (!options.values().leave.has_value())
            options.leave(true);
        auto bar = std::make_unique<Bar>(options);
        std::lock_guard<std::mutex> lock(mutex_);
        bars_.push_back(std::move(bar));
        return *bars_.back();
    }

    Bar& operator[](size_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        return *bars_[index];
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bars_.size();
    }

    bool allFinished() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& bar : bars_)
            if (!bar->finished())
                return false;
        return true;
    }

    void finishAll() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& bar : bars_)
            bar->finish();
    }

    void write(std::string_view message) noexcept { display_->write(message); }
    const Display::Ptr& display() const noexcept { return display_; }

private:
    Display::Ptr display_;
    mutable std::mutex mutex_;
    std::deque<std::unique_ptr<Bar>> bars_;
};

} // namespace tqdm
