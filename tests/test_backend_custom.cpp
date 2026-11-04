// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Built with -DTQDM_PLATFORM_HEADER="custom_platform.hpp": the whole library on
// a backend it has never heard of, through the default displays.

#include <string>

#include "check.hpp"
#include "screen.hpp"
#include "tqdm/tqdm.hpp"

using namespace tqdm;

TEST(everything_goes_through_the_backend) {
    {
        for (int i : range(3, Options().total(3)))
            (void)i;
        tqdm::write("hello");
    }
    CHECK(platform::fakePrepared);
    check::Screen screen(50, 10);
    screen.feed(platform::fakeError.written);
    const auto lines = screen.lines();
    CHECK_EQ(lines.size(), size_t{2});
    CHECK(lines[0].find("100% |") == 0); // UTF-8 theme: the backend said so.
    CHECK(lines[0].find("\xE2\x96\x88") != std::string::npos);
    CHECK_EQ(lines[1], "hello");
    CHECK(unicode::displayWidth(lines[0]) <= 49); // And its width was used.
    CHECK(platform::fakeOutput.written.empty());
}

TEST(standard_output_is_there_too) {
    {
        Bar bar(Options().display(Display::standardOutput()).total(1).format("{n}"));
        bar.update();
    }
    CHECK(platform::fakeOutput.written.find('1') != std::string::npos);
}

int main(int argc, char** argv) { return runAll(argc, argv); }
