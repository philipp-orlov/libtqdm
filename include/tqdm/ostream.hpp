// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The parts that touch iostreams, so that nothing else has to.

#include <memory>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

#include "tqdm/display.hpp"
#include "tqdm/platform.hpp"
#include "tqdm/sink.hpp"

namespace tqdm {

// Draws to any std::ostream, as a log unless told it is a terminal.
class OstreamSink final : public Sink {
public:
    explicit OstreamSink(std::ostream& stream, Capabilities capabilities = {}, TerminalSize size = {})
        : stream_(stream), capabilities_(capabilities), size_(size) {}

    bool write(std::string_view bytes) noexcept override {
        try {
            stream_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            stream_.flush();
            return stream_.good();
        } catch (...) {
            return false;
        }
    }
    Capabilities capabilities() const noexcept override { return capabilities_; }
    TerminalSize size() const noexcept override { return size_; }

private:
    std::ostream& stream_;
    Capabilities capabilities_;
    TerminalSize size_;
};

// While it lives, whole lines written to a stream go through Display::write, so
// that existing std::cout or logging code cannot scribble over the bars. Once
// gone, the stream is back as it was, and any unfinished line is out.
//
//   tqdm::ScopedRedirect redirect(std::cout, tqdm::platform::StdStream::Output);
//
// Pass the stream the ostream is really writing to, as above: when that is not a
// terminal (a pipe, a file) the redirect stands aside, and the output keeps going
// where it was meant to. The other constructor always redirects.
//
// Not with an OstreamSink on the same stream: it would feed itself.
class ScopedRedirect {
public:
    ScopedRedirect(std::ostream& stream, platform::StdStream origin, Display::Ptr display = Display::standardError())
        : stream_(stream), buffer_(std::move(display)) {
        if (platform::isTerminal(platform::stdHandle(origin)))
            original_ = stream_.rdbuf(&buffer_);
    }

    ScopedRedirect(std::ostream& stream, Display::Ptr display) : stream_(stream), buffer_(std::move(display)) {
        original_ = stream_.rdbuf(&buffer_);
    }

    ScopedRedirect(const ScopedRedirect&) = delete;
    ScopedRedirect& operator=(const ScopedRedirect&) = delete;

    ~ScopedRedirect() {
        if (original_ != nullptr) {
            stream_.rdbuf(original_);
            buffer_.finish();
        }
    }

private:
    class Buffer final : public std::streambuf {
    public:
        explicit Buffer(Display::Ptr display) : display_(std::move(display)) {}

        void finish() {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!line_.empty()) {
                display_->write(line_);
                line_.clear();
            }
        }

    protected:
        int_type overflow(int_type c) override {
            if (!traits_type::eq_int_type(c, traits_type::eof())) {
                const char ch = traits_type::to_char_type(c);
                xsputn(&ch, 1);
            }
            return traits_type::not_eof(c);
        }

        std::streamsize xsputn(const char* data, std::streamsize size) override {
            std::lock_guard<std::mutex> lock(mutex_);
            std::string_view rest(data, static_cast<size_t>(size));
            for (size_t newline; (newline = rest.find('\n')) != std::string_view::npos;) {
                line_.append(rest.substr(0, newline));
                display_->write(line_);
                line_.clear();
                rest.remove_prefix(newline + 1);
            }
            line_.append(rest);
            return size;
        }

    private:
        Display::Ptr display_;
        std::mutex mutex_;
        std::string line_;
    };

    std::ostream& stream_;
    Buffer buffer_;
    std::streambuf* original_ = nullptr;
};

} // namespace tqdm
