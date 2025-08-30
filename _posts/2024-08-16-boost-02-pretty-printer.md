---
title: "Visualizing boost::unordered_map in GDB, with pretty-printer customization points"
layout: post
permalink: /PrettyPrinter/
tags: [ boost, pretty-print ]
---

This article is about my experience implementing [GDB pretty-printers](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Pretty-Printing.html) for the [Boost.Unordered containers](https://github.com/boostorg/unordered/). You can read my related pair of articles on the Visual Studio natvis implementation [here](/NatvisForUnordered/) and [here](/NatvisForUnordered2/).

Importantly, in this article I'll outline the techniques I used so that users can inject their own behaviour into the pretty-printers when the containers are using custom fancy pointer types. A "pretty-printer customization point" is my nickname for the technique I'm using, not an official term.

<!--more-->

Before writing any of the visualizations for Boost.Unordered, I had previously written some simple natvis types, but I had never written any GDB pretty-printers. I had assumed it would be more complicated to get started, to connect GDB to the pretty-printer implementation, and to write the pretty-printer itself. I was wrong; it couldn't be easier.

This work has been sponsored by [The C++ Alliance](https://cppalliance.org/).

<br>

## Setting up GDB for pretty-printing

A pretty-printer is a Python script that's loaded into GDB, that tells GDB exactly how to print a type. These scripts already exist for all sorts of Standard Library facilities. GDB has an example for how `std::string` looks with and without a pretty-printer [at this link](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Pretty_002dPrinter-Example.html).

If you want to use the Boost.Unordered pretty-printers, it's very simple. First, ensure GDB has pretty-printing enabled. As far as I understand, this is a one-time configuration step, that you don't need to do for every GDB run.

```
(gdb) set print pretty on
```

I used [Niall Douglas's technique](https://lists.boost.org/Archives/boost/2024/07/257002.php) to embed the Python script into the executable, something I haven't seen before he demonstrated it. He was a great help in getting this technique to work for Unordered as well! In Boost 1.87 or later, if you use Boost.Unordered without defining any other macros, you already have the pretty-printers embedded in your binary. All you need to do is `add-auto-load-safe-path` to your executable, like this.

```
(gdb) add-auto-load-safe-path path/to/executable
```

I added these commands to a "`.gdbinit`" file in my home directory, so I never need to think about it again.

Otherwise, you can disable embedding the Python script in your binary by defining the macro `BOOST_ALL_NO_EMBEDDED_GDB_PRINTERS`, which disables script embedding for all Boost libraries that have it. Then, to use the pretty-printers, you can direct GDB to the Python script using the `source` command. This is the most common way of using a GDB script.

```
(gdb) source path/to/boost/libs/unordered/extra/boost_unordered_printers.py
```

<br>

## A basic pretty-printer

This section may be of interest if you have never written a pretty-printer before, or had any exposure to the GDB Python API. I'll show how the pieces fit together at a high level.

I created 1 pretty-printer class in Python for all of the closed-addressing containers, i.e. the direct drop-in replacement containers for `std::unordered`, since they have shared internals. For the simplest pretty-printer, all you need is a constructor and a `to_string()` function.

```python
class BoostUnorderedFcaPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        return f"This is a {self.val.type}"
```

Particularly of note in the `to_string()` function:
* `self.val` is a `gdb.Value`
* Its `.type` member is a `gdb.Type`
* Both `gdb.Value` and `gdb.Type` have overloaded [`__str__()` functions](https://docs.python.org/3/reference/datamodel.html#object.__str__), giving them nice string representations
* The `__str__()` function is what gets called when using a variable in an f-string
* Notably, the string representation of a `gdb.Value` will call into its own pretty-printer, if a pretty-printer exists

There are multiple ways to register a pretty-printer class with GDB. I did it like this.

```python
def boost_unordered_build_pretty_printer():
    pp = gdb.printing.RegexpCollectionPrettyPrinter("boost_unordered")
    add_template_printer = lambda name, printer: pp.add_printer(name, f"^{name}<.*>$", printer)

    add_template_printer("boost::unordered::unordered_map", BoostUnorderedFcaPrinter)
    add_template_printer("boost::unordered::unordered_multimap", BoostUnorderedFcaPrinter)
    add_template_printer("boost::unordered::unordered_set", BoostUnorderedFcaPrinter)
    add_template_printer("boost::unordered::unordered_multiset", BoostUnorderedFcaPrinter)
    return pp

gdb.printing.register_pretty_printer(gdb.current_objfile(), boost_unordered_build_pretty_printer())
```

I started by creating a `RegexpCollectionPrettyPrinter` object, which acts as a mapping from a regex to a printer type. Each call to the `.add_printer()` function creates a new entry with the given name, the given regex to match, and the given mapped printer type. I wrapped this function call in a lambda for convenience. Ultimately, the `RegexpCollectionPrettyPrinter` object gets passed into the `register_pretty_printer()` function, which connects our custom pretty-printers to GDB.

Now if we query GDB for which printers exist, we see the following output.

```
(gdb) info pretty-printer
global pretty-printers:
    ...
objfile path/to/executable pretty-printers:
    boost_unordered
        boost::unordered::unordered_map
        boost::unordered::unordered_multimap
        boost::unordered::unordered_set
        boost::unordered::unordered_multiset
objfile /lib/x86_64-linux-gnu/libstdc++.so.6 pretty-printers:
    ...
```

There are 4 separate printers registered with GDB, each with their own name and their own regex. They all point to the same printer type, so this could have been registered as a single entry with one regex, but I prefer seeing 4 separate names listed.

When we ask GDB to print a container, it will look like this. This is exactly the returned value of the `to_string()` function.

```
(gdb) print my_unordered_map
$1 = This is a boost::unordered::unordered_map<int, int, boost::hash<int>, std::equal_to<int>, std::allocator<std::pair<int const, int> > >
(gdb) print my_unordered_multiset
$2 = This is a boost::unordered::unordered_multiset<int, boost::hash<int>, std::equal_to<int>, std::allocator<int> >
```

<br>

## Contrasting APIs, a case study

This part will be easier to understand if you've read my previous 2 articles [here](/NatvisForUnordered/) and [here](/NatvisForUnordered2/), although it's not required.

Coming from writing the natvis visualizations, the GDB pretty-printers were more elegant to write. Natvis does not allow easy genericity, and it is strongly typed. I'll give an example of the contrast between the two frameworks using this setup: The `unordered_node` containers use an extra type internally as the "node", for the added level of indirection. The `unordered_flat` containers don't have this extra indirection, which is what makes them "flat".

In the natvis implementation (written in XML), while iterating the container we output each item like this: `<Item>*p_</Item>`. This works just fine for `flat` containers, but it fails for `node` containers. This is because the expression `*p_` can either have type `T` or `element_type<T>`, for `flat` and `node` containers respectively. To generically handle displaying both of these cases, I needed to write an extra natvis `<Type>` definition for `element_type<T>`.

Unfortunately, this means that the `<Type>` definition for `element_type<T>` needs to know about some details that shouldn't be relevant here. Namely, it needs to know about the fancy pointer customization points. I would have loved to write a "`maybe_unwrap_element()`" intrinsic whose only job is either to unwrap the `element_type<T>` into a `T` or to do nothing, however this wasn't feasible because of the 2nd template parameter that's usually (but not always) defaulted. Ultimately, it needed to be more complex.

On the other hand with GDB, this `maybe_unwrap_element()` function was not only feasible, but surprisingly easy. In the pretty-printing API, we view a C++ object through a Python variable of type `gdb.Value`, which is effectively a reflected version of the C++ object. The object's type is stored in the `.type` member, which we can query and branch on, as needed. Natvis requires everything to be strongly typed, so we don't have this same level of flexibility. Similarly, because the pretty-printing API uses a full programming language, helper functions can use control flow and loops. With natvis, it's cumbersome to do more complex tasks because intrinsics are limited to a single statement.

Here is my pretty-printer helper function. This is a short function that achieves something that's difficult-to-impossible in natvis, specifically because natvis is both strongly typed and declarative, while the GDB API is procedural and acts more closely to reflection.

```python
def maybe_unwrap_foa_element(e):
    element_type = "boost::unordered::detail::foa::element_type<"
    if f"{e.type.strip_typedefs()}".startswith(element_type):
        return e["p"]
    else:
        return e
```

In this function, we're checking to see if `e` is an instantiation of the `element_type` class template. If it is, return its `.p` member, otherwise pass through the function untouched. How do we know if `e` is an `element_type`? Just grab its `.type` member as a string, and check if this string starts with the correct template name. It's all strings.

Note, we grab an object's member by passing a string into the square bracket operator, which is called [`__getitem__()`](https://docs.python.org/3/reference/datamodel.html#object.__getitem__) in Python. Here, `e["p"]` will return a `gdb.Value` which wraps the `p` data member. Because both `e` and `e["p"]` are variable of type `gdb.Value`, we can add type annotations to this Python function if desired. This is impossible in natvis because `e` and `e.p` would have different types.

```python
def maybe_unwrap_foa_element(e: gdb.Value) -> gdb.Value:
    # ...
```

<br>

## It's all strings

In general, I found a lot of flexibility in the pretty-printing API by using string conversions and string comparisons. In natvis, these would be typed operations, and it would be much more verbose to achieve the same result.

Here is an example of the flexibility afforded to us by using strings. Below is a simplified version of the actual pretty-printer I wrote for the closed-addressing containers.

```python
class BoostUnorderedFcaPrinter:
    def __init__(self, val):
        self.val = val
        self.name = f"{self.val.type.strip_typedefs()}".split("<")[0]
        self.name = self.name.replace("boost::unordered::", "boost::")
        self.is_map = self.name.endswith("map")

    def to_string(self):
        size = self.val["table_"]["size_"]
        return f"{self.name} with {size} elements"
```

The `__init__` function of any pretty-printer class takes a `val` parameter and stores it, which is the `gdb.Value` of the matched object itself. Then we can determine some properties of its properties by simple string manipulations.

I wanted the template name of the type. Sometimes just calling `.type` returns an alias, but I wanted the concrete typename, so I added `.strip_typedefs()`. Then I grabbed everything in the typename string before the first "`<`" character, and stored it as `self.name`. This is the template name. All the Boost.Unordered containers are defined in the `boost::unordered` namespace, but they are lifted with `using` into the `boost` namespace. I would rather store the name without the middle `unordered` namespace.

How do we know if this type is a map or a set? It's easy in this library. We can just check if the template name ends in "`map`". We can achieve all this introspection with string operations on the typename. This `self.is_map` will be useful later, when we're iterating the elements of the container. Map elements and set elements get displayed differently, so we need to know which paradigm to use. Map elements are displayed as `[key] = value`, while set elements are displayed as `[index] = value`. We'll be able to branch on `self.is_map` to display in 2 different ways. This was impossible in natvis, and was the reason that almost all of the code was duplicated.

Then to display the container in the `to_string(self)` function, I'm only outputting the template name and the size. Later this will act as the prefix, and it is equivalent to what happens in the standard library pretty-printers. So far, this is what a printout may look like.

```
(gdb) print my_unordered_map
$1 = boost::unordered_map with 3 elements
(gdb) print my_unordered_multiset
$2 = boost::unordered_multiset with 5 elements
```

As I mentioned a few sections above, a string interpolation calls into the type's own pretty-printer if such printer exists. For example, take the f-string `f"{self.name} with {size} elements"`. The variable `self.name` is already a string, so there's nothing special going on here. However, `size` is a `gdb.Value` holding an integer, so it gets displayed as an integer. If the `gdb.Value`'s type has a pretty-printer defined for it, it will be displayed using the rules in that pretty-printer, even from a different script, as long as it's properly registered with GDB.

<br>

## Iterating and displaying the elements

To display the Boost.Unordered containers, I wanted to match standard practice. This means matching both how the standard unordered containers are displayed, as well as matching some common practice by other people who have implemented similar pretty-printers previously. Fortunately, it turns out that these are one and the same. The idea looks like this.

```
(gdb) print my_example_map
$1 = boost::any_given_map with 3 elements = {["C"] = "c", ["B"] = "b", ["A"] = "a"}
(gdb) print my_example_set
$2 = boost::any_given_set with 3 elements = {[0] = "c", [1] = "b", [2] = "a"}
```

For iterators, I took inspiration from how I displayed iterators in the natvis implementation. Any valid iterator displays its element inside braces, and the end iterator is simply displayed as "`{ end iterator }`".

```
(gdb) print my_example_map_begin
$1 = iterator = { {first = "C", second = "c"} }
(gdb) print my_example_map_end
$2 = iterator = { end iterator }
(gdb) print my_example_set_begin
$3 = iterator = { "c" }
(gdb) print my_example_set_end
$4 = iterator = { end iterator }
```

Unfortunately, unlike natvis, this doesn't leave room for displaying other things like the function objects (`key_eq`, `hash`, `allocator`) or the stats. Natvis has the option to create different "views" of the visualization, which can display more items, fewer items, or modified items. From my understanding, pretty-printers don't allow that, so there must be 1 canonical visualization of a type. I'll get back to the stats later.

To actually iterate the elements, I am using exactly the same algorithm copied from my natvis implementation, so it isn't interesting to talk about. The helper functions like `match_occupied()` and `is_sentinel()` are much simpler to write with the pretty-printing API.

Here is what it looks like to iterate the elements. The `to_string()` function only needs to output the "prefix" (`f"{self.name} with {size} elements"`), and the elements are displayed through a different avenue. You must define a `display_hint()` function and a `children()` function to iterate the elements. The function `display_hint()` returns a string, which is set to `"map"` in this case, to indicate the formatting of `{[something] = something, [something] = something, etc}`. For this library, this is the formatting we want for the map containers _and_ the set containers. Then `children()` needs to output a generator for the elements. When we have a `display_hint()` of `"map"`, each _pair_ of values from the generator constitutes 1 element, "`[first] = second`".

```python
def display_hint(self):
    return "map"

def children(self):
    def generator():
        # ...
        while condition:
            value = # ...
            if self.is_map:
                first = value["first"]
                second = value["second"]
                yield "", first
                yield "", second
            else:
                yield "", count
                yield "", value
    return generator()
```

Here is where `self.is_map` comes in handy. When we have a "map" type, we want to display each element as "`[key] = value`". For a "set" type, we want "`[index] = value`". To achieve this same behaviour in natvis, I needed to duplicate the entire implementation and modify the 1 line where we output the item. With the pretty-printing API, we can avoid duplicating all that code.

The result looks exactly like I described above.

```
(gdb) print my_example_map
$1 = boost::any_given_map with 3 elements = {["C"] = "c", ["B"] = "b", ["A"] = "a"}
(gdb) print my_example_set
$2 = boost::any_given_set with 3 elements = {[0] = "c", [1] = "b", [2] = "a"}
```

Note that we didn't _need_ to use the `display_hint()` and `children()` functions. We could have put everything inside of `to_string()`. The benefit of the more technique is that GDB _knows_ that we're printing the object's children and it adds certain things to the formatting, like the "` = {...}`", as well as the "`[first] = second`" formatting in the case of `display_hint() == "map"`, neither of which we explicitly specified.

Even further, using `children()` gives GDB the ability to make the formatting look even nicer. Compare these 2 printouts below. This would need to be emulated manually if we only used `to_string()`, but the capability is inherent when we use `children()`.

```
(gdb) print my_example_map
$1 = boost::any_given_map with 3 elements = {["C"] = "c", ["B"] = "b", ["A"] = "a"}
(gdb) print -p -- my_example_map
$2 = boost::any_given_map with 3 elements = {
    ["C"] = "c",
    ["B"] = "b",
    ["A"] = "a"
}
```

<br>

## Customization points

The Boost.Unordered containers also support allocators that use fancy pointers, such as `boost::interprocess::offset_ptr` from the Boost.Interprocess library. In C++, these class types are given overloaded operators that allow them to behave with the same semantics as pointers. When writing a helper for a debugger, like a GDB pretty-printer or a natvis visualizer, we don't have these luxuries.

In [this previous article](/NatvisForUnordered2/), I showed how I injected user-defined behaviour into the natvis visualizations. The method I used for GDB pretty-printers is surprisingly similar.

The key lies in a function documented [here](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Pretty-Printing-API.html), which I'll quote below.

> GDB provides a function which can be used to look up the default pretty-printer for a `gdb.Value`:
>
> Function: **gdb.default_visualizer** *(value)*
>
> &nbsp;&nbsp;&nbsp;&nbsp;This function takes a `gdb.Value` object as an argument. If a pretty-printer for this value exists, then it is returned. If no such printer exists, then this returns `None`.

In short, this function allows us to grab a type's pretty-printer, if it exists. This means that a user can write specific functions for their fancy pointer's pretty-printer, which we can then call from the Boost.Unordered pretty-printers. Unlike natvis, there's no need to overload with SFINAE-like overload sets. Here is the code for the customization point, taken verbatim from the Python script at the time of writing this article.

```python
class BoostUnorderedPointerCustomizationPoint:
    def __init__(self, any_ptr):
        vis = gdb.default_visualizer(any_ptr)
        if vis is None:
            self.to_address = lambda ptr: ptr
            self.next = lambda ptr, offset: ptr + offset
        else:
            self.to_address = lambda ptr: ptr if (ptr.type.code == gdb.TYPE_CODE_PTR) else type(vis).boost_to_address(ptr)
            self.next = lambda ptr, offset: type(vis).boost_next(ptr, offset)
```

This customization point sets itself up with 2 functions, `to_address(ptr)` and `next(ptr, offset)`. If there is no visualizer available, i.e. the "`vis is None`" branch, then we must be using raw pointers, so we will do the basic operations. On the other hand, if we have a visualizer then we will use it. In this case, we call into the visualizer's static functions `boost_to_address(fancy_ptr)` and `boost_next(raw_ptr, offset)`. With the hindsight of my natvis implementation, I was able to take that previous work and translate it almost directly into the pretty-printing API.

I used these lambda definitions in the same way that I would use a constraint in C++ to choose between different function overloads in a class template. I preferred this approach instead of a branch inside the function itself.

Here is what the customization point looks like in action. In the printer for the closed-addressing container, the constructor creates a `self.cpo` variable.

```python
class BoostUnorderedFcaPrinter:
    def __init__(self, val):
        self.val = val
        # ...
        any_ptr = self.val["table_"]["buckets_"]["buckets"]
        self.cpo = BoostUnorderedPointerCustomizationPoint(any_ptr)
```

The nested member `.table_.buckets_.buckets` may be a raw pointer or a fancy pointer, so `self.cpo` will either be initialized with raw pointer functionality or fancy pointer functionality. No user should actually know about these hidden members though.

Any time we need the `to_address()` and `next()` functions in the pretty-printer code, we call into them using `self.cpo`. For example, the `children()` function looks like this below.

```python
def children(self):
    def generator():
        grouped_buckets = self.val["table_"]["buckets_"]

        size = grouped_buckets["size_"]
        buckets = grouped_buckets["buckets"]
        bucket_index = 0

        count = 0
        while bucket_index != size:
            current_bucket = self.cpo.next(self.cpo.to_address(buckets), bucket_index)
            # ...
    return generator()
```

The variable `buckets` may be a raw pointer or a fancy pointer. To access it uniformly as a raw pointer, just call `self.cpo.to_address(buckets)`, which always returns a raw pointer if the customization points were written correctly. In this case we actually want to evaluate `buckets + bucket_index`. This requires the raw version of `buckets` to be offset by `bucket_index` through the `self.cpo.next()` function, because fancy pointers may have arbitrary rules for what "incrementing" or "offsetting" means. Here, the user can specify that as in their pretty-printer for their own type, and it will work seamlessly with Boost.Unordered.

The result is that GDB can have an identical printout for the Boost.Unordered containers whether they are using `std::allocator` or they are using an allocator from Boost.Interprocess that uses `boost::interprocess::offset_ptr`. This is extended for anyone else who writes a printer for their own type with the proper overloaded functions.

Instructions and documentation for how to do this are given in the [Python script itself](https://github.com/boostorg/unordered/blob/develop/extra/boost_unordered_printers.py).

<br>

## Synthesized member functions: GDB xmethods

This section is about more of the GDB API than just the pretty-printing.

Further up, I mentioned about the stats objects. Since Boost 1.86, all Boost.Unordered open-addressing containers support an opt-in for statistical metrics. Displaying this in natvis makes sense in all cases, because Visual Studio has collapsible items. Even if there is a lot of information, we can hide it away behind an unexpanded item. For the GDB pretty-printers, outputting the stats alongside the container elements would be obtrusive.

At first I wanted to output the stats using a [custom command](https://sourceware.org/gdb/current/onlinedocs/gdb.html/CLI-Commands-In-Python.html), `print_stats`. When you call `print the_map`, you get the regular printout. On the other hand, `print_stats the_map` would internally call `print the_map.table_.cstats` and return the output. All the extra print options could be forwarded along. Unfortunately, using the name `print_stats` for such a specialty command is too big of a land grab, and any other name is no longer ergonomic. I left this up [as a GitHub Gist](https://gist.github.com/k3DW/19f0acbaef749efd9414527ef45cf113). If I want to write a similar custom command in the future, I already have a code sample to use as a basis.

In the end, I used a [GDB xmethod](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Xmethod-API.html), which allows you to define class member functions that are callable from GDB. Ultimately, `print the_map` will print the elements regularly, and `print the_map.get_stats()` will print the stats. This is the most sensical solution because `get_stats()` is a function that already exists in the C++ code. However, in the C++ code this function has side-effects like synchronization, plus we need to be able to call this code from GDB even if it had never been instantiated in C++. Therefore, an xmethod is the right choice.

I followed [this tutorial](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Writing-an-Xmethod.html) very closely when writing the xmethod, so I don't think there's anything interesting I can say here, other than pointing you to the tutorial.

<br>

## Conclusion

I already said this above, in the section titled "Setting up GDB for pretty-printing", but I want to reiterate it: If you are compiling for the ELF format and you haven't defined the `BOOST_ALL_NO_EMBEDDED_GDB_PRINTERS` macro, you already have the pretty-printer script embedded in your binary. Of course, this opt-out does exist for those who don't want the extra bytes. My goal was to make this beginner-friendly, to be helpful to people who need it the most.

The GDB pretty-printers for Boost.Unordered will be available in Boost release 1.87. If you want it sooner, it's available in the `develop` branch of the [Boost.Unordered Github](https://github.com/boostorg/unordered/tree/develop/extra/boost_unordered_printers.py).

This has been a very exciting project. I hope it can help people more easily debug their containers.

I want to thank [Niall Douglas](https://github.com/ned14) for help in making the embedded pretty-printer scripts work properly. This is a huge usability and adoptability feature. He and I collaborated on a [script](https://github.com/ned14/quickcpplib/blob/master/scripts/generate_gdb_printer.py) to automatically transform a Python GDB pretty-printing script into a C header with the correct assembly to embed the GDB script.

In the future, I am interested in more tools related to debuggability and visualizations. Namely, I plan on contributing to tools that make it easier and more accessible for developers to write and test their debugger visualizations. We all spend a lot of our time debugging code. As library authors and contributors, we should provide the mechanisms for more easily debugging through the features we've written.

Thanks for reading!
