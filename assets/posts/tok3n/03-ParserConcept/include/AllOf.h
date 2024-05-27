#pragma once
#include "StaticString.h"

template <StaticString str>
struct AllOf
{
    static constexpr bool parse(std::string_view input)
    {
        return input.starts_with(str.view());
    }
};