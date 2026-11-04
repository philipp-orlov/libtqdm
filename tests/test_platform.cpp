// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// The active backend against the contract in tqdm/platform.hpp, on real pipes
// and a real pseudo-terminal. Portable parts first: a new backend should pass
// these before anything else is tried on it. The POSIX parts wait their turn
// behind an #if; a Windows backend wants a ConPTY version of them.

#include <string>

#include "check.hpp"
#include "screen.hpp"
#include "tqdm/bar.hpp"
#include "tqdm/display.hpp"
#include "tqdm/platform.hpp"
#include "tqdm/sink.hpp"

using namespace tqdm;

TEST(contract_holds_for_the_standard_streams) {
    const auto out = platform::stdHandle(platform::StdStream::Output);
    const auto err = platform::stdHandle(platform::StdStream::Error);
    (void)platform::isTerminal(out);
    (void)platform::isTerminal(err);
    const auto size = platform::terminalSize(err);
    CHECK(size.columns >= 0 && size.rows >= 0);
    CHECK(platform::writeAll(err, "", 0));
}

TEST(get_env_tells_unset_from_set) { CHECK(platform::getEnv("TQDM_TEST_CERTAINLY_NOT_SET_ANYWHERE") == nullptr); }

TEST(the_standard_displays_are_one_each) {
    CHECK(Display::standardError() == Display::standardError());
    CHECK(Display::standardOutput() == Display::standardOutput());
    CHECK(Display::standardError() != Display::standardOutput());
}

TEST(a_handle_sink_answers) {
    HandleSink sink(platform::stdHandle(platform::StdStream::Error));
    const Capabilities capabilities = sink.capabilities();
    CHECK(!capabilities.ansi || platform::isTerminal(platform::stdHandle(platform::StdStream::Error)));
}

#if defined(__unix__) || defined(__APPLE__)
#if !defined(TQDM_PLATFORM_GENERIC) && !defined(TQDM_PLATFORM_HEADER)

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace {

struct Pty {
    int master = -1;
    int slave = -1;

    Pty(int columns, int rows) {
        master = ::posix_openpt(O_RDWR | O_NOCTTY);
        CHECK(master >= 0);
        CHECK_EQ(::grantpt(master), 0);
        CHECK_EQ(::unlockpt(master), 0);
        slave = ::open(::ptsname(master), O_RDWR | O_NOCTTY);
        CHECK(slave >= 0);
        struct winsize size {};
        size.ws_col = static_cast<unsigned short>(columns);
        size.ws_row = static_cast<unsigned short>(rows);
        CHECK_EQ(::ioctl(slave, TIOCSWINSZ, &size), 0);
    }
    ~Pty() {
        ::close(slave);
        ::close(master);
    }

    std::string drain() const {
        std::string out;
        char buffer[4096];
        struct pollfd wait {
            master, POLLIN, 0
        };
        while (::poll(&wait, 1, 50) > 0 && (wait.revents & POLLIN) != 0) {
            const ssize_t got = ::read(master, buffer, sizeof buffer);
            if (got <= 0)
                break;
            out.append(buffer, static_cast<size_t>(got));
        }
        return out;
    }
};

} // namespace

TEST(posix_a_terminal_is_a_terminal_and_a_pipe_is_not) {
    Pty pty(77, 19);
    int fds[2];
    CHECK_EQ(::pipe(fds), 0);
    CHECK(platform::isTerminal(pty.slave));
    CHECK(!platform::isTerminal(fds[1]));
    CHECK_EQ(platform::terminalSize(pty.slave).columns, 77);
    CHECK_EQ(platform::terminalSize(pty.slave).rows, 19);
    CHECK_EQ(platform::terminalSize(fds[1]).columns, 0);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(posix_term_and_locale_decide_what_is_used) {
    Pty pty(80, 24);
    ::setenv("TERM", "xterm-256color", 1);
    ::setenv("LC_ALL", "en_US.UTF-8", 1);
    ::unsetenv("NO_COLOR");
    ::unsetenv("FORCE_COLOR");
    ::unsetenv("CLICOLOR_FORCE");
    {
        const auto c = HandleSink(pty.slave).capabilities();
        CHECK(c.ansi);
        CHECK(c.color);
        CHECK(c.utf8);
    }
    ::setenv("LC_ALL", "C", 1);
    CHECK(!HandleSink(pty.slave).capabilities().utf8);
    ::setenv("LC_ALL", "en_US.utf8", 1);
    CHECK(HandleSink(pty.slave).capabilities().utf8);
    ::unsetenv("LC_ALL");
    ::setenv("LC_CTYPE", "C.UTF-8", 1);
    CHECK(HandleSink(pty.slave).capabilities().utf8);

    ::setenv("NO_COLOR", "1", 1);
    {
        const auto c = HandleSink(pty.slave).capabilities();
        CHECK(c.ansi);
        CHECK(!c.color);
    }
    ::setenv("FORCE_COLOR", "1", 1);
    CHECK(!HandleSink(pty.slave).capabilities().color);
    ::unsetenv("NO_COLOR");

    int fds[2];
    CHECK_EQ(::pipe(fds), 0);
    CHECK(!HandleSink(fds[1]).capabilities().ansi);
    CHECK(HandleSink(fds[1]).capabilities().color); // FORCE_COLOR, for a log that shows it.
    ::unsetenv("FORCE_COLOR");
    CHECK(!HandleSink(fds[1]).capabilities().color);
    ::close(fds[0]);
    ::close(fds[1]);

    ::setenv("TERM", "dumb", 1);
    CHECK(!HandleSink(pty.slave).capabilities().ansi);
    ::unsetenv("TERM");
    CHECK(!HandleSink(pty.slave).capabilities().ansi);
    ::setenv("TERM", "xterm", 1);
}

TEST(posix_a_bar_on_a_real_pty) {
    ::setenv("TERM", "xterm-256color", 1);
    ::setenv("LC_ALL", "C.UTF-8", 1);
    Pty pty(60, 20);
    {
        auto display = Display::create(std::make_shared<HandleSink>(pty.slave));
        Bar bar(Options().display(display).total(1000).desc("real").color(colors::kCyan));
        for (int i = 0; i < 1000; ++i) {
            bar.update();
            if (i % 100 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                display->write("step " + std::to_string(i));
            }
        }
    }
    check::Screen screen(60, 20);
    screen.feed(pty.drain());
    const auto lines = screen.lines();
    CHECK_EQ(lines.size(), size_t{11});
    CHECK_EQ(lines[0], "step 0");
    CHECK(lines[10].find("real: 100% |") == 0);
    CHECK(lines[10].find("1000/1000") != std::string::npos);
    CHECK_EQ(screen.wrappedLines(), 0);
    CHECK(!screen.cursorHidden());
}

TEST(posix_a_reader_that_goes_away_is_no_problem) {
    ::signal(SIGPIPE, SIG_IGN);
    int fds[2];
    CHECK_EQ(::pipe(fds), 0);
    ::close(fds[0]);
    {
        auto display = Display::create(std::make_shared<HandleSink>(fds[1]));
        Bar bar(Options().display(display).total(5));
        bar.update(5);
        display->write("to nobody");
        CHECK(bar.finished());
    }
    ::close(fds[1]);
    ::signal(SIGPIPE, SIG_DFL);
}

TEST(posix_big_frames_arrive_whole_through_a_pipe) {
    int fds[2];
    CHECK_EQ(::pipe(fds), 0);
    std::string received;
    std::thread reader([&] {
        char buffer[1024];
        for (ssize_t got; (got = ::read(fds[0], buffer, sizeof buffer)) > 0;)
            received.append(buffer, static_cast<size_t>(got));
    });
    const std::string big(1 << 20, 'x');
    CHECK(platform::writeAll(fds[1], big.data(), big.size()));
    ::close(fds[1]);
    reader.join();
    ::close(fds[0]);
    CHECK(received == big);
}

TEST(posix_a_full_non_blocking_output_gives_up_and_does_not_hang) {
    int fds[2];
    CHECK_EQ(::pipe(fds), 0);
    CHECK_EQ(::fcntl(fds[1], F_SETFL, ::fcntl(fds[1], F_GETFL) | O_NONBLOCK), 0);
    const std::string big(4 << 20, 'x');
    const auto start = std::chrono::steady_clock::now();
    CHECK(!platform::writeAll(fds[1], big.data(), big.size()));
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
    ::close(fds[0]);
    ::close(fds[1]);
}

#endif
#endif

int main(int argc, char** argv) { return runAll(argc, argv); }
