// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Waiting on something that gives no numbers. The spinner moves by itself while
// this thread sleeps, and the line ends with a check mark, or a cross.

#include <chrono>
#include <thread>
#include <tqdm/tqdm.hpp>

int main() {
    {
        tqdm::Bar spinner(tqdm::Options::spinning("Connecting"));
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        spinner.setDescription("Connected");
    }
    {
        tqdm::Bar spinner(tqdm::Options::spinning("Authorizing"));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        spinner.fail("token expired");
    }
    {
        // Counting toward an unknown end: a count, a rate and a spinner.
        tqdm::Bar bar(tqdm::Options().desc("scanned").unit("file"));
        for (int i = 0; i < 300; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            bar.update();
        }
    }
}
