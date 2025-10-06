---
title: "buffalo::buffalo::buffalo..."
layout: post
permalink: /Buffalo/
tags: [ misc ]
---

This is a quick post about something I can't get out of my head.

This came up in a "hallway track" at CppCon 2025 last month, as a spin-off of a conversation about Clang's [-Wdtor-name](https://clang.llvm.org/docs/DiagnosticsReference.html#wdtor-name) error. The following is real code that actually compiles.

```cpp
struct buffalo {
    buffalo();
};
buffalo::buffalo::buffalo::buffalo::buffalo::buffalo::buffalo::buffalo() {
    // ...
}
```

It turns out that the [famous, technically grammatically correct sentence](https://en.wikipedia.org/wiki/Buffalo_buffalo_Buffalo_buffalo_buffalo_buffalo_Buffalo_buffalo) is implementable in C++. Who knew. I'm baffled enough that it stuck in my mind and I need an explanation.

<!--more-->

<br>

## Dumping the AST

Before going any further, I just want to see what the AST looks like for different incarnations of this pattern. Let's check the 2 following pieces of code.

```cpp
struct buffalo {
    buffalo();
};
buffalo::buffalo() {
}
```

```cpp
struct buffalo {
    buffalo();
};
buffalo::buffalo::buffalo::buffalo::buffalo::buffalo::buffalo::buffalo() {
}
```

It turns out, Clang produces exactly the same AST. You can check for yourself [on Compiler Explorer](https://godbolt.org/z/nn7czcE6G), using compiler flags `-Xclang -ast-dump`.

Other than the memory addresses, which change if you look at them funny, only the column numbers change. See the following 2 snippets.

```none
`-CXXConstructorDecl 0x1688c1b0 parent 0x1688b9e8 prev 0x1688bc40 <line:4:1, line:5:1> line:4:10 buffalo 'void ()'
  `-CompoundStmt 0x1688c2e0 <col:20, line:5:1>
```

```none
`-CXXConstructorDecl 0x27320380 parent 0x2731faa8 prev 0x2731fd00 <line:4:1, line:5:1> line:4:64 buffalo 'void ()'
  `-CompoundStmt 0x273204b0 <col:74, line:5:1>
```

This proves that the 2 pieces of code are exactly equivalent, even in the AST.

<br>

## Injected-class-name

I had previously heard of the "injected class name" of a type before, but I had never given it much thought outside of templates. To me, an injected class name was the mechanism that allows us to write the name of a class template without its template parameters, when you're inside that class template. I had never looked any further, and assumed this was the main purpose.

For example, we write this code

```cpp
template <class T>
struct S {
  S(int);
};
```

instead of this code

```cpp
template <class T>
struct S {
  S<T>(int);
};
```

It turns out I was wrong, this is just a nice side effect. This mechanism, the _injected-class-name_ is present in all classes, whether templated or not. This is the first section of [the explanation on cppreference](https://en.cppreference.com/w/cpp/language/injected-class-name.html).

> In a class scope, the class name of the current class or the template name of the current class template is treated as if it were a public member name; this is called _injected-class-name_. The point of declaration of the name is immediately following the opening brace of the class (template) definition.

Basically, there's a secret alias in the above code equivalent to `using S = S<T>`. In the `buffalo` code, it would be `using buffalo = buffalo`.

It's a mechanism to ensure that name lookup, [in the words of Jonathan Wakely](https://stackoverflow.com/a/25549691), "always finds the current class". For example, if you have `int X` and `struct X` both at global scope, `X` will always refer to `struct X` anywhere inside the struct definition. To access `int X`, you need to qualify it as `::X`.

Here is an example from [[basic.lookup.elab]](https://eel.is/c++draft/basic.lookup.elab) in the standard, if you're interested to read it.

```cpp
struct Node {
    struct Node* Next; // OK, refers to injected-class-name Node
    struct Data* Data; // OK, declares type Data at global scope and member Data
};
```

Or see [[class.pre/2]](https://eel.is/c++draft/class.pre#2) for the definition of _injected-class-name_ in the standard itself.

[The cppreference page](https://en.cppreference.com/w/cpp/language/injected-class-name.html) mentions the following line that has some consequences for how I will see all C++ code going forward.

> Constructors do not have names, but the injected-class-name of the enclosing class is considered to name a constructor in constructor declarations and definitions.

So... Every time I write `S::S()`, I'm not directly writing the "name" of the constructor, but rather I'm using the injected-class-name to refer to the constructor... Strange.

<br>

## This keeps happening

If I have a class called `buffalo`, then `buffalo::buffalo` names itself. Clearly if it names itself, then I can tack on another `::buffalo` ad infinitum.

Going further, this can actually be used in various places all over the language. I showed it above for an out-of-line constructor definition. As another example, you can also create a variable by referring to the class that way. You just need another keyword. The following are all equivalent.

```cpp
buffalo b1{};
struct buffalo::buffalo::buffalo b2{};
auto b3 = typename buffalo::buffalo::buffalo::buffalo::buffalo::buffalo::buffalo::buffalo{};
```

"Hey wouldn't it be cool if this worked?" And then it turns out it does.

It seems like people keep running up against this and getting perplexed by it. There's the [original StackOverflow question](https://stackoverflow.com/questions/25549652/why-is-there-an-injected-class-name), but there are a bunch more that I keep finding [(1)](https://stackoverflow.com/questions/71874114/why-classnameaclassnameavariable-in-c-working?noredirect=1&lq=1) [(2)](https://stackoverflow.com/questions/65358148/is-abbbb-bf-right-why-could-i-do-that?noredirect=1&lq=1) [(3)](https://stackoverflow.com/questions/46805449/why-is-the-code-foofoofoofoob-compiling?noredirect=1&lq=1).

<br>

## Back to `-Wdtor-name`

The reason this all started. [The following code](https://godbolt.org/z/Tno67b3n9) is considered to be incorrect in fully conformant ISO C++.

```cpp
struct outer {
    template <class T>
    struct inner {
        ~inner();
    };
};
template <class T>
outer::inner<T>::~inner() {
}
```

Instead, the out-of-line destructor must use the injected class name of `inner` at least once. Here is the technically correct spelling.

```cpp
template <class T>
outer::inner<T>::inner::~inner() {
}
```

<br>

## One more technically grammatically correct sentence

```cpp
namespace james_while_john {
    struct had {
        void a_better_effect_on_the_teacher();
    };
};
void james_while_john::had::had::had::had::had::had::had::had::had::had::had::a_better_effect_on_the_teacher() {
}
```

[See Wikipedia](https://en.wikipedia.org/wiki/James_while_John_had_had_had_had_had_had_had_had_had_had_had_a_better_effect_on_the_teacher)
