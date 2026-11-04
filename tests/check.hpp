// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The smallest thing that can be called a test framework, so that the tests
// need nothing the library does not.
//
//   TEST(name) { CHECK(a == b); CHECK_EQ(text, "expected"); }
//   int main() { return runAll(); }

#include <cstdio>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Case {
    const char* name;
    void (*function)();
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, void (*function)()) { cases().push_back({name, function}); }
};

inline void fail(const char* file, int line, const std::string& message) {
    ++failures();
    std::fprintf(stdout, "  %s:%d: %s\n", file, line, message.c_str());
}

template <typename A, typename B>
void equal(const char* file, int line, const char* expression, const A& a, const B& b) {
    if (!(a == b)) {
        std::ostringstream message;
        message << expression << "\n    got      [" << a << "]\n    expected [" << b << "]";
        fail(file, line, message.str());
    }
}

} // namespace check

#define CHECK(condition)                                              \
    do {                                                              \
        if (!(condition)) {                                           \
            check::fail(__FILE__, __LINE__, "CHECK(" #condition ")"); \
        }                                                             \
    } while (false)

#define CHECK_EQ(a, b) check::equal(__FILE__, __LINE__, #a " == " #b, (a), (b))

#define TEST(name)                                                     \
    static void test_##name();                                         \
    static const check::Register register_##name(#name, &test_##name); \
    static void test_##name()

// Runs every test, or those whose name holds argv[1].
inline int runAll(int argc = 0, char** argv = nullptr) {
    int ran = 0;
    for (const check::Case& test : check::cases()) {
        if (argc > 1 && std::strstr(test.name, argv[1]) == nullptr)
            continue;
        ++ran;
        const int before = check::failures();
        try {
            test.function();
        } catch (const std::exception& e) {
            check::fail(test.name, 0, std::string("threw: ") + e.what());
        } catch (...) {
            check::fail(test.name, 0, "threw");
        }
        std::fprintf(stdout, "%s %s\n", check::failures() == before ? "ok  " : "FAIL", test.name);
    }
    std::fprintf(stdout, "%d tests, %d failed checks\n", ran, check::failures());
    return check::failures() == 0 ? 0 : 1;
}
