// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The progress bar, and everything else that shows progress: with a total it is
// a bar, without one a counter with a spinner, and with a format of its own
// whatever that says.
//
// update() is one atomic add and one atomic load, a few nanoseconds, until it is
// time for a frame. It never waits for a lock, a clock or a terminal unless this
// very call is the one that draws, and if another thread is already drawing it
// moves on. How often it even reads the clock adapts to the rate, so a loop of
// any speed gets about the same small overhead.
//
// No progress call throws. An output that fails makes the bar quietly stop
// drawing: a progress bar is not a reason for a program to fall over.
//
// All members are safe to call from any thread at any time, except that the
// destructor must be the last call, as with any object.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tqdm/config.hpp"
#include "tqdm/display.hpp"
#include "tqdm/format.hpp"
#include "tqdm/options.hpp"
#include "tqdm/render.hpp"
#include "tqdm/theme.hpp"
#include "tqdm/unicode.hpp"

namespace tqdm {

class Bar final : public Line {
public:
    TQDM_COLD explicit Bar(const Options& options = {})
        : uncaughtAtStart_(std::uncaught_exceptions()), values_(options.values()) {
        display_ = values_.display != nullptr ? values_.display : Display::standardError();
        count_.store(values_.initial, std::memory_order_relaxed);
        total_.store(values_.total, std::memory_order_relaxed);

        const Capabilities& capabilities = display_->capabilities();
        theme_ = detail::usableTheme(values_.theme.has_value() ? *values_.theme
                                     : capabilities.utf8       ? themes::blocks()
                                                               : themes::ascii());
        spinner_ = values_.spinner.has_value() ? *values_.spinner
                   : capabilities.utf8         ? spinners::dots()
                                               : spinners::ascii();
        const bool scaled = values_.scale != fmt::Scale::None;
        const char* known = scaled ? "{desc}{percent} {bar} {n:unit}/{total:unit} [{elapsed}<{eta}, {rate}]{postfix}"
                                   : "{desc}{percent} {bar} {n}/{total} [{elapsed}<{eta}, {rate}]{postfix}";
        const char* unknown = "{desc}{spinner} {n:unit} [{elapsed}, {rate}]{postfix}";
        knownTokens_ = detail::parseFormat(values_.format.empty() ? known : values_.format);
        unknownTokens_ = detail::parseFormat(values_.format.empty() ? unknown : values_.format);
        desc_ = unicode::sanitizeLine(values_.desc);
        postfix_ = unicode::sanitizeLine(values_.postfix);
        minInterval_ = toNs(values_.minInterval);
        logInterval_ = toNs(values_.logInterval);

        if (values_.disable || display_->disabled()) {
            state_.store(State::Done, std::memory_order_relaxed);
            nextCheck_.store(kNever, std::memory_order_relaxed);
            return;
        }
        start(display_->now());
        keep_ = values_.leave.value_or(true);
        const size_t others = display_->attach(*this);
        keep_ = values_.leave.value_or(others == 0);
    }

    Bar(const Bar&) = delete;
    Bar& operator=(const Bar&) = delete;

    // An exception on its way through the bar's scope marks it as failed.
    TQDM_COLD ~Bar() override {
        if (std::uncaught_exceptions() > uncaughtAtStart_)
            fail();
        else
            finish();
    }

    // Counts `n` more. Reaching the total finishes the bar.
    void update(uint64_t n = 1) noexcept {
        const uint64_t value = count_.fetch_add(n, std::memory_order_relaxed) + n;
        if (value >= nextCheck_.load(std::memory_order_relaxed))
            onCheck(value);
    }

    Bar& operator+=(uint64_t n) noexcept {
        update(n);
        return *this;
    }

    // Jumps to an absolute count, up or down.
    void setCount(uint64_t value) noexcept {
        count_.store(value, std::memory_order_relaxed);
        onCheck(value);
    }

    // Changes the total while running; 0 makes it unknown. A bar that may reach
    // an old total before the new one arrives wants Options::autoFinish(false).
    void setTotal(uint64_t total) noexcept {
        total_.store(total, std::memory_order_relaxed);
        if (state() == State::Running)
            nextCheck_.store(count_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void setDescription(std::string_view text) noexcept {
        assignText([&] { desc_ = unicode::sanitizeLine(text); });
    }

    void setPostfix(std::string_view text) noexcept {
        assignText([&] {
            fields_.clear();
            postfix_ = unicode::sanitizeLine(text);
        });
    }

    // Keeps "key=value" pairs in the postfix, in first-seen order: setPostfix("loss", 0.25).
    void setPostfix(std::string_view key, std::string_view value) noexcept {
        assignText([&] { putField(key, std::string(value)); });
    }

    void setPostfix(std::string_view key, const char* value) noexcept { setPostfix(key, std::string_view(value)); }

    void setPostfix(std::string_view key, double value) noexcept {
        assignText([&] {
            std::string text;
            fmt::appendValue(text, value);
            putField(key, std::move(text));
        });
    }

    // Done, at whatever count it got to. The final frame shows the average rate
    // and the elapsed time stops. Called by the destructor if nobody else did.
    void finish() noexcept { end(State::Done, {}); }

    // Failed: the line takes Options::failedColor, and `reason` the postfix.
    void fail(std::string_view reason = {}) noexcept { end(State::Failed, reason); }

    // Back to the start, on the screen again if it had finished.
    TQDM_COLD void reset(std::optional<uint64_t> total = std::nullopt) noexcept {
        try {
            const uint64_t now = display_->now();
            if (values_.disable || display_->disabled()) {
                count_.store(values_.initial, std::memory_order_relaxed);
                return;
            }
            bool reattach = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (total.has_value())
                    total_.store(*total, std::memory_order_relaxed);
                count_.store(values_.initial, std::memory_order_relaxed);
                reattach = state() != State::Running;
                startLocked(now);
            }
            if (reattach)
                display_->attach(*this);
        } catch (...) {
        }
    }

    uint64_t count() const noexcept { return count_.load(std::memory_order_relaxed); }
    uint64_t total() const noexcept { return total_.load(std::memory_order_relaxed); }
    State state() const noexcept { return state_.load(std::memory_order_acquire); }
    bool finished() const noexcept { return state() != State::Running; }
    const Display::Ptr& display() const noexcept { return display_; }

    // The current numbers, for a GUI or a log of your own.
    Snapshot snapshot() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshotLocked(display_->now());
    }

    // Counts from one thread that batches into the shared bar, for loops so hot
    // and so parallel that even a contended atomic add shows up.
    class Local {
    public:
        explicit Local(Bar& bar, uint64_t batch = 256) noexcept : bar_(bar), batch_(batch) {}
        Local(const Local&) = delete;
        Local& operator=(const Local&) = delete;
        ~Local() { flush(); }

        void update(uint64_t n = 1) noexcept {
            pending_ += n;
            if (pending_ >= batch_)
                flush();
        }
        void flush() noexcept {
            if (pending_ > 0) {
                bar_.update(pending_);
                pending_ = 0;
            }
        }

    private:
        Bar& bar_;
        uint64_t batch_;
        uint64_t pending_ = 0;
    };

    // Line.
    TQDM_COLD void render(uint64_t now, int width, std::string& out) override {
        std::lock_guard<std::mutex> lock(mutex_);
        sampleLocked(now);
        const Snapshot snapshot = snapshotLocked(now);

        LineSpec spec;
        spec.tokens = snapshot.total != 0 ? &knownTokens_ : &unknownTokens_;
        spec.theme = &theme_;
        spec.spinner = &spinner_;
        spec.label = desc_;
        spec.postfix = postfix_;
        spec.unit = values_.unit;
        spec.scale = values_.scale;
        spec.barColor = snapshot.state == State::Failed ? values_.failedColor
                        : snapshot.state == State::Done ? values_.doneColor.value_or(values_.color)
                                                        : values_.color;
        spec.barWidth = values_.barWidth;
        spec.useColor = display_->capabilities().color;
        spec.useUtf8 = display_->capabilities().utf8;
        const int limit = values_.maxWidth > 0 && (width == 0 || values_.maxWidth < width) ? values_.maxWidth : width;
        renderLine(spec, snapshot, limit, out);
    }

    uint32_t refreshMs() const noexcept override {
        const bool unknown = total() == 0;
        const std::vector<detail::Token>& tokens = unknown ? unknownTokens_ : knownTokens_;
        const bool animated = detail::uses(tokens, detail::Token::Kind::Spinner) ||
                              (unknown && detail::uses(tokens, detail::Token::Kind::Bar));
        return animated ? (std::max<uint32_t>)(spinner_.intervalMs, 20) : 1000;
    }

private:
    static constexpr uint64_t kNever = (std::numeric_limits<uint64_t>::max)();
    static constexpr uint64_t kMostBetweenChecks = 4096;

    static uint64_t toNs(std::chrono::milliseconds value) noexcept {
        return value.count() > 0 ? static_cast<uint64_t>(value.count()) * 1'000'000 : 0;
    }

    void start(uint64_t now) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        startLocked(now);
    }

    void startLocked(uint64_t now) noexcept {
        const uint64_t count = count_.load(std::memory_order_relaxed);
        startNs_ = now;
        endNs_ = now;
        sampleNs_ = now;
        sampleCount_ = count;
        rate_ = 0;
        checkNs_.store(now, std::memory_order_relaxed);
        checkCount_.store(count, std::memory_order_relaxed);
        lastLogNs_.store(now, std::memory_order_relaxed);
        reachedTotal_.store(false, std::memory_order_relaxed);
        nextCheck_.store(count + 1, std::memory_order_relaxed);
        state_.store(State::Running, std::memory_order_release);
    }

    // The slow path: it may be time for a frame.
    TQDM_COLD void onCheck(uint64_t value) noexcept {
        if (state() != State::Running)
            return;
        const uint64_t total = total_.load(std::memory_order_relaxed);
        const bool complete = total != 0 && value >= total;
        if (complete && values_.autoFinish) {
            finish();
            return;
        }
        const uint64_t now = display_->now();
        if (display_->capabilities().ansi) {
            // The one frame that shows a total was reached does not wait its turn.
            const bool firstAtTotal = complete && !reachedTotal_.exchange(true, std::memory_order_relaxed);
            if (firstAtTotal || now - display_->lastFrame() >= minInterval_)
                display_->tryRefresh(now);
        } else if (now - lastLogNs_.load(std::memory_order_relaxed) >= logInterval_) {
            if (display_->tryLog(*this, now))
                lastLogNs_.store(now, std::memory_order_relaxed);
        }
        planNextCheck(value, now, total);
    }

    // Aims the next check at half a redraw interval from now, going by how fast
    // the count moved since this one. The cap bounds how stale a bar can get
    // when a fast loop suddenly turns slow, in programs without the ticker.
    void planNextCheck(uint64_t value, uint64_t now, uint64_t total) noexcept {
        const uint64_t lastNs = checkNs_.exchange(now, std::memory_order_relaxed);
        const uint64_t lastCount = checkCount_.exchange(value, std::memory_order_relaxed);
        uint64_t step = 1;
        if (now > lastNs && value > lastCount) {
            const double perNs = static_cast<double>(value - lastCount) / static_cast<double>(now - lastNs);
            const double target = perNs * static_cast<double>(std::max<uint64_t>(minInterval_, 1'000'000)) / 2;
            step = target >= static_cast<double>(kMostBetweenChecks)
                       ? kMostBetweenChecks
                       : (std::max<uint64_t>)(static_cast<uint64_t>(target), 1);
        }
        uint64_t next = value + step;
        // Never past the total, so completion is always seen.
        if (total > value)
            next = (std::min)(next, total);
        nextCheck_.store(next, std::memory_order_relaxed);
    }

    template <typename F>
    void assignText(F&& assign) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            assign();
        } catch (...) {
        }
    }

    TQDM_COLD void putField(std::string_view key, std::string value) {
        const std::string safeKey = unicode::sanitizeLine(key);
        value = unicode::sanitizeLine(value);
        const auto field =
            std::find_if(fields_.begin(), fields_.end(), [&](const auto& f) { return f.first == safeKey; });
        if (field != fields_.end())
            field->second = std::move(value);
        else
            fields_.emplace_back(safeKey, std::move(value));
        postfix_.clear();
        for (const auto& f : fields_) {
            if (!postfix_.empty())
                postfix_.append(", ");
            postfix_ += f.first;
            postfix_.push_back('=');
            postfix_ += f.second;
        }
    }

    TQDM_COLD void end(State state, std::string_view reason) noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (state_.load(std::memory_order_relaxed) != State::Running)
                return;
            nextCheck_.store(kNever, std::memory_order_relaxed);
            endNs_ = display_->now();
            if (!reason.empty()) {
                try {
                    fields_.clear();
                    postfix_ = unicode::sanitizeLine(reason);
                } catch (...) {
                }
            }
            state_.store(state, std::memory_order_release);
        }
        display_->detach(*this, keep_);
    }

    // Folds the movement since the last sample into the smoothed rate, weighting
    // by time and not by how many frames happened to be drawn.
    void sampleLocked(uint64_t now) noexcept {
        constexpr uint64_t kShortestSample = 10'000'000;
        if (state() != State::Running || now < sampleNs_ + kShortestSample)
            return;
        const uint64_t count = count_.load(std::memory_order_relaxed);
        const double seconds = static_cast<double>(now - sampleNs_) / 1e9;
        const double moved = count > sampleCount_ ? static_cast<double>(count - sampleCount_) : 0;
        const double instant = moved / seconds;
        if (values_.smoothing <= 0) {
            const double run = static_cast<double>(now - startNs_) / 1e9;
            const double done = count > values_.initial ? static_cast<double>(count - values_.initial) : 0;
            rate_ = done / run;
        } else if (rate_ <= 0) {
            rate_ = instant;
        } else {
            rate_ += (1 - std::exp(-seconds / values_.smoothing)) * (instant - rate_);
        }
        sampleNs_ = now;
        sampleCount_ = count;
    }

    Snapshot snapshotLocked(uint64_t now) const noexcept {
        Snapshot s;
        s.n = count_.load(std::memory_order_relaxed);
        s.total = total_.load(std::memory_order_relaxed);
        s.state = state_.load(std::memory_order_relaxed);
        const uint64_t until = s.state == State::Running ? now : endNs_;
        s.elapsed = until > startNs_ ? static_cast<double>(until - startNs_) / 1e9 : 0;
        s.rate = rate_;
        if (s.state != State::Running) {
            const double done = s.n > values_.initial ? static_cast<double>(s.n - values_.initial) : 0;
            s.rate = s.elapsed > 0 ? done / s.elapsed : rate_;
        }
        if (s.total != 0) {
            const double reached =
                s.n >= s.total ? 1 : (std::min)(static_cast<double>(s.n) / static_cast<double>(s.total), 0.999999999);
            s.fraction = values_.reverse ? 1 - reached : reached;
            if (s.n >= s.total)
                s.eta = 0;
            else if (s.rate > 0)
                s.eta = static_cast<double>(s.total - s.n) / s.rate;
        } else if (s.state != State::Running) {
            s.fraction = 1;
        }
        return s;
    }

    // The hot path gets a cache line each, away from each other and from the
    // rest: count_ is written by every update(), nextCheck_ only read.
    alignas(64) std::atomic<uint64_t> count_{0};
    alignas(64) std::atomic<uint64_t> nextCheck_{kNever};

    alignas(64) std::atomic<uint64_t> total_{0};
    std::atomic<State> state_{State::Done};
    std::atomic<uint64_t> checkNs_{0};
    std::atomic<uint64_t> checkCount_{0};
    std::atomic<uint64_t> lastLogNs_{0};
    std::atomic<bool> reachedTotal_{false};

    int uncaughtAtStart_;
    bool keep_ = true;
    uint64_t minInterval_ = 0;
    uint64_t logInterval_ = 0;
    Options::Values values_;
    Display::Ptr display_;
    Theme theme_;
    Spinner spinner_;
    std::vector<detail::Token> knownTokens_;
    std::vector<detail::Token> unknownTokens_;

    mutable std::mutex mutex_; // What is below. Never held while taking the display's lock.
    std::string desc_;
    std::string postfix_;
    std::vector<std::pair<std::string, std::string>> fields_;
    uint64_t startNs_ = 0;
    uint64_t endNs_ = 0;
    uint64_t sampleNs_ = 0;
    uint64_t sampleCount_ = 0;
    double rate_ = 0;
};

} // namespace tqdm
