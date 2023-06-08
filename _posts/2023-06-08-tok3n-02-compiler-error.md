---
title: "My first compiler bug"
layout: post
permalink: /FirstCompilerBug/
---

I talked about this in a lightning talk at C++Now 2023 (link TBA). I want to go into more detail here.

It's exciting in a masochistic way I guess. A sort of self-schadenfreude. Somehow I found a bug in a major compiler! Frustrating for my workflow obviously, but it feels like a compliment, in a strange way.

Long story short: I hit a bug in MSVC where (spoiler) a non-type template parameter gets zeroed out at run-time but not at compile-time. Long story long: let's talk about how I got there.

<!--more-->

I still need more articles before I get my library decently functional. But in real life, after my library had some function, I was using it to parse input for [Advent of Code](https://adventofcode.com/). Before switching to my library, my AOC workflow involved doing all the parsing in an immediately invoked lambda expression, then returning the parsed data into a const variable. I like to keep const correctness, so this felt like the right strategy for me.

I switched to using my library and everything worked just fine. Until it didn't. After switching a few days' problems, suddenly I hit some errors. I tried troubleshooting by changing my strategy -- maybe I mistyped somewhere? -- but I eventually gave that up. The setup felt pretty normal. It looked like the following.

```cpp
static const auto input = read_input_file();
static const auto data = []
{
    constexpr OneChar<"0123456789"> digit;
    constexpr auto parser = /* something with digit */;
    auto result = parser.parse(input);
    if (not result)
        std::unreachable();
    return *result;
}();
```

I was expecting to hit the `return` statement, but it hit `std::unreachable()`. I stumbled around for something to fix it.

<br>

## Troubleshooting

I observed a few things. First, moving this logic into its own named function instead of a lambda solved the problem. Moving this logic into the enclosing scope also solved the problem. So it only happens in a lambda.

Next, without changing anything else, declaring `digit` outside the lambda solved the problem. Even werider, declaring a different variable with the same type as `digit` in the enclosing scope *also* solved the problem, but only if it was before the lambda. So I'm assuming this bug is about where the class template instantiation first appears. Very strange.

I got ahead of myself though. I didn't yet know what was actually going wrong. Instead of parsing the entire input with something derived from `digit`, let's parse some simple examples with `digit` itself.

```cpp
[] {
    constexpr OneChar<"0123456789"> digit;
    static_assert(digit.parse("0"));
    static_assert(digit.parse("1"));
    // etc...
}()
```

This compiles completely fine. So obviously there's nothing wrong with `digit`. [Or is there](https://www.google.com/search?q=jake+chudnow+moon+men)?

On a whim, I changed my testing style. How about we try run-time tests with the C `assert` macro?

```cpp
[] {
    constexpr OneChar<"0123456789"> digit;
    static_assert(digit.parse("0"));
    assert(digit.parse("0"));
    static_assert(digit.parse("1"));
    assert(digit.parse("1"));
    // etc...
}()
```

I came to a horrifying conclusion. The compile-time tests were passing, but the run-time tests were failing. But `OneChar` is stateless right? Right??

Wrong, there's one piece of state.

<br>

## `OneChar`'s unexpected statefulness

`OneChar` does have state, but it's not modifiable. Its template parameter determines its behaviour. Is something happening to the template parameter, to make the function behave differently? Is that possible?

It took a few tries to expose the string parameter at run-time. I settled on a new variable in `OneChar` that aliases its `StaticString` template parameter, and I'm calling it `the_string`.

```cpp
[] {
    constexpr OneChar<"01"> digit;
    static_assert(digit.parse("0"));
    for (char c : digit.the_string.data)
        std::cout << "Char - " << c << "\n";
    std::cout << "Size = " << digit.the_string.data.size() << "\n";
}();
```

Here is the printout:

```
Char - 0
Char - 1
Char -
Size = 3
```

What? It works as expected? But if I add `assert(digit.parse("0"));` then it still fails. Let's try one more thing. We need to simplify this example.

<br>

## Minimally reproducible

If I'm going to file a bug report, I need to narrow this down. Let's simplify `OneChar` with a made-up function `match()` for testing.

```cpp
template <StaticString str>
requires (is_sorted_and_uniqued(str))
struct OneChar
{
	static constexpr bool match(char c)
	{
		return str.contains(c);
	}
};

[] {
	constexpr OneChar<"01"> digit;
	static_assert(digit.match('0'));
	static_assert(digit.match('1'));
	static_assert(not digit.match('\0'));

	assert(not digit.match('0'));
	assert(not digit.match('1'));
	assert(digit.match('\0'));
}();
```

Ahh yes here it is. We got to the root of the problem. The template parameter is getting zeroed out at run-time, but left intact at compile-time. Let's reproduce it with even less code. Can we remove the constraint on the `OneChar` class template? No, that stops the problem from happening. But can we simplify the constraint? As it turns out, yes. In fact, as far as I can tell, it can be any constraint. So let's make it completely trivial.

```cpp
template <auto value>
concept True = true;

template <StaticString str>
requires True<str>
struct OneChar
{
    // ...
};
```

That helps. How about the NTTP? Can `StaticString` be replaced with a primitive type? No, it seems like it needs to be a class NTTP. But how simple can it get? This is what I came up with. It's just a simple wrapper around 1 `int`.

```cpp
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
```

From what I can tell, this is the minimal example of this bug. Then it must be used in a lambda. Then this specialization of `Matcher` must be instantiated for the first time inside of the lambda. Clearly this is a very specific bug. 

```cpp
int main()
{
    [] {
		constexpr Matcher<1> matcher;
		static_assert(matcher.match(1));
		assert(matcher.match(1)); // Fails here at run-time
	}();
}
```

And that's it! As of Visual Studio 17.6.2, the most recent update as of the time of writing, this is still a bug. I hope it gets fixed at some point. That said, this problem coincidentally goes away once I introduce other preferred syntax into the library, but we'll get there.

Next up, I want to expand into more than just `OneChar`. It'll be a short one about a couple other types of parsers.
