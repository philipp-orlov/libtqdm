// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

// Bars around loops: that the loop itself is not changed in any way.

#include <algorithm>
#include <cstdint>
#include <forward_list>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "support.hpp"
#include "tqdm/iter.hpp"

using namespace tqdm;
using check::Terminal;

namespace {

Options quiet() { return Options().disable(); }

} // namespace

TEST(items_are_the_items) {
    std::vector<int> v{1, 2, 3};
    for (auto& x : iter(v, quiet()))
        x *= 2;
    CHECK_EQ(v[2], 6);

    const std::vector<int> c{1, 2, 3};
    int sum = 0;
    for (const auto& x : iter(c, quiet()))
        sum += x;
    CHECK_EQ(sum, 6);

    std::map<std::string, int> m{{"a", 1}, {"b", 2}};
    for (auto& [key, value] : iter(m, quiet()))
        value += static_cast<int>(key.size());
    CHECK_EQ(m["b"], 3);

    int array[] = {1, 2, 3, 4};
    auto loop = iter(array, quiet());
    for (int& x : loop)
        x = 0;
    CHECK_EQ(array[3], 0);
    CHECK_EQ(loop.bar().total(), uint64_t{4});

    std::vector<bool> flags(3, false); // Proxies, not references.
    for (auto flag : iter(flags, quiet()))
        flag = true;
    CHECK(flags[1]);

    std::vector<std::unique_ptr<int>> owners;
    owners.push_back(std::make_unique<int>(5));
    for (auto& p : iter(owners, quiet()))
        CHECK_EQ(*p, 5);
}

TEST(a_temporary_lives_through_the_loop) {
    const auto make = [] { return std::vector<std::string>{"long enough to be on the heap", "b"}; };
    size_t total = 0;
    for (const auto& s : iter(make(), quiet()))
        total += s.size();
    CHECK_EQ(total, size_t{30});
}

TEST(total_comes_from_the_range) {
    std::vector<int> v(3);
    std::set<int> s{1, 2};
    std::list<int> l(4);
    std::forward_list<int> f(5);
    CHECK_EQ(iter(v, quiet()).bar().total(), uint64_t{3});
    CHECK_EQ(iter(s, quiet()).bar().total(), uint64_t{2});
    CHECK_EQ(iter(l, quiet()).bar().total(), uint64_t{4});
    CHECK_EQ(iter(f, quiet()).bar().total(), uint64_t{5});
    CHECK_EQ(iter(v, Options().disable().total(99)).bar().total(), uint64_t{99});
}

namespace {

// A range that can only be read once, and whose end is a different type.
struct Reader {
    std::istringstream stream;
    struct End {};
    struct It {
        using iterator_category = std::input_iterator_tag;
        using value_type = std::string;
        using difference_type = std::ptrdiff_t;
        using pointer = const std::string*;
        using reference = const std::string&;

        std::istringstream* stream;
        std::string word;
        bool more;

        const std::string& operator*() const { return word; }
        It& operator++() {
            more = static_cast<bool>(*stream >> word);
            return *this;
        }
        bool operator==(const End&) const { return !more; }
    };
    It begin() {
        It it{&stream, {}, false};
        ++it;
        return it;
    }
    End end() { return {}; }
};

} // namespace

TEST(a_single_pass_range_with_a_sentinel_has_no_total_and_still_finishes) {
    Terminal t;
    Reader reader{std::istringstream("one two three")};
    std::string joined;
    {
        auto loop = iter(reader, Options().display(t.display).format("{n}/{total}"));
        CHECK_EQ(loop.bar().total(), uint64_t{0});
        for (const std::string& word : loop)
            joined += word;
        CHECK(loop.bar().finished());
    }
    CHECK_EQ(joined, "onetwothree");
    CHECK_EQ(t.screen().lines().back(), "3/?");
}

TEST(the_count_follows_finished_items) {
    Terminal t;
    std::vector<int> v(10);
    auto loop = iter(v, Options().display(t.display));
    uint64_t expected = 0;
    for (int& x : loop) {
        (void)x;
        CHECK_EQ(loop.bar().count(), expected);
        ++expected;
    }
    CHECK_EQ(loop.bar().count(), uint64_t{10});
    CHECK(loop.bar().finished());
}

TEST(a_break_shows_what_was_done) {
    Terminal t;
    std::vector<int> v(10);
    {
        int seen = 0;
        for (int& x : iter(v, Options().display(t.display).format("{n}/{total}"))) {
            (void)x;
            if (++seen == 4)
                break;
        }
    }
    CHECK_EQ(t.screen().lines().back(), "3/10");
}

TEST(an_exception_leaves_a_failed_bar) {
    Terminal t(80, 24, false, true);
    std::vector<int> v(10);
    try {
        for (int& x : iter(v, Options().display(t.display).theme(themes::hashes()))) {
            (void)x;
            throw std::runtime_error("boom");
        }
    } catch (const std::runtime_error&) {
    }
    CHECK(t.sink->text().find("\x1b[31m") != std::string::npos);
}

TEST(an_empty_range_is_fine) {
    Terminal t;
    std::vector<int> v;
    int runs = 0;
    for (int& x : iter(v, Options().display(t.display).format("{n}"))) {
        (void)x;
        ++runs;
    }
    CHECK_EQ(runs, 0);
    CHECK_EQ(t.screen().lines().back(), "0");
}

TEST(works_with_algorithms_when_begin_and_end_match) {
    std::vector<int> v{1, 2, 3, 4};
    auto loop = iter(v, quiet());
    std::for_each(loop.begin(), loop.end(), [](int& x) { x += 1; });
    CHECK_EQ(std::accumulate(v.begin(), v.end(), 0), 14);
}

namespace {

template <typename R>
std::vector<long long> allOf(R&& range) {
    std::vector<long long> out;
    for (auto value : range)
        out.push_back(static_cast<long long>(value));
    return out;
}

using Longs = std::vector<long long>;

} // namespace

TEST(range_counts) {
    CHECK(allOf(range(3, quiet())) == Longs({0, 1, 2}));
    CHECK(allOf(range(2, 5, quiet())) == Longs({2, 3, 4}));
    CHECK(allOf(range(0, 10, 3, quiet())) == Longs({0, 3, 6, 9}));
    CHECK(allOf(range(5, 0, -2, quiet())) == Longs({5, 3, 1}));
    CHECK(allOf(range(0, quiet())).empty());
    CHECK(allOf(range(-3, quiet())).empty());
    CHECK(allOf(range(5, 2, quiet())).empty());
    CHECK(allOf(range(0, 5, 0, quiet())).empty());
    CHECK(allOf(range(0, 5, -1, quiet())).empty());
    CHECK(allOf(range(size_t{2}, quiet())) == Longs({0, 1}));
    CHECK(allOf(range(1, uint64_t{3}, quiet())) == Longs({1, 2}));
}

TEST(options_may_be_an_lvalue) {
    const Options constant = quiet();
    Options mutableOne = quiet();
    CHECK_EQ(range(4, constant).bar().total(), uint64_t{4});
    CHECK_EQ(range(4, mutableOne).bar().total(), uint64_t{4});
    CHECK_EQ(range(1, 4, mutableOne).bar().total(), uint64_t{3});
}

namespace {

// Dereferencing changes something, as some generators and cursors do.
struct Cursor {
    int left = 3;
    struct End {};
    struct It {
        using iterator_category = std::input_iterator_tag;
        using value_type = int;
        using difference_type = std::ptrdiff_t;
        using pointer = int*;
        using reference = int;

        int* left;
        int reads = 0;

        int operator*() { return ++reads + *left; }
        It& operator++() {
            --*left;
            return *this;
        }
        bool operator==(const End&) const { return *left == 0; }
    };
    It begin() { return It{&left}; }
    End end() { return {}; }
};

} // namespace

TEST(an_iterator_may_need_to_be_mutable_to_read) {
    Cursor cursor;
    int runs = 0;
    for (int value : iter(cursor, quiet())) {
        CHECK(value > 0);
        ++runs;
    }
    CHECK_EQ(runs, 3);
}

TEST(range_totals) {
    CHECK_EQ(range(1000, quiet()).bar().total(), uint64_t{1000});
    CHECK_EQ(range(0, 10, 3, quiet()).bar().total(), uint64_t{4});
    CHECK_EQ(range(10, 0, -5, quiet()).bar().total(), uint64_t{2});
}

TEST(range_at_the_far_ends_of_a_type) {
    using L = std::numeric_limits<int>;
    CHECK(allOf(range(L::max() - 2, L::max(), quiet())) == Longs({L::max() - 2, L::max() - 1}));
    CHECK(allOf(range(L::min(), L::max(), L::max(), quiet())) == Longs({L::min(), -1, L::max() - 1}));
    CHECK(allOf(range(L::max(), L::min(), L::min(), quiet())) == Longs({L::max(), -1}));
    CHECK_EQ(range(L::min(), L::max(), 1, quiet()).bar().total(), uint64_t{4294967295u});
    using U = std::numeric_limits<uint64_t>;
    CHECK_EQ(range(U::max(), quiet()).bar().total(), U::max());
    CHECK_EQ(range<int8_t>(-128, 127, 1, quiet()).bar().total(), uint64_t{255});
    CHECK_EQ(range<short>(-32768, 32767, 1, quiet()).bar().total(), uint64_t{65535});
}

TEST(range_reaches_the_bar) {
    Terminal t;
    auto loop = range(3, Options().display(t.display).format("{n}{postfix}"));
    for (int i : loop)
        loop.bar().setPostfix("i", static_cast<double>(i));
    CHECK_EQ(t.screen().lines().back(), "3 i=2");
}

int main(int argc, char** argv) { return runAll(argc, argv); }
