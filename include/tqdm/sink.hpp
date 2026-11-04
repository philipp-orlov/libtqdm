// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Where the bytes go, and what the far end can do with them.
//
// A Sink is called with the display's lock held, one complete frame per call,
// so it needs no locking of its own, but must not call back into the display.

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "tqdm/platform.hpp"

namespace tqdm {

using platform::TerminalSize;

struct Capabilities {
    bool ansi = false;  // Cursor movement works: bars redraw in place. Otherwise log lines.
    bool color = false; // SGR colors reach a reader.
    bool utf8 = false;  // Block glyphs show. Otherwise the default theme is ASCII.
};

class Sink {
public:
    virtual ~Sink() = default;

    // False means the output is gone for good: nothing is written again.
    virtual bool write(std::string_view bytes) noexcept = 0;
    virtual Capabilities capabilities() const noexcept { return {}; }
    virtual TerminalSize size() const noexcept { return {}; }
};

// A stream of the process, written directly: no iostream or stdio buffer, one
// system call per frame, so frames do not tear between threads or processes.
class HandleSink final : public Sink {
public:
    explicit HandleSink(platform::Handle handle) noexcept : handle_(handle) {
        const bool terminal = platform::isTerminal(handle_);
        capabilities_.ansi = terminal && platform::enableAnsi(handle_);
        capabilities_.utf8 = platform::outputIsUtf8(handle_);
        capabilities_.color = colorWanted(capabilities_.ansi);
    }

    bool write(std::string_view bytes) noexcept override {
        return platform::writeAll(handle_, bytes.data(), bytes.size());
    }
    Capabilities capabilities() const noexcept override { return capabilities_; }
    TerminalSize size() const noexcept override { return platform::terminalSize(handle_); }

private:
    // NO_COLOR (no-color.org) wins over FORCE_COLOR and CLICOLOR_FORCE.
    static bool colorWanted(bool otherwise) noexcept {
        const auto isSet = [](const char* name) {
            const char* value = platform::getEnv(name);
            return value != nullptr && *value != '\0' && !(value[0] == '0' && value[1] == '\0');
        };
        const char* noColor = platform::getEnv("NO_COLOR");
        if (noColor != nullptr && *noColor != '\0')
            return false;
        return isSet("FORCE_COLOR") || isSet("CLICOLOR_FORCE") || otherwise;
    }

    platform::Handle handle_;
    Capabilities capabilities_;
};

// Hands every frame to a function: a GUI, a socket, a logger.
class FunctionSink final : public Sink {
public:
    explicit FunctionSink(std::function<void(std::string_view)> function, Capabilities capabilities = {},
                          TerminalSize size = {})
        : function_(std::move(function)), capabilities_(capabilities), size_(size) {}

    bool write(std::string_view bytes) noexcept override {
        try {
            function_(bytes);
            return true;
        } catch (...) {
            return false;
        }
    }
    Capabilities capabilities() const noexcept override { return capabilities_; }
    TerminalSize size() const noexcept override { return size_; }

private:
    std::function<void(std::string_view)> function_;
    Capabilities capabilities_;
    TerminalSize size_;
};

// Keeps everything in a string, to look at later. Pretends to be whatever
// terminal it is told to, which is what the tests run against.
class MemorySink final : public Sink {
public:
    explicit MemorySink(Capabilities capabilities = {}, TerminalSize size = {})
        : capabilities_(capabilities), size_(size) {}

    bool write(std::string_view bytes) noexcept override {
        try {
            text_.append(bytes);
            ++writes_;
            return true;
        } catch (...) {
            return false;
        }
    }
    Capabilities capabilities() const noexcept override { return capabilities_; }
    TerminalSize size() const noexcept override { return size_; }

    void resize(TerminalSize size) noexcept { size_ = size; }
    const std::string& text() const noexcept { return text_; }
    size_t writes() const noexcept { return writes_; }

private:
    Capabilities capabilities_;
    TerminalSize size_;
    std::string text_;
    size_t writes_ = 0;
};

} // namespace tqdm
