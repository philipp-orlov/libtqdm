// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Everything libtqdm asks of the operating system, in one place. No other
// header includes an OS header, so a port
// is one new file here (docs/PORTING.md).
//
// A backend defines, in namespace tqdm::platform:
//
//   using Handle = ...;                   what a stream is: fd, HANDLE, FILE*
//   Handle stdHandle(StdStream)          the process's stdout or stderr
//   bool isTerminal(Handle)              an interactive terminal, not a pipe or file
//   TerminalSize terminalSize(Handle)    {0, 0} when unknown
//   bool enableAnsi(Handle)              make cursor movement work; false if it cannot
//   bool outputIsUtf8(Handle)           the terminal shows UTF-8 as text
//   bool writeAll(Handle, const char*, size_t)   every byte, or false for good
//   const char* getEnv(const char*)      nullptr when unset
//
// All of them are noexcept. A backend is selected, first match wins, by:
//
//   TQDM_PLATFORM_HEADER    your own: -DTQDM_PLATFORM_HEADER='"my_backend.hpp"'
//   TQDM_PLATFORM_GENERIC   standard C only: no terminal control, log lines
//   _WIN32                  generic for now, until platform/windows.hpp exists
//   __unix__, __APPLE__     POSIX
//   anything else           generic

#include <cstddef>
#include <type_traits>
#include <utility>

namespace tqdm::platform {

enum class StdStream { Output, Error };

struct TerminalSize {
    int columns = 0;
    int rows = 0;
};

} // namespace tqdm::platform

#if defined(TQDM_PLATFORM_HEADER)
#include TQDM_PLATFORM_HEADER
#elif defined(TQDM_PLATFORM_GENERIC) || defined(_WIN32)
#include "tqdm/platform/generic.hpp"
#elif defined(__unix__) || defined(__APPLE__)
#include "tqdm/platform/posix.hpp"
#else
#include "tqdm/platform/generic.hpp"
#endif

namespace tqdm::platform {

// A backend that drifts from the contract fails here, with this message, and
// not somewhere in the middle of Display.
static_assert(std::is_same_v<decltype(stdHandle(StdStream::Error)), Handle>,
              "platform backend: Handle stdHandle(StdStream)");
static_assert(noexcept(isTerminal(std::declval<Handle>())) &&
                  std::is_same_v<decltype(isTerminal(std::declval<Handle>())), bool>,
              "platform backend: bool isTerminal(Handle) noexcept");
static_assert(noexcept(terminalSize(std::declval<Handle>())) &&
                  std::is_same_v<decltype(terminalSize(std::declval<Handle>())), TerminalSize>,
              "platform backend: TerminalSize terminalSize(Handle) noexcept");
static_assert(noexcept(enableAnsi(std::declval<Handle>())) &&
                  std::is_same_v<decltype(enableAnsi(std::declval<Handle>())), bool>,
              "platform backend: bool enableAnsi(Handle) noexcept");
static_assert(noexcept(outputIsUtf8(std::declval<Handle>())) &&
                  std::is_same_v<decltype(outputIsUtf8(std::declval<Handle>())), bool>,
              "platform backend: bool outputIsUtf8(Handle) noexcept");
static_assert(noexcept(writeAll(std::declval<Handle>(), "", size_t{0})) &&
                  std::is_same_v<decltype(writeAll(std::declval<Handle>(), "", size_t{0})), bool>,
              "platform backend: bool writeAll(Handle, const char*, size_t) noexcept");
static_assert(noexcept(getEnv("")) && std::is_same_v<decltype(getEnv("")), const char*>,
              "platform backend: const char* getEnv(const char*) noexcept");

} // namespace tqdm::platform
