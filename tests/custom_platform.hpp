// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// A complete backend in a few lines, standing in for what a port will be, and
// the proof that TQDM_PLATFORM_HEADER is all a port has to touch.

#include <cstddef>
#include <string>

namespace tqdm::platform {

struct FakeStream {
    std::string written;
};

using Handle = FakeStream*;

inline FakeStream fakeError;
inline FakeStream fakeOutput;
inline bool fakePrepared = false;

inline Handle stdHandle(StdStream stream) noexcept { return stream == StdStream::Error ? &fakeError : &fakeOutput; }
inline bool isTerminal(Handle) noexcept { return true; }
inline TerminalSize terminalSize(Handle) noexcept { return {50, 10}; }
// Where a Windows backend turns on ENABLE_VIRTUAL_TERMINAL_PROCESSING.
inline bool enableAnsi(Handle) noexcept {
    fakePrepared = true;
    return true;
}
inline bool outputIsUtf8(Handle) noexcept { return true; }
inline bool writeAll(Handle handle, const char* data, size_t size) noexcept {
    handle->written.append(data, size);
    return true;
}
inline const char* getEnv(const char*) noexcept { return nullptr; }

} // namespace tqdm::platform
