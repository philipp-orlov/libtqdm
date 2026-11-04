// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// One output and the lines live on it: the region at the bottom of a terminal.
//
//     finished output and write()'s messages scroll away above
//     +------------------------------------------+
//     | outer:  40% |#####    | 4/10 ...          |   the region: redrawn in place,
//     | inner:  70% |#######  | 70/100 ...        |   in the order the lines came
//     +------------------------------------------+
//
// All bars on one stream share one Display, so bars in different threads or
// nested loops stack up by themselves, and a message printed through write()
// lands above them.
//
// A frame is built whole and goes out in a single Sink::write, with the cursor
// hidden only inside that write. So a frame never tears, and a crash between
// frames cannot leave the terminal without a cursor: nothing needs restoring,
// and no signal handler is installed.
//
// Autowrap is off for the region's rows, also only inside that write. A line
// that turns out wider than the terminal is clipped, and the region keeps its
// height: the width may be out of date (a resize arrives after the frame was
// built) or wrong (a terminal may measure an emoji differently from us), and a
// wrapped row is what would otherwise leave a trail of old bars on the screen.
//
// When the output is not a terminal nothing is redrawn. Bars write a line now
// and then (Options::logInterval) and a last one when they finish.
//
// A thread keeps the region alive while the program is busy elsewhere: elapsed
// time and spinners move even if no update() arrives. It only exists on a real
// terminal, and only while a bar is live. Build with TQDM_NO_THREADS to leave
// it out; lines then move only when updated.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(TQDM_NO_THREADS)
#include <condition_variable>
#include <thread>
#endif

#include "tqdm/config.hpp"
#include "tqdm/format.hpp"
#include "tqdm/platform.hpp"
#include "tqdm/sink.hpp"
#include "tqdm/unicode.hpp"

namespace tqdm {

// Nanoseconds from any fixed point. Replaceable, which makes tests exact.
using Clock = uint64_t (*)();

inline uint64_t steadyNow() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

// One row of a region. Bar is one; anything else with text to keep on screen
// can be too, between Display::attach() and Display::detach().
class Line {
public:
    virtual ~Line() = default;

    // Called with the display's lock held. Cells beyond `width` (0: no limit) would wrap.
    virtual void render(uint64_t now, int width, std::string& out) = 0;
    // How often this line wants a redraw while nothing else happens.
    virtual uint32_t refreshMs() const noexcept { return 1000; }
};

struct DisplayConfig {
    Clock clock = steadyNow;
    bool ticker = true;    // The thread described above. Terminals only.
    bool disabled = false; // Bars here count, but show nothing.
};

class Display {
public:
    using Ptr = std::shared_ptr<Display>;

    static Ptr create(std::shared_ptr<Sink> sink, DisplayConfig config = {}) {
        return Ptr(new Display(std::move(sink), config));
    }

    // The process's stderr and stdout. Made on first use, and never destroyed,
    // so a bar in a static object is safe in its destructor.
    static const Ptr& standardError() {
        static const Ptr* display = new Ptr(fromStream(platform::StdStream::Error));
        return *display;
    }
    static const Ptr& standardOutput() {
        static const Ptr* display = new Ptr(fromStream(platform::StdStream::Output));
        return *display;
    }

    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    TQDM_COLD ~Display() {
#if !defined(TQDM_NO_THREADS)
        std::thread ticker;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ticker = stopTicker();
        }
        if (ticker.joinable())
            ticker.join();
#endif
    }

    // Prints whole lines above the region, and never garbles it. A newline at
    // the end is optional. Safe from any thread.
    TQDM_COLD void write(std::string_view message) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!message.empty() && message.back() == '\n')
                message.remove_suffix(1);
            if (capabilities_.ansi) {
                pending_.emplace_back(message);
                paint(clock_());
            } else {
                std::string text(message);
                text.push_back('\n');
                send(text);
            }
        } catch (...) {
        }
    }

    // Redraws the region now, whatever the time since the last frame.
    TQDM_COLD void refresh() noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            paint(clock_());
        } catch (...) {
        }
    }

    // Adds a line below the others. Returns how many were live before it.
    TQDM_COLD size_t attach(Line& line) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t live = 0;
        for (const Entry& entry : entries_)
            live += entry.line != nullptr ? 1 : 0;
        entries_.push_back(Entry{&line, {}});
        if (capabilities_.ansi) {
            const uint64_t now = clock_();
            if (entries_.size() == 1 || now - lastFrame() >= kFrameGap)
                paint(now);
            startTicker();
        }
        return live;
    }

    // Removes a line, after one last render. If `keep`, the text stays in the
    // region until every line above it is done, then scrolls away with them.
    TQDM_COLD void detach(Line& line, bool keep) noexcept {
#if !defined(TQDM_NO_THREADS)
        std::thread ticker;
#endif
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto entry =
                std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.line == &line; });
            if (entry == entries_.end())
                return;
            const uint64_t now = clock_();
            if (keep && !capabilities_.ansi) {
                std::string text;
                line.render(now, 0, text);
                text.push_back('\n');
                send(text);
                entries_.erase(entry);
            } else if (keep) {
                line.render(now, lineWidth(), entry->text);
                entry->line = nullptr;
            } else {
                entries_.erase(entry);
            }
            const bool anyLive =
                std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.line != nullptr; });
            if (capabilities_.ansi && (keep || !anyLive || now - lastFrame() >= kFrameGap))
                paint(now);
#if !defined(TQDM_NO_THREADS)
            if (!anyLive)
                ticker = stopTicker();
#endif
        } catch (...) {
        }
#if !defined(TQDM_NO_THREADS)
        if (ticker.joinable())
            ticker.join();
#endif
    }

    // A frame, unless another thread is busy drawing one: an update() must
    // never wait on a slow terminal. True if it drew.
    TQDM_COLD bool tryRefresh(uint64_t now) noexcept {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
        paint(now);
        return true;
    }

    // A log line for a live line, on outputs that are not terminals.
    TQDM_COLD bool tryLog(Line& line, uint64_t now) noexcept {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
        try {
            std::string text;
            line.render(now, 0, text);
            text.push_back('\n');
            send(text);
        } catch (...) {
        }
        return true;
    }

    uint64_t now() const noexcept { return clock_(); }
    uint64_t lastFrame() const noexcept { return lastFrame_.load(std::memory_order_relaxed); }
    const Capabilities& capabilities() const noexcept { return capabilities_; }
    bool disabled() const noexcept { return disabled_; }

private:
    static constexpr uint64_t kFrameGap = 50'000'000; // Structural changes, at most 20/s.
    static constexpr int kDefaultColumns = 80;

    struct Entry {
        Line* line;       // Null once finished: only the text is left.
        std::string text; // What this line last rendered to.
    };

    Display(std::shared_ptr<Sink> sink, const DisplayConfig& config)
        : sink_(std::move(sink)), clock_(config.clock != nullptr ? config.clock : steadyNow),
          capabilities_(sink_->capabilities()), tickerWanted_(config.ticker), disabled_(config.disabled) {}

    TQDM_COLD static Ptr fromStream(platform::StdStream stream) {
        DisplayConfig config;
        const char* disable = platform::getEnv("TQDM_DISABLE");
        config.disabled = disable != nullptr && *disable != '\0' && !(disable[0] == '0' && disable[1] == '\0');
        return create(std::make_shared<HandleSink>(platform::stdHandle(stream)), config);
    }

    int lineWidth() const noexcept {
        const int columns = sink_->size().columns;
        // Never the last column: some terminals wrap on reaching it.
        return (std::max)(1, (columns > 0 ? columns : kDefaultColumns) - 1);
    }

    void send(std::string_view bytes) noexcept {
        if (!broken_ && !bytes.empty() && !sink_->write(bytes))
            broken_ = true;
    }

    static void appendCsi(std::string& out, int count, char command) {
        out.append("\x1b[");
        fmt::appendUint(out, static_cast<uint64_t>(count));
        out.push_back(command);
    }

    // Terminal rows the region's last frame takes at `columns`: more than its
    // lines when the terminal just got narrower and rewrapped them. A rewrapped
    // row that a terminal scrolled off the top stays there: nothing can reach it.
    int rowsTaken(int columns) const noexcept {
        int rows = 0;
        for (const std::string& line : shown_)
            rows += (std::max)(1, (unicode::displayWidth(line) + columns - 1) / columns);
        return rows;
    }

    // The heart of it. Locked by the caller.
    TQDM_COLD void paint(uint64_t now) noexcept {
        if (!capabilities_.ansi)
            return;
        if (broken_) {
            // Nowhere for it to go, and a long-running program must not pile it up.
            pending_.clear();
            entries_.erase(
                std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.line == nullptr; }),
                entries_.end());
            return;
        }
        lastFrame_.store(now, std::memory_order_relaxed);
        try {
            const TerminalSize size = sink_->size();
            const int columns = size.columns > 0 ? size.columns : kDefaultColumns;
            const int width = (std::max)(1, columns - 1);

            for (Entry& entry : entries_)
                if (entry.line != nullptr)
                    entry.line->render(now, width, entry.text);
            // Finished lines at the top have nothing above them left to wait for.
            while (!entries_.empty() && entries_.front().line == nullptr) {
                pending_.push_back(std::move(entries_.front().text));
                entries_.erase(entries_.begin());
            }

            // Moving up past the top of the screen is not possible, so a region
            // taller than the terminal shows what fits, and says so.
            const size_t count = entries_.size();
            const size_t limit = size.rows >= 2 ? static_cast<size_t>(size.rows - 1) : count;
            next_.clear();
            if (count <= limit) {
                for (const Entry& entry : entries_)
                    next_.push_back(entry.text);
            } else {
                for (size_t i = 0; i + 1 < limit; ++i)
                    next_.push_back(entries_[i].text);
                std::string more = "... ";
                fmt::appendUint(more, count - (limit - 1));
                more.append(" more");
                next_.push_back(std::move(more));
            }

            const bool resized = columns != lastColumns_ && lastColumns_ != 0;
            const bool sameShape = pending_.empty() && next_.size() == shown_.size() && !resized;
            if (sameShape && next_ == shown_) {
                lastColumns_ = columns;
                return; // Nothing to say: no bytes on the wire.
            }

            frame_.assign("\x1b[?25l");
            const int up =
                (resized && columns < lastColumns_ ? rowsTaken(columns) : static_cast<int>(shown_.size())) - 1;
            if (up > 0)
                appendCsi(frame_, up, 'A');
            frame_.push_back('\r');

            if (sameShape) {
                // Only the lines that changed.
                frame_.append("\x1b[?7l");
                for (size_t i = 0; i < next_.size(); ++i) {
                    // The last one always: the cursor must end on the last row
                    // of the region's last line, wherever a resize then rewraps it.
                    if (next_[i] != shown_[i] || i + 1 == next_.size()) {
                        frame_ += next_[i];
                        frame_.append("\x1b[K");
                    }
                    if (i + 1 < next_.size())
                        frame_.append("\r\n");
                }
            } else {
                for (const std::string& message : pending_)
                    appendMessage(frame_, message);
                frame_.append("\x1b[?7l");
                for (size_t i = 0; i < next_.size(); ++i) {
                    frame_ += next_[i];
                    frame_.append("\x1b[K");
                    if (i + 1 < next_.size())
                        frame_.append("\r\n");
                }
                frame_.append("\x1b[J"); // Whatever a taller region left below.
            }
            frame_.append("\x1b[?7h\x1b[?25h");
            pending_.clear();
            shown_.swap(next_);
            lastColumns_ = columns;
            send(frame_);
        } catch (...) {
        }
    }

    static void appendMessage(std::string& out, std::string_view message) {
        for (;;) {
            const size_t newline = message.find('\n');
            out += message.substr(0, newline);
            out.append("\x1b[K\r\n");
            if (newline == std::string_view::npos)
                return;
            message.remove_prefix(newline + 1);
        }
    }

#if defined(TQDM_NO_THREADS)
    void startTicker() noexcept {}
#else
    TQDM_COLD void startTicker() noexcept {
        if (!tickerWanted_ || ticker_.joinable())
            return;
        try {
            ticker_ = std::thread([this, generation = tickerGeneration_] { runTicker(generation); });
        } catch (...) {
            // No more threads to be had. Lines still move on update().
        }
    }

    // Locked by the caller; the caller joins the result after unlocking.
    std::thread stopTicker() noexcept {
        ++tickerGeneration_;
        tickerWake_.notify_all();
        return std::move(ticker_);
    }

    TQDM_COLD void runTicker(uint64_t generation) noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        while (generation == tickerGeneration_) {
            uint32_t wait = 1000;
            for (const Entry& entry : entries_)
                if (entry.line != nullptr)
                    wait = (std::min)(wait, entry.line->refreshMs());
            tickerWake_.wait_for(lock, std::chrono::milliseconds(std::max<uint32_t>(wait, 20)));
            if (generation == tickerGeneration_)
                paint(clock_());
        }
    }

    std::thread ticker_;
    std::condition_variable tickerWake_;
    uint64_t tickerGeneration_ = 0;
#endif

    std::shared_ptr<Sink> sink_;
    Clock clock_;
    Capabilities capabilities_;
    bool tickerWanted_;
    bool disabled_;

    std::mutex mutex_;
    std::atomic<uint64_t> lastFrame_{0};
    bool broken_ = false;
    int lastColumns_ = 0;
    std::vector<Entry> entries_;
    std::vector<std::string> pending_; // Lines to scroll away with the next frame.
    std::vector<std::string> shown_;   // What the region shows now.
    std::vector<std::string> next_;
    std::string frame_;
};

// Prints a line above any bars on standard error.
inline void write(std::string_view message) noexcept { Display::standardError()->write(message); }

} // namespace tqdm
