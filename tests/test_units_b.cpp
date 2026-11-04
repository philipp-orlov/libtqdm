// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// What windows.h does to a program that did not ask for NOMINMAX, and a few more
// of its kind: none of it may get in the way, whichever header comes first.
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define small char
#define near
#define far
#define IN
#define OUT
#define ERROR 0
#define OPTIONAL
#define DELETE 0x00010000L
#define interface struct

#include "tqdm/bar.hpp"
#include "tqdm/display.hpp"
#include "tqdm/format.hpp"
#include "tqdm/group.hpp"
#include "tqdm/iter.hpp"
#include "tqdm/platform.hpp"
#include "tqdm/render.hpp"
#include "tqdm/sink.hpp"
#include "tqdm/theme.hpp"
#include "tqdm/unicode.hpp"

const void* displayInUnitB() { return tqdm::Display::standardError().get(); }

int loopInUnitB() {
    int sum = 0;
    for (int i : tqdm::range(3, tqdm::Options().disable()))
        sum += i;
    return sum;
}
