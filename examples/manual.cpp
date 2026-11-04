// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// A bar driven by hand, reporting a value as it goes, in a training-loop style.

#include <chrono>
#include <thread>
#include <tqdm/tqdm.hpp>

int main() {
    tqdm::Bar bar(tqdm::Options().total(200).desc("training").unit("batch").color(tqdm::colors::kCyan));

    double loss = 2.5;
    for (int batch = 0; batch < 200; batch += 4) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        loss *= 0.97;
        bar.setPostfix("loss", loss);
        bar.setPostfix("lr", 0.001);
        bar.update(4);
    }
}
