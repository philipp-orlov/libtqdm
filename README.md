# libtqdm

Progress bars for C++17. Header-only, no dependencies beyond the standard library, and cheap
enough to leave in the hottest loop you have: an update costs about 4 ns and never waits for a
terminal.

```cpp
#include <tqdm/tqdm.hpp>

for (auto& file : tqdm::iter(files, tqdm::Options().desc("files").unit("file"))) {
    process(file);
}
```

```
files:  42% |█████████████████▊                        | 252/600 [00:02<00:04, 96.0 file/s]
```

* `include/tqdm/`: the library. Add it to the include path, and that is the installation.
* `examples/`: ten small programs, one per idea.
* `tests/`, `benchmarks/`.
* `docs/design.md`: how it works and why. `docs/PORTING.md`: bringing it to a new platform.

## Platforms

| Platform | State |
| --- | --- |
| Linux | Supported. Tested with GCC 13 on aarch64: the whole suite, sanitizers included. The CI config adds Clang on x86-64, and has not had its first run yet. |
| macOS | Uses the POSIX backend and should work. Not yet built: its CI job is there, allowed to fail. |
| Windows | Compiles to the generic backend by design: plain log lines, no redraw. Not yet built. The native backend is the next step, in one file ([PORTING](docs/PORTING.md)). |

All OS calls live behind seven functions in [`tqdm/platform.hpp`](include/tqdm/platform.hpp). Build
with `-DTQDM_PLATFORM_HEADER='"my_backend.hpp"'` to supply your own.

## Using it

### CMake

```cmake
add_subdirectory(libtqdm)        # or: find_package(tqdm CONFIG REQUIRED), once installed
target_link_libraries(my_target PRIVATE tqdm::tqdm)
```

Without CMake: add `include/` to the include path and link with `-pthread`.

### Around a loop

```cpp
for (auto& item : tqdm::iter(items)) { /* any range; the total comes from it */ }
for (int i : tqdm::range(1000)) { /* also range(a, b) and range(a, b, step) */ }

auto loop = tqdm::iter(items);   // Name it to reach the bar from inside the loop.
for (auto& item : loop) {
    loop.bar().setPostfix("size", static_cast<double>(item.size()));
}
```

Items pass through untouched. A temporary range is kept alive for the loop. A `break` shows the
work that was finished, and an exception leaving the loop marks the bar as failed.

### By hand

```cpp
tqdm::Bar bar(tqdm::Options().total(1000).desc("training").color(tqdm::colors::kCyan));
for (int batch = 0; batch < 1000; ++batch) {
    bar.setPostfix("loss", train(batch));   // Keeps an ordered list of key=value.
    bar.update();                            // From any thread. Finishes on reaching the total.
}
```

### Bytes, and things with no total

```cpp
tqdm::Bar download(tqdm::Options().bytes().total(size).initial(resumedFrom).desc("ubuntu.iso"));
// ubuntu.iso:  31% |█████████▍                     | 215 MiB/700 MiB [00:03<00:07, 71.7 MiB/s]

tqdm::Bar scan(tqdm::Options().desc("scanned").unit("file"));   // No total: counts, with a spinner.
// scanned: ⠹ 1834 file [00:04, 442 file/s]

tqdm::Bar wait(tqdm::Options::spinning("Connecting"));          // No numbers at all.
// ⠋ Connecting 00:03                  ... which ends as: ✓ Connected 00:04
```

The spinner and the elapsed time keep moving while your thread is blocked.

### Several at once

Bars on one stream stack by themselves, in the order they were made, whichever thread they belong
to. An inner loop's bar clears away when done and an outer one stays.

```
epoch:  66% |██████████████████████████████▋               | 2/3 [00:00<00:01, 9.46 it/s]
batch:  47% |█████████████████████▍                       | 19/40 [00:00<00:01, 184 it/s]
```

A `tqdm::Group` is for when something must own them; it may grow at any time:

```cpp
tqdm::Group group;
tqdm::Bar& bar = group.add(tqdm::Options().total(100).desc("layer-0"));   // Good while group lives.
group.write("started");
```

### Printing while a bar runs

```cpp
tqdm::write("checkpoint saved");    // A whole line above the bars, from any thread.

// Or leave existing code as it is, and catch its stream:
tqdm::ScopedRedirect redirect(std::cout, tqdm::platform::StdStream::Output);
```

The redirect stands aside if stdout is not a terminal, so `program > out.txt` still works. Plain
`std::cout` without it would land on top of the bar, as with any tool of this kind.

### Hot, parallel loops

`update()` from many threads is safe, but it is one contended cache line.
[`Bar::Local`](examples/parallel_for.cpp) batches per thread and takes that away:

```cpp
tqdm::Bar::Local local(bar);        // One in each thread.
for (auto& item : myShare) {
    work(item);
    local.update();                 // Flushes every 256, and on destruction.
}
```

### Not a terminal

When output is a pipe or a file nothing is redrawn and no escape reaches it. Short jobs print one
line at the end, long ones a line every `logInterval` (30 s by default), so CI logs stay readable:

```
training: 100% |████████████████████| 200/200 [00:00<00:00, 385 batch/s] loss=0.5452, lr=0.001
```

### Output anywhere

Bars draw on stderr. To go elsewhere, make a `Display` over a `Sink`: `HandleSink` for a stream of
the process, `OstreamSink` for any `std::ostream`, `FunctionSink` for a callback, or your own in
three lines. `bar.snapshot()` gives the numbers themselves, for a GUI or a metrics endpoint.

```cpp
auto display = tqdm::Display::create(std::make_shared<tqdm::FunctionSink>(
    [](std::string_view line) { sendToUi(line); }));
tqdm::Bar bar(tqdm::Options().display(display).total(50));
```

`Display::standardOutput()` draws on stdout. A class deriving from `tqdm::Line` can join a region
as a row of its own, a status header for example.

## Reference

### Options

All optional, all chainable. `tqdm::Options` is plain data, so a house style can live in one and be
copied.

| Option | Default | |
| --- | --- | --- |
| `total(n)` | unknown | What done looks like. |
| `initial(n)` | 0 | Where it starts, when resuming. Not counted in the rate. |
| `desc(s)`, `postfix(s)` | | Text before and after. Control characters become spaces. |
| `unit(s)` | `it` | |
| `scale(Scale)`, `bytes()` | `Scale::None` | `Scale::Si` gives 1.23M; `Scale::Binary` 1.18Mi; `bytes()` means binary with unit `B`. |
| `format(s)` | see below | The whole line. |
| `theme(t)` | `blocks`, or `ascii` if not UTF-8 | Also `classic`, `hashes`, `line`, `shades`, `dots`, or a `Theme` of your own. |
| `spinner(s)` | `dots`, or `ascii` if not UTF-8 | Also `arc`, or your own. |
| `color(c)`, `doneColor(c)`, `failedColor(c)` | none, same, red | `colors::kGreen`, `Color::index(208)`, `Color::rgb(255, 140, 0)`. |
| `barWidth(n)`, `maxWidth(n)` | fill the line | |
| `minInterval(ms)` | 100 ms | Least time between redraws. |
| `logInterval(ms)` | 30 s | Least time between lines, when not a terminal. |
| `smoothing(seconds)` | 5 | History behind the rate and estimate; 0 is the plain average. |
| `leave(bool)` | only if no other bar is live | Stay on screen when finished. |
| `reverse()` | | Bar and percentage empty out: a countdown. |
| `autoFinish(bool)` | true | Finish on reaching the total. Turn off if the total is still moving. |
| `disable()` | | Count, show nothing. |
| `display(d)` | stderr | |

### Bar

| | |
| --- | --- |
| `update(n = 1)`, `bar += n` | Count more. |
| `setCount(n)` | Jump to a value, up or down. |
| `setTotal(n)` | Change the total while running. |
| `setDescription(s)` | |
| `setPostfix(s)`, `setPostfix(key, value)` | A string, or an ordered `key=value` list of strings and numbers. |
| `finish()` | Done at the current count. The destructor does this if nobody did. |
| `fail(reason = "")` | Failed. An exception through the bar's scope does this too. |
| `reset(total = same)` | Start over, back on screen if it had finished. |
| `count()`, `total()`, `state()`, `finished()`, `snapshot()` | Look. |

Every call is safe from any thread, and none throws.

### Format strings

```cpp
tqdm::Options().format("{bold}{percent:.1}{reset} {bar:20} {n} of {total}, {gray}{remaining} to go{reset}");
```

| Placeholder | |
| --- | --- |
| `{desc}`, `{label}` | `label: `, with nothing at all when unset; the label alone. |
| `{percent}`, `{percent:.N}` | ` 42%`, ` 42.5%`. Steady width, floored: 100% means done. |
| `{bar}`, `{bar:N}` | Fills what the rest of the line leaves; or a fixed width. Bounces with no total. |
| `{n}`, `{total}`, `{remaining}` | Counts, scaled. `?` if not known. |
| `{n:unit}`, `{total:unit}`, `{remaining:unit}` | The same with the unit: `1834 file`, `215 MiB`. |
| `{elapsed}`, `{eta}` | `MM:SS`, `H:MM:SS`, `Nd HH:MM:SS`. `?` if not known. |
| `{rate}`, `{unit}` | `12.3 it/s`, or `2.50 s/it` when slower than one a second. |
| `{spinner}`, `{postfix}` | The frame; ` text`, with nothing at all when unset. |
| `{bold}` `{dim}` `{italic}` `{underline}` `{reverse}` `{reset}` | |
| `{red}` `{green}` `{yellow}` `{blue}` `{magenta}` `{cyan}` `{white}` `{gray}` | |
| `{{`, `}}` | Literal braces. |

The default is `{desc}{percent} {bar} {n}/{total} [{elapsed}<{eta}, {rate}]{postfix}`, or
`{desc}{spinner} {n:unit} [{elapsed}, {rate}]{postfix}` with no total. Anything unknown stays in the
line as written. A line that does not fit shrinks its bar first, then cuts the end.

### Environment

| | |
| --- | --- |
| `NO_COLOR` | No colors. Wins over the next two. |
| `FORCE_COLOR`, `CLICOLOR_FORCE` | Colors even when not on a terminal. |
| `TQDM_DISABLE=1` | Bars on the standard streams show nothing. |
| `TERM=dumb`, or unset | No redraw: log lines. |
| `LC_ALL`, `LC_CTYPE`, `LANG` | A UTF-8 one selects the block glyphs. Only read, never set. |

### Build options

| | |
| --- | --- |
| `TQDM_NO_THREADS` | Leave out the thread that keeps idle lines moving. Lines move only on update. |
| `TQDM_PLATFORM_GENERIC` | Standard C only: log lines, wherever it is compiled. |
| `TQDM_PLATFORM_HEADER` | Your own backend. |

Each is a macro, or a CMake option of the same name.

## Performance

Measured on one machine (aarch64, GCC 13.3, `-O2`). To reproduce: `-DTQDM_BUILD_BENCHMARKS=ON`, then
`benchmarks/bench_update`.

| | |
| --- | --- |
| One update, on a pseudo-terminal drained as fast as it fills | 4.07 ns |
| One update, on a terminal that discards the output | 4.1 ns |
| Output for 50,000,000 updates | 4 frames, 738 bytes |
| Compile time of a file using it | 0.99 s |
| Stripped size of a program using it | 133 KB |

The last two rows are the price: all of this is code, and a header-only library compiles it in each
file that uses it (0.31 s for one that only includes it). If it matters, keep the bars in a few files.

## Behavior

Each of these has a test in [`tests/`](tests/), most in [`test_regressions.cpp`](tests/test_regressions.cpp):

* **The hot path stays cheap:** one atomic add per update. Frames are rate-limited, whole, and unchanged ones
  are not sent.
* **The locale is left alone:** it has its own UTF-8 decoder, and any bytes are valid input.
* **Any glyphs work:** glyphs without width are replaced, and a bar is its exact width with any others.
* **Lines fit the terminal:** autowrap is off while drawing, and resizes are followed.
* **Any output:** one `Display` per output. The default is stderr, so stdout stays yours.
* **One kind of bar:** bars, counters and spinners differ only in a format string, and all draw on update.
* **Estimates cannot overflow:** doubles, clamped, and `?` when absurd.
* **The total can change:** `setTotal()`.
* **Messages and bars coexist:** `write()` and `ScopedRedirect`.
* **Pipes and files get log lines,** never escape codes.
* **Elapsed time starts with the bar,** not at its first tick.
* **The cursor is hidden only inside each single write,** so a crash leaves no state to restore and there is no
  signal handler.
* **A region taller than the terminal** shows what fits and says `... 7 more`.

## Limits

* **Order:** lines keep their order. A finished bar is not moved above the active ones.
* **Forking:** do not use a bar in a forked child without `exec`. As with any threads and locks, the
  child gets a copy of a lock that nobody will release.
* **`SIGPIPE`:** a stderr pipe whose reader has gone raises it, as any write there does, and the
  default action ends the program. Ignore it (`signal(SIGPIPE, SIG_IGN)`) and the bar stops drawing
  and nothing else happens. A library has no business changing signal dispositions.
* **Resizing:** rows are recomputed for terminals that rewrap on resize, which nearly all do. `xterm`
  clips instead, so narrowing it may overwrite a line above the bar. And a bar in the very first row of
  a screen leaves its rewrapped top behind in the scrollback, out of any program's reach.
* **Shared libraries:** the standard displays are function-local statics. Across shared libraries built
  with `-fvisibility=hidden` each gets its own, so bars from two of them will not stack.
* **Glyph widths** come from Unicode 15 and your terminal may disagree on the rare emoji. Autowrap being
  off keeps that from breaking the region; the worst case is a clipped line end.

## Building the tests

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/e2e_tmux.sh` runs the examples in a real terminal emulator, including resizes mid-run; it is
skipped if there is no `tmux`. On aarch64 kernels ThreadSanitizer may refuse to start: use
`setarch -R ctest ...`.

## License

Apache 2.0: see `LICENSE` and `NOTICE`.
