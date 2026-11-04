// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// TQDM_COLD marks what runs a few times a second at most: rendering, painting,
// construction. Kept out of line, these do not get compiled again into every
// call site, which is most of what a header-only library costs in build time and
// binary size, and keeps update() itself down to a few instructions in a loop.
#if defined(__GNUC__) || defined(__clang__)
#define TQDM_COLD __attribute__((noinline, cold))
#elif defined(_MSC_VER)
#define TQDM_COLD __declspec(noinline)
#else
#define TQDM_COLD
#endif
