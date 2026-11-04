// Copyright (c) 2026 Philipp Orlov
// SPDX-License-Identifier: Apache-2.0

#pragma once

// A bar around a loop:
//
//   for (auto& item : tqdm::iter(items)) { ... }
//   for (int i : tqdm::range(1000)) { ... }
//
// The total comes from the range if it knows (or can cheaply find out), and a
// temporary is kept alive for the whole loop. Items pass through untouched, by
// reference where the range gives references. The count moves when the body of
// the loop is done with an item, so a `break` shows only finished work; the bar
// ends with the loop, and an exception thrown out of it marks the bar as failed.
//
// To reach the bar from inside the loop, name the wrapper:
//
//   auto loop = tqdm::iter(items);
//   for (auto& item : loop) { loop.bar().setPostfix("name", item.name); }

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

#include "tqdm/bar.hpp"
#include "tqdm/options.hpp"

namespace tqdm {

namespace detail {

template <typename R, typename = void>
struct HasSize : std::false_type {};
template <typename R>
struct HasSize<R, std::void_t<decltype(std::size(std::declval<R&>()))>> : std::true_type {};

template <typename I, typename = void>
struct MultiPass : std::false_type {};
template <typename I>
struct MultiPass<I, std::void_t<typename std::iterator_traits<I>::iterator_category>>
    : std::is_base_of<std::forward_iterator_tag, typename std::iterator_traits<I>::iterator_category> {};

template <typename R>
uint64_t measure(R& range) {
    if constexpr (HasSize<R>::value) {
        return static_cast<uint64_t>(std::size(range));
    } else {
        using std::begin;
        using std::end;
        using First = decltype(begin(range));
        if constexpr (MultiPass<First>::value && std::is_same_v<First, decltype(end(range))>)
            return static_cast<uint64_t>(std::distance(begin(range), end(range)));
        else
            return 0;
    }
}

template <typename S>
struct Sentinel {
    S end;
};

template <typename I>
class Iterator {
public:
    using iterator_category = std::input_iterator_tag;
    using value_type = typename std::iterator_traits<I>::value_type;
    using difference_type = typename std::iterator_traits<I>::difference_type;
    using pointer = typename std::iterator_traits<I>::pointer;
    using reference = typename std::iterator_traits<I>::reference;

    Iterator(I it, Bar* bar) : it_(std::move(it)), bar_(bar) {}

    decltype(auto) operator*() { return *it_; }
    decltype(auto) operator*() const { return *it_; }
    I operator->() const { return it_; }
    Iterator& operator++() {
        ++it_;
        bar_->update();
        return *this;
    }
    void operator++(int) { ++*this; }

    bool operator==(const Iterator& other) const { return atEnd(other.it_); }
    bool operator!=(const Iterator& other) const { return !atEnd(other.it_); }
    template <typename S>
    bool operator==(const Sentinel<S>& other) const {
        return atEnd(other.end);
    }
    template <typename S>
    bool operator!=(const Sentinel<S>& other) const {
        return !atEnd(other.end);
    }

private:
    // The end of a range with no total is the only way to know it is over.
    template <typename S>
    bool atEnd(const S& end) const {
        const bool done = it_ == end;
        if (done && bar_ != nullptr)
            bar_->finish();
        return done;
    }

    I it_;
    Bar* bar_;
};

template <typename T>
class IntSequence {
public:
    IntSequence(T first, T last, T step) : first_(first), step_(step) {
        using U = std::make_unsigned_t<T>;
        if (step > 0 && first < last) {
            const U span = static_cast<U>(static_cast<U>(last) - static_cast<U>(first));
            count_ = static_cast<uint64_t>(span / static_cast<U>(step)) + (span % static_cast<U>(step) != 0 ? 1 : 0);
        } else if (step < 0 && first > last) {
            const U span = static_cast<U>(static_cast<U>(first) - static_cast<U>(last));
            const U down = static_cast<U>(U{0} - static_cast<U>(step));
            count_ = static_cast<uint64_t>(span / down) + (span % down != 0 ? 1 : 0);
        }
    }

    class const_iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = T;

        const_iterator(const IntSequence* sequence, uint64_t index) : sequence_(sequence), index_(index) {}
        // Modular arithmetic on the way, so the far end of a type is not overflow.
        T operator*() const {
            using U = std::make_unsigned_t<T>;
            return static_cast<T>(static_cast<U>(sequence_->first_) +
                                  static_cast<U>(index_) * static_cast<U>(sequence_->step_));
        }
        const_iterator& operator++() {
            ++index_;
            return *this;
        }
        bool operator==(const const_iterator& other) const { return index_ == other.index_; }
        bool operator!=(const const_iterator& other) const { return index_ != other.index_; }

    private:
        const IntSequence* sequence_;
        uint64_t index_;
    };

    const_iterator begin() const { return {this, 0}; }
    const_iterator end() const { return {this, count_}; }
    uint64_t size() const { return count_; }

private:
    T first_;
    T step_;
    uint64_t count_ = 0;
};

} // namespace detail

// What iter() and range() return. R is a reference for a range that outlives
// the loop, and a value for a temporary, kept here. Neither copied nor moved:
// the iterators point into it.
template <typename R>
class Iterable {
public:
    Iterable(R&& range, const Options& options) : range_(std::forward<R>(range)), bar_(withTotal(options)) {}

    Iterable(const Iterable&) = delete;
    Iterable& operator=(const Iterable&) = delete;

    auto begin() {
        using std::begin;
        using First = decltype(begin(range_));
        return detail::Iterator<First>(begin(range_), &bar_);
    }

    auto end() {
        using std::begin;
        using std::end;
        using First = decltype(begin(range_));
        using Last = decltype(end(range_));
        if constexpr (std::is_same_v<First, Last>)
            return detail::Iterator<First>(end(range_), nullptr);
        else
            return detail::Sentinel<Last>{end(range_)};
    }

    Bar& bar() noexcept { return bar_; }

private:
    Options withTotal(Options options) {
        if (options.values().total == 0)
            options.total(detail::measure(range_));
        return options;
    }

    R range_;
    Bar bar_;
};

template <typename R>
Iterable<R> iter(R&& range, const Options& options = {}) {
    return Iterable<R>(std::forward<R>(range), options);
}

// The integers from `first` up to, not including, `last`, every `step`; down if
// `step` is negative. Any integer type.
template <typename T>
auto range(T first, T last, T step, const Options& options = {}) {
    static_assert(std::is_integral_v<T>, "tqdm::range counts in integers");
    return Iterable<detail::IntSequence<T>>(detail::IntSequence<T>(first, last, step), options);
}

template <typename A, typename B, std::enable_if_t<std::is_integral_v<A> && std::is_integral_v<B>, int> = 0>
auto range(A first, B last, const Options& options = {}) {
    using T = std::common_type_t<A, B>;
    return range<T>(static_cast<T>(first), static_cast<T>(last), T{1}, options);
}

template <typename T>
auto range(T count, const Options& options = {}) {
    return range<T>(T{0}, count, T{1}, options);
}

} // namespace tqdm
