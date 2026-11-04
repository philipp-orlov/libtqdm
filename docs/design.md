# Design

## The layers

Each depends only on the ones above it in this list, and the first four know nothing of bars.

| Header | | |
| --- | --- | --- |
| `platform.hpp`, `platform/*` | The OS | Seven functions. No other header includes an OS header. |
| `unicode.hpp`, `unicode_tables.hpp` | Text | Decoding, cell widths, cutting. Generated tables. |
| `format.hpp` | Numbers | Durations, rates, prefixes. Locale-free, printf-free. |
| `theme.hpp`, `options.hpp` | Data | Plain values. |
| `render.hpp` | One line | Pure: format + `Snapshot` + width gives a string. |
| `sink.hpp` | Where bytes go | One virtual call per frame. |
| `display.hpp` | The region | Lines on one output, the frame builder, the ticker. Knows `Line`, not `Bar`. |
| `bar.hpp` | The counter | Atomics, rate estimate, lifecycle. |
| `group.hpp`, `iter.hpp`, `ostream.hpp` | Conveniences | Nothing here the layers below rely on. |

## The hot path

```cpp
void update(uint64_t n = 1) noexcept {
    const uint64_t value = count_.fetch_add(n, relaxed) + n;
    if (value >= nextCheck_.load(relaxed)) onCheck(value);
}
```

`nextCheck_` is the only thing that decides whether anything else happens. `onCheck` is out of line
and cold, reads the clock and aims `nextCheck_` at half a redraw interval ahead, from how fast the
count moved since the last check. So a 1 kHz loop checks on most updates, and a 100 MHz loop one update
in 4096, with the same small overhead. The two are on separate cache lines: every update writes
`count_`, and that must not disturb the readers of `nextCheck_`.

The cap of 4096 bounds how stale a bar can be when a fast loop suddenly turns slow, in a program without
the ticker. `nextCheck_` never passes the total, so the update that completes a bar is always seen.

Drawing takes the display's lock with `try_lock`. If another thread is already drawing, this update goes
back to work: a slow terminal holds up at most the one thread that happens to be writing to it.

## Locks

Two kinds, one order. A display's lock guards the region and the sink. A bar's lock guards its strings
and the rate estimate. `render()` takes the bar's lock under the display's, and the bar never takes
the display's while holding its own. So a setter (`setPostfix`) never waits for a terminal.

## A frame

Everything for one frame goes out in one `Sink::write`:

```
ESC[?25l            cursor off
ESC[nA CR           to the top of the region
message ESC[K CRLF  anything scrolling away above: write()'s lines, finished bars
ESC[?7l             autowrap off
line ESC[K CRLF     each line
line ESC[K
ESC[J               clear what a taller region left below
ESC[?7h ESC[?25h    autowrap and cursor back on
```

Three properties follow:

* **No torn frames.** A single `write()`, however many threads or processes.
* **Nothing to restore.** The cursor and autowrap are only changed inside that one write, so a crash or
  `kill -9` between frames leaves the terminal as it was. That is why there is no signal handler or
  `atexit` hook: there is nothing for one to do.
* **The region's height is knowable.** With autowrap off a line cannot take two rows, even when the width
  we measured is out of date or the terminal disagrees on a glyph. A wrong guess costs a clipped line end,
  not a trail of old bars.

If only some lines changed, only those are written; if none did, no bytes go out. The last line is always
written, so the cursor always ends at the end of the region: the resize arithmetic relies on it.

### Resizing

There is no `SIGWINCH` handler, since a library must not take signals. The width is read again for each
frame (one `ioctl`, ten times a second). When it has shrunk, the rows the last frame now takes are computed
from its text, and the cursor moves up that many.

Measured in tmux: the size and the rewrap change together, and the cursor stays on the last row of the
rewrapped text. tmux keeps the cursor's screen row fixed and scrolls the extra rows into its history, which
is why a bar on the very first row loses its top to the scrollback.

### A region taller than the terminal

The cursor cannot move above the top of the screen, so a taller region would corrupt itself on every frame.
Past `rows - 1` lines it shows what fits and a `... N more` line.

## Finished lines

A finished line that is staying on screen waits in the region until every line above it has finished too,
and scrolls away with them: the order on screen never changes. By then the `Bar` object may be long
gone (`Display::detach` renders it one last time and keeps only the text).

Finished lines that are not staying cause no frame of their own unless one is due, so a nested bar created
ten thousand times a second costs no more than 20 frames a second.

## The rate

Sampled at render time, so at most 10 Hz, as an exponential moving average weighted by elapsed time:
`rate += (1 - exp(-dt / tau)) * (instant - rate)`. Weighting by time and not by sample means the result
does not depend on how often frames happen to be drawn, and a stall pulls the rate down (the ticker keeps
sampling when nothing updates) so the estimate grows honestly. The final frame shows the plain average,
as the only number left that means anything.

## The ticker

A thread per display that has live lines on a real terminal, and no other. It starts with the first line,
is gone with the last, wakes as often as the fastest animation among the lines (at most 1 Hz otherwise),
and is what lets a spinner spin while the program is blocked.

`detach()` moves the thread out under the lock and joins after unlocking. A generation counter stops a
thread that was told to go while a new first line was attaching, which the simple `stop` flag would not.

The standard displays are never destroyed, so a static `Bar`'s destructor works at any point in shutdown.
A `Bar` holds a `shared_ptr` to its display, so no order of destruction can leave it dangling.

## Failure

Nothing about progress may be why a program fails. Every call on the path is `noexcept`: allocation failures
and exceptions from a sink are swallowed, and an output that fails once is marked broken. Then nothing is
drawn or kept, while counts still work. A bad format string renders as text. Any byte sequence is valid
text.

## Testing

`tests/screen.hpp` is a small terminal emulator, and most tests say what a person would see on it, not which
bytes were sent. It deliberately ignores `?7l`, so that a line that *would* have wrapped is still caught.
The clock is injected, so timing tests are exact.

`tests/e2e_tmux.sh` covers what no in-process test can: a real emulator's behavior on resize. It found the
wrapped-row trail that led to autowrap being off during frames.
