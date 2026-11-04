#!/bin/sh
# Copyright (c) 2026 Philipp Orlov
# SPDX-License-Identifier: Apache-2.0
#
# Real programs in a real terminal emulator: runs them in tmux and checks what
# ends up on its screen, including a resize mid-run, the one thing the in-process
# tests cannot do. Exit 77 (a skip for CTest) when there is no tmux.
#
#   e2e_tmux.sh <directory with the built examples>
set -u
examples=$1
command -v tmux >/dev/null 2>&1 || exit 77

run=0
socket="tqdm-e2e-$$-$run"
session=e2e
tm() { tmux -L "$socket" "$@"; }
trap 'tm kill-server 2>/dev/null' EXIT
failed=0

start() {  # width height command...
    width=$1; height=$2; shift 2
    tm kill-server 2>/dev/null
    # A server of its own for each: one still shutting down refuses connections.
    run=$((run + 1))
    socket="tqdm-e2e-$$-$run"
    tm -f /dev/null new-session -d -s "$session" -x "$width" -y "$height" \
        "env TERM=screen-256color LC_ALL=C.UTF-8 $*; echo FINISHED; sleep 100"
    tm set-option -g status off 2>/dev/null
}

wait_for() {  # text
    i=0
    while [ $i -lt 300 ]; do
        tm capture-pane -p -t "$session" 2>/dev/null | grep -q "$1" && return 0
        sleep 0.1
        i=$((i + 1))
    done
    return 1
}

expect() {  # what grep-pattern
    if ! tm capture-pane -p -S - -t "$session" | grep -q "$2"; then
        echo "FAIL $1: no match for [$2]"
        tm capture-pane -p -S - -t "$session" | sed 's/^/    /'
        failed=1
    else
        echo "ok   $1"
    fi
}

reject() {  # what grep-pattern
    if tm capture-pane -p -S - -t "$session" | grep -q "$2"; then
        echo "FAIL $1: unwanted match for [$2]"
        tm capture-pane -p -S - -t "$session" | sed 's/^/    /'
        failed=1
    else
        echo "ok   $1"
    fi
}

# One bar, then the shell's next line starts on a fresh row.
start 80 24 "$examples/quickstart"
wait_for FINISHED || { echo "FAIL quickstart did not finish"; failed=1; }
expect "quickstart: the final bar" 'files: 100% |'
expect "quickstart: fresh line after" '^FINISHED'
count=$(tm capture-pane -p -S - -t "$session" | grep -c 'files:')
[ "$count" = 1 ] && echo "ok   quickstart: redraws in place" || {
    echo "FAIL quickstart: $count rows of the same bar"; failed=1; }

# Stacked bars from threads, with messages going by above.
start 100 30 "$examples/threads"
wait_for FINISHED || { echo "FAIL threads did not finish"; failed=1; }
expect "threads: worker bars" 'layer-1.*100%'
expect "threads: messages above" 'finished layer-0'
expect "threads: last bar" 'layer-3.*100%'

# Narrow enough that a naive bar wraps on every frame.
start 40 24 "$examples/quickstart"
wait_for FINISHED || { echo "FAIL narrow did not finish"; failed=1; }
count=$(tm capture-pane -p -S - -t "$session" | grep -c 'files:')
[ "$count" = 1 ] && echo "ok   narrow: one row" || {
    echo "FAIL narrow: $count rows"; failed=1; }

# Resized smaller and bigger while running: no trail of old bars left behind.
# With lines above the bar, as any shell has: tmux scrolls a rewrapped first row
# into its history, and from the very top of a screen no program can reach it.
start 100 24 "sh -c 'echo one; echo two; echo three; echo four; $examples/quickstart 60'"
wait_for 'files:' || { echo "FAIL resize did not start"; failed=1; }
sleep 0.5
tm resize-window -t "$session" -x 50 -y 24
sleep 0.5
tm resize-window -t "$session" -x 90 -y 24
sleep 0.5
tm resize-window -t "$session" -x 40 -y 24
wait_for FINISHED || { echo "FAIL resize did not finish"; failed=1; }
count=$(tm capture-pane -p -S - -t "$session" | grep -c 'files:')
[ "$count" = 1 ] && echo "ok   resize: one row" || {
    echo "FAIL resize: $count rows"; tm capture-pane -p -S - -t "$session" | sed 's/^/    /'; failed=1; }

# Nothing moves on its own without the ticker; with it, a spinner spins.
start 80 24 "$examples/spinner"
wait_for 'Connecting' || { echo "FAIL spinner did not start"; failed=1; }
first=$(tm capture-pane -p -t "$session" | grep Connecting)
sleep 0.4
second=$(tm capture-pane -p -t "$session" | grep Connecting)
[ -n "$first" ] && [ "$first" != "$second" ] && echo "ok   spinner: moves without updates" || {
    echo "FAIL spinner: [$first] then [$second]"; failed=1; }
wait_for FINISHED || { echo "FAIL spinner did not finish"; failed=1; }

exit $failed
