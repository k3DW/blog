#include "AnyOf.h"
#include <iostream>
#include <cassert>

struct Wrapper
{
	constexpr Wrapper(int i) : value(i) {}
	int value;
};

template <auto>
concept True = true;

template <Wrapper wrapper>
requires True<wrapper>
struct Matcher
{
    static constexpr bool match(int i)
    {
        return i == wrapper.value;
    }
};

int main()
{
    [] {
        constexpr AnyOf<"01"> digit;
        static_assert(digit.match('0'));
        static_assert(digit.match('1'));
        static_assert(not digit.match('\0'));

        assert(not digit.match('0'));
        assert(not digit.match('1'));
        assert(digit.match('\0'));
    }();

    [] {
		constexpr Matcher<1> matcher;
		static_assert(matcher.match(1));
		assert(matcher.match(1)); // Fails here at run-time
	}();
}
