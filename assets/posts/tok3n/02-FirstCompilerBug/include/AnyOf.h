#pragma once
#include "StaticString.h"

template <StaticString str>
requires SortedAndUniqued<str>
struct AnyOf
{
	static constexpr bool match(char c)
	{
		return str.contains(c);
	}

    static constexpr auto the_string = str;
};
