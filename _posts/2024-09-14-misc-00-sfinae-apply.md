---
title: "A single-function SFINAE-friendly std::apply"
layout: post
permalink: /SfinaeApply/
tags: [ misc ]
---

There's this issue I've had when using `std::apply`, and I'm sure if you've written enough generic code, then you've experienced it too. If not, don't worry, I'll go through it fully. As specified in the standard, you can't check whether a call to `std::apply` is semantically valid at compile-time. This would often be useful with a [SFINAE idiom](https://en.cppreference.com/w/cpp/language/sfinae), whether using classic SFINAE or using C++20 constraints.

I recently wrote a SFINAE-friendly `apply` function for my C++20 expression template parser generator library [`tok3n`](https://github.com/k3DW/tok3n). I thought the code was interesting enough that I wanted to write more about it here. I aimed to develop an explicit understanding of SFINAE-friendliness along the way.

<!--more-->

<br>

## A brief explanation of `std::apply`

Just in case you're reading this and you don't yet know about `std::apply`, I'll introduce it here. Most people reading this article should probably skip this section, but it's here for those who want it, for completeness.

Here is a motivating example with a simple summing lambda.
```cpp
auto sum = [](int a, int b, int c) { return a + b + c; };
std::tuple<int, int, int> tup{ 2, 3, 4 };
```

If we want to call `sum` with `tup`'s elements as arguments, we can use `std::get`.

```cpp
int summed_with_get = sum(std::get<0>(tup), std::get<1>(tup), std::get<2>(tup));
assert(summed_with_get == 9);
```

This code is correct, but it's ugly and verbose. We need to write `std::get` 3 separate times, once for each element of the tuple. We'll need to change the call site of `sum` if the number of elements ever changes.

Alternatively, C++17 introduced `std::apply`, which does all this element unpacking with `std::get` automatically, so you don't have to think about it. This is what the same summing code would look like.

```cpp
int summed_with_apply = std::apply(sum, tup)
assert(summed_with_apply == 9);
```

Wow, that's beautiful code. Even better, it's generic over the number of elements. That is, if we change the tuple and summing function to have 2 elements, or 4 elements, or any number of N elements, the call to `std::apply` doesn't change.

Here's an adapted version of the code on the [cppreference page for `std::apply`](https://en.cppreference.com/w/cpp/utility/apply). I substituted all the "exposition-only" things for C++ code that can be compiled as-is. Below is a completely valid and conforming implementation of `std::apply` as stated in the C++17/C++20 standard. I left out the `noexcept` specification because it isn't relevant here.

```cpp
namespace std {

template <class F, class Tuple, std::size_t... I>
constexpr decltype(auto) __apply_impl(F&& f, Tuple&& t, std::index_sequence<I...>)
{
    return std::invoke(std::forward<F>(f), std::get<I>(std::forward<Tuple>(t))...);
}
template <class F, class Tuple>
constexpr decltype(auto) apply(F&& f, Tuple&& t)
{
    return __apply_impl(std::forward<F>(f), std::forward<Tuple>(t),
        std::make_index_sequence<std::tuple_size_v<std::decay_t<Tuple>>>{});
}

} // namespace std
```

Here it uses `std::make_index_sequence<N>` to create a `std::index_sequence<0, 1, etc, N-1>`. Then, the `__apply_impl()` overload gets selected based on the specific `index_sequence` passed in. This is how we inject the pack of numbers. When we call `std::get<I>(expr)...`, we are _actually_ calling `std::get<0>(expr), std::get<1>(expr), etc, std::get<N-1>(expr)`, for each of the numbers in the pack.

Note that `decltype(auto)` just means "forward along _exactly_ the value category of the returned expression". In generic contexts like this is makes sense, but it has very rare usage.

This code isn't exactly beginner-friendly, but I don't think it needs to be. It's meant to be written by standard library implementers. Below is a more beginner-friendly and almost-but-not-quite-correct version of this code. This code is not meant to be used. It is for explanation only, for the purposes of this article.

```cpp
namespace incorrect_std {

template <class F, class Tuple, std::size_t... I>
constexpr auto __incorrect_apply_impl(const F& f, const Tuple& t, std::index_sequence<I...>)
{
    return f(std::get<I>(t)...);
}
template <class F, class Tuple>
constexpr auto incorrect_apply(const F& f, const Tuple& t)
{
    return __incorrect_apply_impl(f, t, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

} // namespace incorrect_std
```

The above code isn't correct, but it'll work "correctly enough" in many cases. Hopefully it helps to illustrate the point more clearly, if the previous mock implementation didn't completely make sense. The 2 implementations are morally equivalent, but the first one takes more care to be technically correct in all the edge cases.

As you can see, `std::apply` is surprisingly simple. We're just calling `std::get<I>` on the tuple for each `I` from `0` to `N-1`, and passing these as arguments to `f`. It only requires a valid `std::tuple_size_v` and `std::get` on the tuple object, meaning we can use `std::array`, `std::pair`, `std::tuple`, and any user-defined types that meet this API criteria.

Unfortunately we can't check for the validity of a specific call to `std::apply` and then use that information later in our program. I've run into this issue, hence the article.

<br>

## A possible use case of a SFINAE-friendly `apply`

Let's say, for example, I wanted to call `std::apply` if the expression is valid, but then fallback to just invoking the function regularly otherwise. Here is how I would implement that scheme in C++20. It's possible to write an equivalent function in pre-C++20. It'll be left as an exercise for the reader. (I feel so empowered saying that!)

```cpp
template <class F, class Tuple>
constexpr decltype(auto) apply_or_invoke(F&& f, Tuple&& t)
{
	if constexpr (requires { std::apply(std::forward<F>(f), std::forward<Tuple>(t)); })
		return std::apply(std::forward<F>(f), std::forward<Tuple>(t));
	else
		return std::invoke(std::forward<F>(f), std::forward<Tuple>(t));
}
```

Why would you want this specific case? Who knows. It's just a simple enough case to show the point. There are other reasons you want to check for the validity of a call to `std::apply` at compile-time, but this one is a simple few-liner example.

Now let's put it to work. I'll start with a lambda that counts the number of arguments you pass to it.

```cpp
auto count_args = []([[maybe_unused]] auto&&... ts) { return sizeof...(ts); };
```

Here's what _should_ happen for any call to `apply_or_invoke(count_args, obj)`:
* Calling with any `obj` that is tuple-like should yield the `obj` type's `std::tuple_size_v`
* Calling with any `obj` that isn't tuple-like should yield `1`

These are how some tuple-like types interact.

```cpp
static_assert(0 == apply_or_invoke(count_args, std::tuple<>{}));
static_assert(1 == apply_or_invoke(count_args, std::tuple<int>{}));
static_assert(3 == apply_or_invoke(count_args, std::tuple<int, int, int>{}));
static_assert(2 == apply_or_invoke(count_args, std::pair<int, int>{}));
static_assert(5 == apply_or_invoke(count_args, std::array<int, 5>{}));
```

This is just as expected.

What about some non-tuple-like types? The following statements _should_ compile successfully.

```cpp
static_assert(1 == apply_or_invoke(count_args, nullptr) == 1);
static_assert(1 == apply_or_invoke(count_args, int{}) == 1);
static_assert(1 == apply_or_invoke(count_args, std::string{}) == 1);
```

But actually, each of these lines causes a compile error. On MSVC, the first error to pop up says the following, substituting `T` for whatever type I'm trying to use here. I'm sure there are similar errors on other compilers.

> error C2027: use of undefined type 'std::tuple_size<T>'

<br>

## What is SFINAE-friendliness?

The problem lies in the `if constexpr` condition.

```cpp
requires { std::apply(std::forward<F>(f), std::forward<T>(t)); }
```

As it turns out, this requires-expression doesn't ever return `false`. It's either `true` or it's a compilation error. We can't know the return type without analyzing the function body, which is what SFINAE-friendliness is about. We want the semantic validity of the function signature to match the semantic validity of the function body. Let's take away the function body.

```cpp
template <class F, class Tuple>
constexpr decltype(auto) my_apply(F&& f, Tuple&& t);
```

The above code is what the function signature of `std::apply` looks like. We have 2 template parameters, `F` and `Tuple`, without any constraints on which types those parameters can be. In that case, any template parameters should satisfy a concept checking for callability of this function. So let's write it.

```cpp
template <class F, class Tuple>
cconcept my_applyable = requires (F f, Tuple t) { my_apply(f, t); };
```

We would expect this to be `true` for all `F` and `Tuple` template arguments. So what actually happens?

```cpp
static_assert(!my_applyable<int, int>);
static_assert(!my_applyable<decltype(count_args), tuple<>>);
```

Apparently, this concept _evaluates to `false` for all arguments_.

Oh.

Earlier I said:

> We want the semantic validity of the function signature to match the semantic validity of the function body.

I don't think I was incorrect here. But I also said:

> ```cpp
> template <class F, class Tuple>
> constexpr decltype(auto) my_apply(F&& f, Tuple&& t);
> ```
>
> [...]
>
> We have 2 template parameters, `F` and `Tuple`, without any constraints on which types those parameters can be. In that case, any template parameters should satisfy a concept checking for callability of this function.

This claim _isn't_ true. This signature _does_ have conditions. Namely, all types must be semantically valid. This includes the return type.

This function has the placeholder return type `decltype(auto)`, which actually means the function body is analyzed to determine what the return type will be. Here there isn't a function body, meaning the return type can't be deduced, meaning this function isn't callable with any arguments at all.

So what's actually going on here?

<br>

## Into the weeds

This is my best understanding of the details. I'm open to being corrected, and I'll amend the article if and when that happens. Feel free to reach out.

I'll make a simpler example here than `std::apply`, because we can get quite lost in the detailed expert-level syntax. Here's a function template called `plus` that operates on 2 types with `operator+`.

```cpp
template <class T, class U>
auto plus(const T& t, const U& u)
{
    return t + u;
}
```

If I try calling `plus(1, 2)`, this returns `3`. If I try calling `plus(nullptr, 0)` then I get a compile error. These 2 points are obvious. But with the function as it is, we can't even check for callability. Take the following code for example.

```cpp
template <class A, class B>
concept plus_able = requires (A a, B b) { plus(a, b); };

static_assert(plus_able<int, int>);
static_assert(!plus_able<nullptr_t, int>);
```

The 2nd `static_assert` doesn't evaluate to `true` or `false`, but gives an error entirely. In my case:

> error C2389: '+': illegal operand 'nullptr'

There is an ordering to the steps when compiling a C++ function template. Here is a broad and "correct-enough" overview. It's glossing over many details.
* First, the function signature is deemed semantically valid or invalid, without involving the function body.
* Then the function body is "stamped out" with the specific types provided for this instantiation. If the function signature had a placeholder return type, here is when the return type is deduced.

When we're querying for the callability of a function, we're only checking the answer to the 1st bullet point above. If checking the 1st bullet point fails, we call this ["substitution failure is not an error"](https://en.cppreference.com/w/cpp/language/sfinae), and the compiler can move on to try other things. It's a recoverable failure.

If the 2nd point fails, then this is an unrecoverable hard error.

Let's reexamine the `plus` function with these points in mind.

```cpp
template <class T, class U>
auto plus(const T& t, const U& u)
{
    return t + u;
}
```

When we call `plus(nullptr, 0)`, the signature is considered without the function body. That signature looks like the following.

```cpp
to_be_determined plus<nullptr_t, int>(const nullptr_t&, const int&);
```

The compiler hasn't yet analyzed the body of the function, so it doesn't yet know the return type, but the signature looks valid. At this point, the compiler has chosen the overload, and there's no going back.

When the compiler analyzes the body, it sees `nullptr + 0`, which is not semantically valid C++ code. Now the error is unrecoverable. This means, even if we are only checking for the semantic validity of the function call inside of a concept, we get a compile error.

In summary, the problem is thus. In order to check whether a function template is callable with specific arguments, we can't have unconstrained template parameters and a deduced return type, if the function body will be semantically invalid for some set of template parameters.

<br>

## Constraining the function template

I see 3 ways to make the function SFINAE-friendly, given my last sentence in the section above.
1. Constrain the template parameters
2. Write an explicit return type, so that the compiler doesn't need to see the function body
3. Do both of the above

Point 1 looks a lot nicer in C++20 than it does prior to C++20. Point 2 is available in the same syntax regardless of pre- or post-C++20. Technically, constraining a function template prior to C++20 involves giving it an explicit return type, which will provide recoverable errors if the return type is determined to be semantically invalid.

Point 3 is complete overkill, but I won't stop you if you feel empowered.

We can take a common pre-C++20 SFINAE-friendly approach, like the following.

```cpp
template <class T, class U, class = void>
struct plus_trait;
template <class T, class U>
struct plus_trait<T, U, std::void_t<decltype(std::declval<T>() + std::declval<U>())>>
{
	using type = decltype(std::declval<T>() + std::declval<U>());
};

template <class T, class U>
typename plus_trait<T, U>::type plus(const T& t, const U& u)
{
    return t + u;
}
```

This is unnecessary though. An approach using a bespoke trait was needed before C++11, but we can simplify it with `decltype`.

```cpp
template <class T, class U>
decltype(std::declval<T>() + std::declval<U>()) plus(const T& t, const U& u)
{
    return t + u;
}
```

This one above is compatible with C++11 and beyond. We can simplify it further by using a trailing return type.

```cpp
template <class T, class U>
auto plus(const T& t, const U& u) -> decltype(t + u)
{
    return t + u;
}
```

Then in C++20, we can write constraints in a region separate from the return type. In this case, we just need to copy the function body into the requires-expression, so I'll write the constraint in-line with the function signature instead of making it a named concept.

```cpp
template <class T, class U>
requires requires (T t, U u) { t + u; }
auto plus(T t, U u)
{
    return t + u;
}
```

This is actually more verbose than the trailing return type example above it, but I prefer it aesthetically. However, it's more common practice to use the trailing `decltype()`, so I'll do it that way. At least we can be rid of the `requires requires` duplication.

I flip back and forth between finding the function body duplication amusing, and finding it annoying. As it stands, we actually need to triplicate the function body, if we want full correctness with `noexcept`. Like the following.

```cpp
template <class T, class U>
auto plus(T t, U u) noexcept(noexcept(t + u)) -> decltype(t + u)
{
    return t + u;
}
```

The triplication is unfortunate. I hope we'll be able to do better in the future without writing the same function body 3 separate times.

Now let's do this to `std::apply`.

<br>

## Applying SFINAE-friendliness to `apply`

I'll create 2 functions, `__apply_impl()` and `apply()`, each with this general form.

```cpp
template </* template-parameters */>
constexpr auto function(/* parameters */) noexcept(noexcept(/* expression */)) -> decltype(/* expression */)
{
    return /* expression */;
}
```

As I mentioned above, there is quite a lot of repetition of code. This could be wrapped up into a macro.

For `__apply_impl()`, this will have
* `parameters` = `F&& f, Tup&& tup, std::index_sequence<Is...>`, with the necessary template parameters
* `expression` = `std::invoke(std::forward<F>(f), std::get<Is>(std::forward<Tup>(tup))...)`

For `apply()`, this will have
* `parameters` = `F&& f, Tup&& tup`, with the necessary template parameters
* `expression` = `__apply_impl(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{})`
* We actually need a constraint checking whether `std::tuple_size<std::decay_t<Tup>>` is a complete type, meaning `std::tuple_size_v` is valid in this case.

Repeating all this as many times as we actually need to, it's a mouthful. But it works! Now we can query whether or not `apply()` can be called with given parameters. This is extremely useful.

That was a short section, and it satisfies the requirements. But let's see if we can write this without the helper function `__apply_impl()`. I want to avoid introducing another identifier into the namespace. _*Note that everything in the rest of this article is for exploration and amusement. I'm not recommending this for your code. I'm merely having fun seeing how far I can take the language.*_

<br>

## One step further

In C++20, we can write lambdas whose call operator has explicitly specified template parameters. Using this, we could define a non-SFINAE-friendly `apply()` function without proper `noexcept` like below, with an immediately-invoked lambda. I first saw a similar trick like this from Daisy Hollman. I'm a fan of her "cute tricks" series. This one, in particular, could actually be applicable in production code.

```cpp
template <class F, class Tup>
constexpr decltype(auto) apply(F&& f, Tup&& tup)
{
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::invoke(std::forward<F>(f), std::get<Is>(std::forward<Tup>(tup))...);
    }(std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{});
}
```

This is more terse than defining the helper function `__apply_impl()`. Of course we still have a "helper function", but it's defined anonymously inside the `apply()` function itself. Can this become SFINAE-friendly?

Yes, we can do it, but it's not pretty, especially factoring in `noexcept`. We have a few things to consider.
* To factor in SFINAE-friendliness and `noexcept`, we need to triplicate the function body
* The function body is an immediately-invoked lambda expression
* A lambda defined inside an unevaluated context (for example, inside a `decltype` expression) cannot have any captures, and seemingly must redeclare the template parameters

Previously, the inner immediately-invoked lambda looked like this.
```cpp
[&]<std::size_t... Is>(std::index_sequence<Is...>) -> decltype(auto) {
    return std::invoke(std::forward<F>(f), std::get<Is>(std::forward<Tup>(tup))...);
}(std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{})
```

Now instead, it needs to be capture-less and it needs to have its own template parameters. This is what I came up with. I chose to use `F_` and `Tup_` for the inner type parameters, in place of `F` and `Tup`, otherwise we'll end up with shadowing errors.

```cpp
[]<class F_, class U, std::size_t... Is>(F_&& f_, Tup_&& tup_, std::index_sequence<Is...>) -> decltype(auto) {
    return std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...);
}(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{})
```

But wait, we're not done. This is just a stateless version of what we already had. The inner lambda itself needs to have a constraint, and it needs to factor in `noexcept`. Ultimately it looks like this.

```cpp
[]<class F_, class Tup_, std::size_t... Is>(F_&& f_, Tup_&& tup_, std::index_sequence<Is...>)
noexcept(noexcept(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)))
-> decltype(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)) {
    return std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...);
}(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{})
```

Isn't it gorgeous? But we're _still_ not done. In C++ it seems we love repeating ourselves, given all the "requires requires" and "noexcept noexcept". _*We still need to triplicate this entire lambda*_. We _could_ trim off certain parts of the expression in certain places, but I would prefer not to do that, to ensure it's correct. Copying-and-pasting is less error-prone than copying-and-pasting-and-then-editing.

<br>

## The monster

Here's the result. Note we still to check for `std::tuple_size` before the function parameters.

```cpp
template <class F, class Tup>
requires requires {	std::tuple_size<std::decay_t<Tup>>{}; }
constexpr auto apply(F&& f, Tup&& tup)
noexcept(noexcept(
    []<class F_, class Tup_, std::size_t... Is>(F_&& f_, Tup_&& tup_, std::index_sequence<Is...>)
	noexcept(noexcept(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)))
	-> decltype(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)) {
		return std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...);
	}(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{})
)) -> decltype(
	[]<class F_, class Tup_, std::size_t... Is>(F_&& f_, Tup_&& tup_, std::index_sequence<Is...>)
	noexcept(noexcept(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)))
	-> decltype(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)) {
		return std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...);
	}(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{})
) {
    return []<class F_, class Tup_, std::size_t... Is>(F_&& f_, Tup_&& tup_, std::index_sequence<Is...>)
	noexcept(noexcept(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)))
	-> decltype(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)) {
		return std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...);
	}(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{});
}
```

And there we have it, a fully SFINAE-friendly and `noexcept`-friendly implementation of `std::apply`, that doesn't introduce any additional identifiers into the namespace.

In a previous version of this article, I wrote a "monster" that MSVC didn't like. That was using `requires requires` instead of `-> decltype()`. This one actually works perfectly fine with MSVC, but Clang doesn't like it, complaining about `std::tuple_size`.

Regardless, there is a downside here. If we don't name our helper function, then we're declaring 3 separate lambdas, which means 3 distinct types in the compiler. It's possible the compiler will have a larger memory footprint from using a function defined this way.

I think I know how to fix both of our problems.

<br>

## The finale

We need to make sure to define the lambda only once, and yet be able to use it in all these 3 places.

Instead of writing the lambda 3 times, I'll just write it once as a defaulted non-type template parameter. Like this.

```cpp
template <class F, class Tup, auto impl =
	[]<class F_, class Tup_, std::size_t... Is>(F_&& f_, Tup_&& tup_, std::index_sequence<Is...>)
	noexcept(noexcept(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)))
	-> decltype(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...))
	{
		return std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...);
	}
>
requires requires {	std::tuple_size<std::decay_t<Tup>>{}; }
constexpr auto apply(F&& f, Tup&& tup)
noexcept(noexcept(impl(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{})))
-> decltype(impl(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{}))
{
    return impl(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{});
}
```

Actually Clang still doesn't like this, so I'll have to switch to `requires requies` in the outer function. Here is the final final version.

```cpp
template <class F, class Tup, auto impl =
	[]<class F_, class Tup_, std::size_t... Is>(F_&& f_, Tup_&& tup_, std::index_sequence<Is...>)
	noexcept(noexcept(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...)))
	-> decltype(std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...))
	{
		return std::invoke(std::forward<F_>(f_), std::get<Is>(std::forward<Tup_>(tup_))...);
	}
>
requires requires {	std::tuple_size<std::decay_t<Tup>>{}; }
constexpr decltype(auto) apply(F&& f, Tup&& tup)
noexcept(noexcept(impl(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{})))
requires requires { impl(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{}); }
{
    return impl(std::forward<F>(f), std::forward<Tup>(tup), std::make_index_sequence<std::tuple_size_v<std::decay_t<Tup>>>{});
}
```

This works on both Clang and MSVC.

Yes, we still need to triplicate the _calls_ to `impl`, but the _definition_ of `impl` is only present once, meaning we only create 1 lambda type per function instantiation. There is still the possibility of purposeful misuse by specifying the template parameters at the call side, but I don't think that's worth worrying about.

<br>

## Conclusion

I hope you enjoyed the process of watching me learn about SFINAE-friendliness, and how to allow a function template to be queried for the validity of its calls. We went on a wild ride, starting with the [cppreference sample implementation of `std::apply`](https://en.cppreference.com/w/cpp/utility/apply), then making it SFINAE-friendly, then working as hard as possible to remove the separate helper function, to minimize the number of identifiers introduced into the namespace. This turned our function into a monster, but it was ultimately condensed back into something reasonably manageable. Nice!

I started writing this article intending it to be a short one, but somehow it turned into my longest article yet.

Thanks for reading! 
