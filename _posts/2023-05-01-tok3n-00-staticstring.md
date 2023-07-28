---
title: "A string literal as a template argument"
layout: post
permalink: /StaticString/
tags: [ tok3n ]
---

## But first, a preamble

I promise I'll get to the point. This is my backstory before the recipe in this metaphorical cookbook.

I was first introduced to the idea of parser combinators in, surprisingly, a CppCon talk not about parser combinators, at least not as the main feature. Ben Deane and Jason Turner's famous ["constexpr ALL the Things!"](https://youtu.be/PJwd4JLYJJY) talk opened my eyes to the magic that's possible with parser combinators, and I wanted to do it too! I tried and failed to replicate what they had, then gave it up.

<!--more-->

Another magical idea, expression templates, was introduced to me in Joel Falcou's two-part talk ["Expression Templates: Past, Present, Future"](https://youtu.be/IiVl5oSU5B8). Then I was determined to use expression templates in a 2nd attempt at creating parser combinators. Maybe this time I'll succeed by using a more complex method than before! (sarcasm of course)

I tried again, and gave up again. This stuff is hard, and I wasn't very experienced.

Then in summer 2022 I decided to give it another try. It's a fun challenge, for some definition of "fun" at least. With this attempt, I got it. I don't know what was the secret sauce that made a difference, but I want to talk about it here. I'm too excited not to share!

<br>

## A string, but make it static

Thanks for indulging me above, if you did.

I wanted to create an ergonomic interface where a string literal can be passed as a template argument. This 3rd key piece of magic was introduced to me by Hana Dusikova's [CTRE library](https://github.com/hanickadot/compile-time-regular-expressions).

To pass a string literal as a template argument, the parameter must be another type that's implicitly constructed from a string literal, to be used as a non-type template parameter (NTTP). Simple enough. But this type also needs to satisfy [certain requirements](https://en.cppreference.com/w/cpp/language/template_parameters) so it can be used as an NTTP. Put simply, it needs to be constexpr constructible and its data members must all be public. This is a drastic simplification, and it isn't completely accurate, but it's all that matters for our purposes here. Note that this only works in C++20 and beyond.

To get started, we know that a string is conceptually just an array of chars, so let's create a struct to store an array of chars. Depending on the size of the string literal, the array needs to hold a different number of chars, so we'll template this class on a `std::size_t` parameter. I'm calling it `StaticString`.

```cpp
template <std::size_t N>
struct StaticString
{
    std::array<char, N + 1> data = {};
};
```

This is a good start. I'm also leaving room to store a null terminator, just in case we ever want to use this data as a C string. Instead of `N` chars, make space for `N + 1` chars.

As-is, this type can already be used as an NTTP, even without adding constructors and other functions. But that's not what I'm looking for. I want automatic conversion from a string literal to a `StaticString`.

<br>

## Adding functionality

Next we need to create a constructor that accepts a string literal, which is just syntax sugar for a C array of chars. In C++ you can't pass a C array as a parameter, but you ***can*** take a C array by reference, surprisingly. Let's do that. And remember to make it constexpr.

```cpp
template <std::size_t N>
struct StaticString
{
    std::array<char, N + 1> data = {};

    constexpr StaticString(const char (&input)[N + 1])
    {
        std::ranges::copy_n(input, N + 1, data.begin());
    }
};
```

I would love to use `std::memcpy()` here instead, but that function isn't constexpr. So it's either a raw `for` loop or a `std::ranges` solution. I chose `std::ranges`. I'll eventually need other constructors and other functions, but I'll mention those when they're needed, if I decide to write more articles.

One more thing the type needs: [deduction guides](https://en.cppreference.com/w/cpp/language/class_template_argument_deduction), so that we don't need to specify the template parameter.

```cpp
template <std::size_t N>
StaticString(const char(&)[N]) -> StaticString<N - 1>;
```

I want to ensure that, for example, `StaticString("abc")` creates a `StaticString<3>`. The string literal `"abc"` has a length of 4, not 3, because of the null terminator. Instead of what I did here, `StaticString<3>` ***could*** have meant that it stores 2 chars plus the null terminator. Or it could have meant 3 chars with no null terminator. Or millions of other things. But this is what I liked the most.

Alright, let's use it!

<br>

## One final piece of magic

```cpp
template <StaticString str>
void foo()
{
    for (char c : str.data)
        std::cout << c;
    std::cout << "\n";
};

int main()
{
    foo<"abc">();  // > abc
    foo<"wxyz">(); // > wxyz
}
```

Of course you would never write this function in real life. This is just a demonstration of [functioning code](https://godbolt.org/z/axGsac681).

Here, we use `StaticString` as the NTTP, instead of `StaticString<N>` with a value for `N`. That means `foo()` is templated on ***every*** string literal, and not just the string literals of one particular size. And even better, we don't need to specify the size at the call site! I don't understand why it works, but I'm happy that it does. I would love to know which part of the C++ standards document allows it.

I've been using this type as a foundation to create other types where all the data is stored in the type itself, instead of using data members. I'll talk about compile time and run time performance at some other point. For now, it's fun and I've been enjoying the process.

I won't claim that my library is the best in any metric, but it's exciting to be able to call it my own. I want to document every aspect of my library in this blog, from the ground up, starting with this humble first blog post. I hope you'll come along with me.
