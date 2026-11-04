// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Printing while a bar runs. tqdm::write() for new code; ScopedRedirect for code
// that already prints to a stream and should not have to change.

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <tqdm/tqdm.hpp>

static void legacyCodeThatPrints(int i) { std::cout << "legacy code says " << i << '\n'; }

int main() {
    tqdm::Bar bar(tqdm::Options().total(30).desc("work"));

    tqdm::ScopedRedirect redirect(std::cout, tqdm::platform::StdStream::Output);
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (i % 10 == 0)
            tqdm::write("checkpoint " + std::to_string(i));
        if (i % 10 == 5)
            legacyCodeThatPrints(i);
        bar.update();
    }
}
