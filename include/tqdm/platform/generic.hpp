// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Standard C only, so it builds anywhere. There is no way to ask about a
// terminal, so every stream counts as a log: bars come out as periodic lines
// and nothing moves the cursor. See tqdm/platform.hpp for the contract.

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace tqdm::platform {

using Handle = std::FILE*;

inline Handle stdHandle(StdStream stream) noexcept { return stream == StdStream::Error ? stderr : stdout; }

inline bool isTerminal(Handle) noexcept { return false; }

inline TerminalSize terminalSize(Handle) noexcept { return {}; }

inline bool enableAnsi(Handle) noexcept { return false; }

inline bool outputIsUtf8(Handle) noexcept { return false; }

inline bool writeAll(Handle handle, const char* data, size_t size) noexcept {
    return std::fwrite(data, 1, size, handle) == size && std::fflush(handle) == 0;
}

inline const char* getEnv(const char* name) noexcept {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // getenv is "unsafe": the pointer is only read at once
#endif
    return std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

} // namespace tqdm::platform
