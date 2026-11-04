// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Two translation units including everything: header-only means no duplicate
// symbols, and one standard display between them.

#include "check.hpp"
#include "tqdm/tqdm.hpp"

const void* displayInUnitB();
int loopInUnitB();

TEST(the_same_display_in_both_units) { CHECK(tqdm::Display::standardError().get() == displayInUnitB()); }

TEST(both_units_can_loop) { CHECK_EQ(loopInUnitB(), 3); }

int main(int argc, char** argv) { return runAll(argc, argv); }
