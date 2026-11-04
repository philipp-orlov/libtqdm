// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// What a bar can look like: themes, colors, format strings, a theme of your own.

#include <chrono>
#include <string>
#include <thread>
#include <tqdm/tqdm.hpp>
#include <utility>
#include <vector>

int main() {
    const auto base = [] { return tqdm::Options().total(60).maxWidth(70); };

    std::vector<std::pair<std::string, tqdm::Theme>> themes = {
        {"blocks", tqdm::themes::blocks()}, {"ascii", tqdm::themes::ascii()}, {"classic", tqdm::themes::classic()},
        {"hashes", tqdm::themes::hashes()}, {"line", tqdm::themes::line()},   {"shades", tqdm::themes::shades()},
        {"dots", tqdm::themes::dots()}};

    tqdm::Theme own;
    own.left = "<";
    own.right = ">";
    own.fill = "\xF0\x9F\x8D\x95"; // Two cells each; the bar still fits exactly.
    own.empty = ".";
    themes.emplace_back("own", own);

    tqdm::Group group;
    for (auto& [name, theme] : themes) {
        group.add(base().desc(name).theme(theme).color(tqdm::colors::kCyan).doneColor(tqdm::colors::kGreen));
    }
    group.add(base()
                  .barWidth(16)
                  .color(tqdm::Color::rgb(255, 140, 0))
                  .format("{bold}{yellow}{percent:.1}{reset} {bar} {n} of {total}, "
                          "{gray}{remaining} to go{reset}"));
    group.add(base().spinner(tqdm::spinners::arc()).desc("spinner").format("{spinner} {desc}{eta} left"));

    for (int step = 0; step < 60; ++step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        for (size_t i = 0; i < group.size(); ++i)
            group[i].update();
    }
}
