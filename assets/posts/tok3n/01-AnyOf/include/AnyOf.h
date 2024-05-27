#pragma once
#include "StaticString.h"

template <StaticString str>
requires SortedAndUniqued<str>
struct AnyOf
{
    static constexpr bool parse(std::string_view input)
    {
        return not input.empty() && str.contains(input.front());
    }
};