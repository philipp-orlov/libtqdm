# Porting

Everything OS-specific is in one file per platform under `include/tqdm/platform/`. The contract is the
comment at the top of [`platform.hpp`](../include/tqdm/platform.hpp), checked at compile time by the
`static_assert`s at the end of the same file. A port should not need to touch any other library header.

These already hold, so a port does not have to arrange them:

* Headers are pure ASCII: no `/utf-8` needed by anyone including them.
* `std::min`, `std::max` and `numeric_limits::max` are parenthesized, and `tests/test_units_b.cpp` builds with
  `windows.h`-style macros defined (`min`, `max`, `small`, `near`, `far`, `ERROR`, ...) to keep it that way.
* The test suite is backend-independent, apart from the block in `tests/test_platform.cpp` behind `#if`.
* `tests/custom_platform.hpp` is a complete backend in 40 lines, as a template.

## What is proven on Linux today

Before any port starts: a third-party backend through `TQDM_PLATFORM_HEADER` (`test_backend_custom`), the
generic one, in both its own test and the portable half of the platform test (`test_backend_generic`,
`test_platform_generic`). And the macros above.

## macOS

Should need no new code: `__APPLE__` selects the POSIX backend, and everything in it is POSIX.

1. Look at the `macos` job in `.github/workflows/ci.yml`, or run the build and `ctest` on a Mac.
2. Fix what it finds. Unverified so far: Apple Clang and libc++ on the whole suite, and `e2e_tmux.sh`
   (needs `brew install tmux`).
3. Remove `continue-on-error` from the job, and update the table in the README.

## Windows

1. Add `include/tqdm/platform/windows.hpp`, and select it in `platform.hpp`:

   ```cpp
   #elif defined(TQDM_PLATFORM_GENERIC)
   #include "tqdm/platform/generic.hpp"
   #elif defined(_WIN32)
   #include "tqdm/platform/windows.hpp"
   ```

2. Implement the contract:

   | | |
   | --- | --- |
   | `Handle` | `void*`, the Win32 `HANDLE` type, without leaking its name into the rest. |
   | `stdHandle` | `GetStdHandle(STD_OUTPUT_HANDLE / STD_ERROR_HANDLE)`. |
   | `isTerminal` | `GetConsoleMode` succeeds. Git Bash's mintty pipes report false, and get log lines: acceptable to begin with. |
   | `terminalSize` | `GetConsoleScreenBufferInfo`, from `srWindow`, not `dwSize`: that is the scrollback. |
   | `enableAnsi` | `SetConsoleMode` adding `ENABLE_VIRTUAL_TERMINAL_PROCESSING`. Returns false before Windows 10 1511, and the bars become log lines by themselves. |
   | `outputIsUtf8` | `SetConsoleOutputCP(CP_UTF8)` returns true. The one place a process-wide setting has to be made; restore it at exit if this matters to the host program. |
   | `writeAll` | `WriteFile`, in a loop. Not `WriteConsoleA`, so redirected handles work the same way. |
   | `getEnv` | `getenv`, with C4996 silenced as in `generic.hpp`. Or `GetEnvironmentVariableA`, into a thread-local buffer. |

   `windows.h` belongs only in this file, and only with `WIN32_LEAN_AND_MEAN` and `NOMINMAX` defined first.
   Better still, declare what is needed by hand, so including libtqdm does not bring all of it into a program.

3. Port the POSIX half of `tests/test_platform.cpp` to ConPTY (`CreatePseudoConsole`).

4. Check by hand, in Windows Terminal and the classic `conhost`:

   * `\x1b[?7l` and `\x1b[?25l` work in `conhost`. If autowrap does not, the region's height depends on the
     width being right (it still is, in the normal case).
   * Whether resizing rewraps. `conhost` depends on its "wrap text output" setting, Windows Terminal does.
     If not, `Display::rowsTaken` overestimates, as with `xterm` (see "Limits" in the README).

5. Try what commonly goes wrong in progress bars on Windows, none of which has been checked here: a bar
   shrinking in `cmd`, a bar not staying on one line, `byte` ambiguity with `windows.h`, and building with MinGW.

6. Remove `continue-on-error` from the `windows` job in the CI, and update the table in the README.

## Anywhere else

`generic.hpp` is standard C and works anywhere with `fwrite`, giving log lines. That is the fallback for
any platform not recognized by `platform.hpp`. `TQDM_NO_THREADS` removes the one use of `std::thread`,
for targets without one; `std::mutex` and `std::atomic` remain.
