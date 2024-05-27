#pragma once
#include <algorithm>
#include <array>

template <std::size_t N>
struct StaticString
{
    std::array<char, N> data = {};

    constexpr StaticString(const char (&input)[N + 1])
    {
        std::ranges::copy_n(input, N, data.begin());
    }
};

template <std::size_t N>
StaticString(const char(&)[N]) -> StaticString<N - 1>;
