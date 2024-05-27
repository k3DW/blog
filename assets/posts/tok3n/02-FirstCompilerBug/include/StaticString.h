#pragma once
#include <algorithm>
#include <array>
#include <string_view>

template <std::size_t N>
struct StaticString
{
    std::array<char, N> data = {};

    constexpr StaticString(const char (&input)[N + 1])
    {
        std::ranges::copy_n(input, N, data.begin());
    }

    constexpr StaticString(char c) noexcept requires (N == 1)
	{
		data[0] = c;
	}
    
    constexpr std::string_view view() const
    {
        return std::string_view(data.data(), N);
    }

    constexpr bool contains(char c) const
    {
        return view().find(c) != std::string_view::npos;
    }
    
    constexpr std::size_t size() const noexcept
    {
        return N;
    }
};

template <std::size_t N>
StaticString(const char(&)[N]) -> StaticString<N - 1>;

StaticString(char) -> StaticString<1>;

constexpr bool is_sorted_and_uniqued(const char* arr, std::size_t N)
{
    if (N <= 1)
        return true;

    for (std::size_t i = 0; i != N - 1; ++i)
        if (arr[i + 1] <= arr[i])
            return false;
    return true;
}

template <StaticString str>
concept SortedAndUniqued = is_sorted_and_uniqued(str.data.data(), str.size());
