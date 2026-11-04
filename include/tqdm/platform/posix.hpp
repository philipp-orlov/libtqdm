// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Linux, the BSDs and macOS. See tqdm/platform.hpp for the contract.

#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace tqdm::platform {

using Handle = int;

inline Handle stdHandle(StdStream stream) noexcept {
    return stream == StdStream::Error ? STDERR_FILENO : STDOUT_FILENO;
}

inline bool isTerminal(Handle handle) noexcept { return ::isatty(handle) == 1; }

inline TerminalSize terminalSize(Handle handle) noexcept {
    struct winsize size {};
    if (::ioctl(handle, TIOCGWINSZ, &size) != 0)
        return {};
    return {static_cast<int>(size.ws_col), static_cast<int>(size.ws_row)};
}

inline const char* getEnv(const char* name) noexcept { return std::getenv(name); }

inline bool enableAnsi(Handle) noexcept {
    const char* term = getEnv("TERM");
    return term != nullptr && *term != '\0' && std::strcmp(term, "dumb") != 0;
}

// The environment, not setlocale(): a library has no business changing or
// depending on the process locale, and most programs never call setlocale().
inline bool outputIsUtf8(Handle) noexcept {
    for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG"}) {
        const char* value = getEnv(name);
        if (value == nullptr || *value == '\0')
            continue;
        for (const char* p = value; *p != '\0'; ++p) {
            if ((p[0] == 'U' || p[0] == 'u') && (p[1] == 'T' || p[1] == 't') && (p[2] == 'F' || p[2] == 'f')) {
                return true;
            }
        }
        return false;
    }
    return false;
}

inline bool writeAll(Handle handle, const char* data, size_t size) noexcept {
    int stalls = 0;
    while (size > 0) {
        const ssize_t written = ::write(handle, data, size);
        if (written > 0) {
            data += written;
            size -= static_cast<size_t>(written);
            stalls = 0;
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        // Someone set the terminal non-blocking. Wait a little, then give up:
        // never hold up the program for its progress bar.
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && stalls++ < 5) {
            struct pollfd wait {
                handle, POLLOUT, 0
            };
            (void)::poll(&wait, 1, 20);
            continue;
        }
        return false;
    }
    return true;
}

} // namespace tqdm::platform
