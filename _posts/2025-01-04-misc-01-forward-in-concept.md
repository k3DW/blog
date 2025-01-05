---
title: "How and why to std::forward inside a concept"
layout: post
permalink: /ForwardInConcept/
tags: [ misc, _hidden ]
---

I thought I had a solid understanding of how `std::forward` works, but it turns out I was wrong. In a concept definition where I originally used `std::forward`, it turned out to give the incorrect behaviour. It seems like `std::forward` should only be used in cases of type deduction, but I was using it in a situation where the type was explicitly passed.

This is a quick article where I go through my journey of forwarding inside a concept definition, and hopefully shed some light on the topic for those who are interested.

<!--more-->

In the effort to expand my parser generator library [`tok3n`](https://github.com/k3DW/tok3n), I started supporting out-parameters for the parsers, so that the parsed result can be any type that satisfies the necessary API. Relevant to this article, I have written concepts to check whether a type satisfies the APIs I need, and I misused `std::forward` in the concepts along the way.

<br>

## My use case

I have a few function-concept pairs in [`tok3n`](https://github.com/k3DW/tok3n) to check satisfaction of an API. In this article I'll look at my `adl_get()` function and `gettable` concept. I wanted to have the concept be standalone, and the function rely on the concept. Here is the function.

```cpp
template <std::size_t I, class T>
requires gettable<T&&, I>
constexpr decltype(auto) adl_get(T&& t)
{
	using std::get;
	return get<I>(std::forward<T>(t));
}
```

This function is meant to be used like `adl_get<2>(val)`. For example, if `val` is a `boost::variant`, then it will internally call `boost::get<2>(val)` with [ADL](https://en.cppreference.com/w/cpp/language/adl). The point is, I don't want to depend only on `std::get`. I want to support non-`std::` as well. I named it `adl_get` because I wanted to avoid any interaction with the other `get` overloads, since `get` is often used with ADL. It takes `t` by forwarding reference, and then uses `std::forward` to pass it to whichever `get` function is the best match. 

Notably, this function only works if the concept `gettable<T&&, I>` is satisfied. The type of `std::forward<T>(t)` is `T&&`, so checking `gettable<T, I>` would not be accurate.

Here was my first version of `gettable`. Please note, this is wrong, so I'm calling it `wrong_gettable`.

```cpp
template <class T, std::size_t I>
concept wrong_gettable = requires (T t)
{
	requires [](T t_) {
		using std::get;
		return requires { get<I>(std::forward<T>(t_)); };
	}(std::forward<T>(t));
};
```

I basically rewrote the `adl_get` function body inside a lambda because I need the `using std::get;` line. However, instead of returning the result of `get()`, I'm returning whether the expression `get<I>(std::forward<T>(t_))` is semantically valid. Then I'm immediately invoking this lambda and using the result in a [nested requirement](https://en.cppreference.com/w/cpp/language/requires#Nested_requirements), with the `requires` keyword right before the lambda.

The somewhat ugly form of the concept doesn't particularly matter here. I could have written it differently. The point is, I want to know whether `get<I>(std::forward<T>(t_))` is semantically valid, when `std::get` is also added to the overload set.

This concept works perfectly fine when I'm just calling `adl_get()`. Take the following setup.

```cpp
struct as_non_const_ref{};
struct as_const_ref{};
struct as_rvalue_ref{};

template <std::size_t>
void get(as_non_const_ref&) {}
template <std::size_t>
void get(const as_const_ref&) {}
template <std::size_t>
void get(as_rvalue_ref&&) {}
```

Then we should be able to `static_assert` on whether these types satisfy the concept.

```cpp
static_assert(gettable<as_non_const_ref&, 0>);
static_assert(not gettable<const as_non_const_ref&, 0>);
static_assert(not gettable<as_non_const_ref&&, 0>);

static_assert(gettable<as_const_ref&, 0>);
static_assert(gettable<const as_const_ref&, 0>);
static_assert(gettable<as_const_ref&&, 0>);

static_assert(not gettable<as_rvalue_ref&, 0>);
static_assert(not gettable<const as_rvalue_ref&, 0>);
static_assert(gettable<as_rvalue_ref&&, 0>);
```

And indeed this works just fine.

<br>

## What about passing a value type to the concept?

So far, every invocation of `gettable` has used a reference type for the `T` parameter. What would it mean to pass a value type?

For example, if I wanted to check `gettable<as_rvalue_ref, 0>`, should this be `true` or `false`? We could argue that it should be disallowed entire, and that we should add `std::is_reference_v` into the concept, but I'd like to allow this.

I would argue that checking for `gettable<as_rvalue_ref, 0>` should be equivalent to checking whether the following code compiles:

```cpp
as_rvalue_ref rr = ...;
adl_get<0>(rr);
```

I'm not doing anything fancy with the variable `rr`, I'm just passing it bare. Yes of course, this actually gets passed as an lvalue reference, but checking `decltype(rr)` gives just `as_rvalue_ref`, not `as_rvalue_ref&`. This code snippet does not compile, so therefore `gettable<as_rvalue_ref, 0>` should be `false`.

That's not what happens.

```cpp
static_assert(wrong_gettable<as_rvalue_ref, 0>);
```

This check actually succeeds, even though the corresponding code fails to compile.

<br>

## The return type of `std::forward`

This section goes over what I assumed about `std::forward` and about type deduction, and where I was wrong.

Note, `std::forward` must always be used with an explicit template parameter, so `std::forward(something)` is never valid code. It needs to be `std::forward<Something>(something)`.

At first I assumed `std::forward` would return a value type when you give it a value type. Like the following code.

```cpp
int x = 5;
using Type = decltype(std::forward<int>(x));

static_assert(std::same_as<Type, int&&>); // ???
```

I originally thought `Type` would be `int`. I naively and incorrectly assumed that the return type of `std::forward` is always the same type that was passed in. This is incorrect. Here, `Type` is actually `int&&`.

I couldn't understand why this would happen. If I'm forwarding with the type `int` and passing it an lvalue reference, the output should either be an lvalue reference or a value. Why should the above code be a move?

But it turns out I also misunderstood type deduction. [This C++ paper from 2002](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2002/n1385.htm) is a good read, as is the [StackOverflow post](https://stackoverflow.com/questions/3582001/what-are-the-main-purposes-of-stdforward-and-which-problems-does-it-solve) I got it from. `std::forward` seems to go hand-in-hand with forwarding references in function templates, where the template parameter is deduced from the argument.

Here was the misunderstanding regarding type deduction.

```cpp
template <class T>
void foo(T&&) {}
```

Previously, I thought that calling `foo()` with an lvalue reference deduces `T` as an lvalue reference, and calling `foo()` with an rvalue reference deduces `T` as an rvalue reference. This is _wrong_. Here is what actually happens.

```cpp
int x = 5;
foo(x);                // T is int&
foo(std::as_const(x)); // T is const int&
foo(std::move(x));     // T is int, not int&&
```

The `std::forward` function needs to work in harmony with type deduction, so `std::forward<int>(...)` must necessarily return an rvalue reference, given that rvalue references cause the deduced type to be a value type.

That settled it in my mind. I can't use `std::forward` in the concept, because I'm not always working with a deduced `T`.

<br>

## Trying `static_cast` in the concept

My next attempt at designing this concept looked like this. Note that it's also wrong.

```cpp
template <class T, std::size_t I>
concept also_wrong_gettable = requires (T t)
{
	requires [](T t_) {
		using std::get;
		return requires { get<I>(static_cast<T>(t_)); };
	}(static_cast<T>(t));
};
```

I wanted to use `static_cast` instead of `std::forward` because of the following properties.

```cpp
int x = 5;
using T1 = decltype(std::forward<int>(x));
using T2 = decltype(static_cast<int>(x));

static_assert(std::same_as<T1, int&&>);
static_assert(std::same_as<T2, int>);
```

Here, `T1` is `int&&`, but `T2` is just `int` with no reference. That's great! Now when I call `get<I>(static_cast<T>(t_))` this will pass a `T` directly, instead of a `T&&` as before, right?

It still doesn't work. The expression `static_cast<int>(x)` creates another `int` from the previous `int`, meaning that it creates a temporary value, meaning it passes an rvalue reference. It doesn't simply forward along the old `int`, it constructs a new one, passing it as a temporary.

As this point I was (and still am) convinced that I can only achieve the results I want by writing my own forwarding function.

<br>

## Writing my own forwarding function

Here is the definition of `std::forward`.

```cpp
template< class T >
constexpr T&& forward( std::remove_reference_t<T>& t ) noexcept
{
    return static_cast<T&&>(t);
}
template< class T >
constexpr T&& forward( std::remove_reference_t<T>&& t ) noexcept
{
    return static_cast<T&&>(t);
}
```

I wanted to modify `std::forward` and call it `non_deduced_forward`, to work the way I want it to work when given value types. When we pass this function an rvalue reference, the result should still be an rvalue reference, so there's no need to change the 2nd overload from `std::forward` above. However, I split the 1st overload into the reference case and the non-reference case, like the following.

```cpp
template< class T >
constexpr decltype(auto) non_deduced_forward( std::remove_reference_t<T>& t ) noexcept
{
	if constexpr (std::is_reference_v<T>)
    	return static_cast<T&&>(t); // Same as before
	else
		return t; // Pass along the lvalue reference
}

// ... 2nd overload remains unchanged, but renamed to `non_deduced_forward`
```

This function uses `decltype(auto)` so it can pass along the exact reference type, without explicitly stating it. I could use `std::conditional_t` for the precision and guaranteed correctness, but I chose to leave it with shorter syntax for now.

This function `non_deduced_forward()` behaves identically to `std::forward` when passed reference types, and it also behaves identically when passed a value type and given an rvalue reference parameter. The only difference is how it handles lvalue references when passed a value type.

```cpp
int x = 5;
using T1 = decltype(non_deduced_forward<int>(x));
using T2 = decltype(non_deduced_forward<int>(std::move(x)));

static_assert(std::same_as<T1, int&>);
static_assert(std::same_as<T2, int&&>);
```

To me, this behaviour is more sensical when passing the type explicitly. But this is incompatible with C++'s type deduction. Take the following code for example.

```cpp
template <class T>
decltype(auto) foo(T&& t)
{
    return std::forward<T>(t);
}

template <class T>
decltype(auto) bar(T&& t)
{
    return non_deduced_forward<T>(t);
}

static_assert(std::same_as<decltype(foo(x)), int&>);
static_assert(std::same_as<decltype(foo(std::as_const(x))), const int&>);
static_assert(std::same_as<decltype(foo(std::move(x))), int&&>);

static_assert(std::same_as<decltype(bar(x)), int&>);
static_assert(std::same_as<decltype(bar(std::as_const(x))), const int&>);
static_assert(std::same_as<decltype(bar(std::move(x))), int&>); // ??? This is obviously wrong
```

All of the `static_assert`s make sense except for the last one. When using this `non_deduced_forward()` function with forwarding references and type deduction, it gives the wrong result for rvalue reference arguments. That said, it could be fixed if `bar()` used `non_deduced_forward<T&&>(t)` instead... but that's not something I'd advocate. I made this function for one specific purpose, which was to use it in a concept.

So let's use it in a concept.

<br>

## Putting it all together

This is what the final concept looks like. ("Final" for now, until I refactor it again.)

```cpp
template <class T, std::size_t I>
concept gettable = requires (T t)
{
	requires [](T t_) {
		using std::get;
		return requires { get<I>(non_deduced_forward<T>(t_)); };
	}(non_deduced_forward<T>(t));
};
```

Using the setup from the first section of the article, all the `static_assert`s using `gettable` still hold. But now, the following `static_assert`s will _also_ all hold.

```cpp
static_assert(gettable<as_non_const_ref, 0>);
static_assert(not gettable<const as_non_const_ref, 0>);

static_assert(gettable<as_const_ref, 0>);
static_assert(gettable<const as_const_ref, 0>);

static_assert(not gettable<as_rvalue_ref, 0>);
static_assert(not gettable<const as_rvalue_ref, 0>);
```

In the original wrong `gettable`, some of these would not hold. I was particularly concerned with the checks regarding `as_rvalue_ref`. I don't ever think someone would _actually_ define a type where `get()` only exists on rvalue references, but I want the concept to be accurate for all possible usages.

Through this process I learned quite a lot about `std::forward` and type deduction. Hopefully you've learned something too, or at least felt some catharsis by seeing me hold some misconceptions that you may have held in the past.

In conclusion, I want to write code that's correct and verifiable at compile-time through the use of concepts. C++ makes it possible, but it doesn't always make the process easy, and clearly I'll go through great lengths to achieve this. Thanks for reading!
