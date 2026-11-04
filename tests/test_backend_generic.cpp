// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Built with TQDM_PLATFORM_GENERIC: what any platform gets before it has a
// backend of its own, Windows at the moment. No terminal control, ever.

#include <cstdio>
#include <string>

#include "check.hpp"
#include "tqdm/tqdm.hpp"

using namespace tqdm;

TEST(the_generic_backend_is_a_log_of_ascii_lines) {
    std::FILE* file = std::tmpfile();
    CHECK(file != nullptr);
    CHECK(!platform::isTerminal(file));
    CHECK_EQ(platform::terminalSize(file).columns, 0);
    CHECK(!platform::enableAnsi(file));
    CHECK(!platform::outputIsUtf8(file));
    {
        auto display = Display::create(std::make_shared<HandleSink>(file));
        Bar bar(Options().display(display).total(4).desc("generic").barWidth(4));
        bar.update(4);
        display->write("note");
    }
    std::rewind(file);
    char buffer[256] = {};
    const size_t got = std::fread(buffer, 1, sizeof buffer - 1, file);
    std::fclose(file);
    const std::string text(buffer, got);
    CHECK(text.find("generic: 100% |####| 4/4") == 0);
    CHECK(text.find("\nnote\n") != std::string::npos);
    CHECK(text.find('\x1b') == std::string::npos);
    CHECK(text.find('\r') == std::string::npos);
}

TEST(the_standard_displays_work) {
    Bar bar(Options().total(1).format("generic stderr {n}"));
    bar.update();
    CHECK(bar.finished());
}

int main(int argc, char** argv) { return runAll(argc, argv); }
