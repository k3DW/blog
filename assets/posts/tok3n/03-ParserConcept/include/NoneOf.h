#pragma once
#include "StaticString.h"

template <StaticString str>
requires SortedAndUniqued<str>
struct NoneOf
{
    static constexpr bool parse(std::string_view input)
    {
        return not input.empty() && not str.contains(input.front());
    }
};