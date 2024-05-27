---
title: "The Parser concept (+ NoneOf and AllOf)"
layout: post
permalink: /ParserConcept/
tags: [ tok3n ]
---

Continuing on my [series](/tags/tok3n/) on my expression template parser generator library...

So far I've defined the `AnyOf` parser, that checks whether the first character of the input string matches any characters from a given set. This is one of my "primitive" parsers. I've thought of 2 other "primitives" to act as a basis for the library: `NoneOf` and `AllOf`. After that, let's expand on the idea of a "Parser" in general.

<!--more-->

Later edit: I previously called these parsers `OneChar`, `NotChar`, and `Literal` respectively, but I've since changed them.

<br>

## `NoneOf` and `AllOf`, simple extensions of `AnyOf`

The idea here is simple. Instead of checking whether the first character matches any characters in the given set, we'll check whether the first character *doesn't* match any characters in the given set. Instead I could add another template parameter to the `AnyOf` type to make it do this, but I prefer having these as separate types. For now the code will be duplicated, but I'll remove duplication later.

Right now, I'll copy and paste all of `AnyOf`, but change the condition.

```cpp
template <StaticString str>
requires SortedAndUniqued<str>
struct NoneOf
{
    static constexpr bool parse(std::string_view input)
    {
        return not input.empty() && not str.contains(input.front());
    }
};
```
The only difference here is `not str.contains(...)` instead of `str.contains(...)`. Otherwise it's the same.

I also want a parser that checks that the input string starts with a given substring. This change is equally as simple as `NoneOf`. Just change the condition.

```cpp
template <StaticString str>
struct AllOf
{
    static constexpr bool parse(std::string_view input)
    {
        return input.starts_with(str.view());
    }
};
```

This time, we shouldn't make sure the parser's string is sorted and uniqued. This substring to match can obviously be anything, with repeated characters that aren't in order.

<br>

## Conceptualizing the "Parser"

At this point, I have 3 unrelated (in terms of inheritance) struct templates, but I want to use them in identical scenarios. I'm using C++20, so this sounds like a good time to define a concept.

What should comprise a "Parser"?

To start off, we want a static function called `parse()` that takes a string input. All my parser types so far have this function. What should it return? A `bool`, like I've shown so far? I'm not so sure of that. Take this simple nontrivial example. I want to parse either a `'0'` or a `'1'`, and indicate which one in the return value.

```cpp
int parse_bit(std::string_view str)
{
    bool result = AnyOf<"01">::parse(str);
    if (not result)
        return -1;
    else if ( /* we parsed a '0' */ )
        return 0;
    else // we parsed a '1'
        return 1;
}
```

Here we're trying to direct the control flow, using the contents of what was already parsed. With a `bool` return value, this isn't impossible, but it's annoying. This is the case whenever the value of the parsed string is needed for something. With a `bool` return value, how could we do this in the above example?

```cpp
int parse_bit(std::string_view str)
{
    bool result = AnyOf<"01">::parse(str);
    if (not result)
        return -1;
    else if (str[0] == '0')
        return 0;
    else
        return 1;
}
```

This is fine in this small example, but notice that we needed to re-parse the input manually after already parsing the input. We're checking if the first `char` in `str` is `'0'`, but we already did that. This could get unwieldy really fast with a more complex example.

Instead we could use a `std::optional` as a return type. But even better, I've found, is a `Result` class that wraps an optional, and also includes a "remaining" string. I've also found that the "remaining" un-parsed string is helpful in chaining operations together, but that's for a later discussion. It also leaves room for extensibility later, whereas `std::optional` can't be edited.

In a later article I'll expand on the `Result` type. For now, let's see how this could help the above example. `AnyOf::parse()` returns a `Result<std::string_view>`.

```cpp
int parse_bit(std::string_view str)
{
    Result<std::string_view> result = AnyOf<"01">::parse(str);
    if (not result)
        return -1;
    else if ((*result)[0] == '0')
        return 0;
    else
        return 1;
}
```

Now we don't need to parse the string twice. We call `parse(str)`, and we don't use `str` again. For this example it feels like overkill, but in the general case it's much nicer.

So far for the concept, we have:

```cpp
template <class P>
concept Parser = requires (std::string_view str) {
    { P::parse(str) } -> IsResult; // Later article
};
```

Let's keep going.

<br>

## Beyond the `parse()` function

Looking ahead (pun), I want another function on a "Parser" type, called `lookahead()`. Just in case we build a parser with an expensive result type, I want a way to skip creating it, and just check if the string matches or not. This is closer to the original idea of having a `bool` return type. I still think the "remaining" string is useful though, so I use `Result<void>` for this purpose.

Speaking of result types, I want each "Parser" to have a `typename result_type`, that indicates what it returns in its result. For now we only have `string_view` result types in the `AnyOf`, `NoneOf`, and `AllOf` parsers, but there will be more result types later. I want to generalize.

Next, I want each "Parser" to be an empty type. This is an expression template library, and I don't want the types to hold any non-static data members. Every instance of a type should be identical to every other instance of that same type. And I'll throw in an "implicitly default constructible" for good measure.

Lastly, I'll need a way to query a parser for which "family" it belongs to. I settled on using an enum called `ParserFamily`, where the first value is called `None`, the last is called `END`, and the middle values are valid.

Let's see the final concept.

```cpp
template <class P> 
concept Parser =
    requires { typename std::integral_constant<ParserFamily, P::family>; } and
    static_cast<int>(P::family) > static_cast<int>(ParserFamily::None) and
    static_cast<int>(P::family) < static_cast<int>(ParserFamily::END) and
    (std::is_empty_v<P>) and
    requires (void(fn)(P)) { fn({}); } and // Implicitly default constructible
    requires (Input input)
    {
        typename P::result_type;
        { P::parse(input) } -> IsResult<typename P::result_type>;
        { P::lookahead(input) } -> IsResult<void>;
    };
```

Some notes:
* I'm using the `std::integral_constant` line to ensure `P::family` is `static constexpr`
* After the self-explanatory `std::is_empty_v`, the following line checks for implicit default constructibility.
* I also aliased `Input = std::string_view` in my code base. This may (read: will) change later.

<br>

## Wrapping up

For the parsers I already defined in these articles, they need to be rewritten to conform to the concept. I'll spend time in later articles showing `Result<T>` and rewriting the parser types. Thanks for reading!

[Here](https://github.com/k3DW/blog/tree/main/assets/posts/tok3n/03-ParserConcept) is the code written in this article.
