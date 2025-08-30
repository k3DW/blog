---
title: "How to use the libc++ GDB pretty-printers"
layout: post
permalink: /LibcxxPrettyPrinters/
tags: [ pretty-print ]
---

Last year I wrote [an article](/NatvisTesting/) about my attempt so far at a system for testing Natvis files automatically. Here, I wanted to write the equivalent article but for GDB pretty-printers, for which I have [an earlier article](/PrettyPrinter/). As I said in the Natvis testing article:

> I want the CI to fail if I accidentally break the visualizers. Right now, I only know there's something wrong if I check with my own eyes, or if someone files a bug report. To me, this isn't good enough. It's too much of a maintenance burden.

I originally intended to spend this article creating a minimal framework for testing GDB pretty-printers. I started to write it alongside my attempt to make this framework, as a documentation resource for myself and others: If I document the pitfalls I experience, hopefully I won't experience them again. I didn't intend to get derailed by libc++, so it turns out _that_ is where the article went. Getting libc++'s pretty-printers to work just as the libstdc++ ones work has been a big enough challenge that it's worth an entire article.

<!--more-->

I gave a talk last month at CppNorth 2025 titled "Debugger Visualizers to Make Your Code Accessible", where I went through a comprehensive look at why and how to set up Natvis and GDB pretty-printers for your project. While the majority of the time was spend on the "how", my emphasis was on the "why". **If you are writing code for other people to use, you should do your best to make that code easily accessible to those people, which includes writing debugger visualizers as companions to that code.**

I wanted to give a springboard to those in the audience and bring down the barrier to entry. I believe this is very important. It's also what I'm doing with this article.

CppNorth is another story, for another post sometime. Maybe a CppNorth 2025 trip report, but for now I'm too fixated on automating GDB. For the most part, the sections of this article are in chronological order of what I know. I wrote each section knowing only enough to write the section, and dealing with the associated problems. Let's get into it.

<br>

## The goals

1. Figure out how to automate GDB and read its output
2. Create some system that tests the running of GDB on an executable, against a given expected output

<br>

## Where am I starting from?

I've already been banging my proverbial head against the metaphorical wall for a few hours at this point on the automation, before starting to write this article. So I'll take a section to fill in the details of where I'm starting from.

As it turns out, goal 1 is orders of magnitude easier than goal 2.

Until starting to work on this, I didn't know that GDB has a "run these commands and then exit" mode. This is known as ["batch" mode](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Mode-Options.html), with the flag `-batch`. I'm sure unit testing isn't what `-batch` was originally intended to do, but it's perfect for this use case.

It will run the commands specified by the flags `-ex` and `-x`.

* With `-ex` (short form of `-eval-command`), you specify a command directly, such as `-ex "print my_object"`.
* With `-x` (short form of `-command`), you specify a file containing commands. These commands are run in order immediately.
* The order of commands run is exactly the order as specified by these flags.

For example, let's say I have a file called "commands.txt" with these commands.

```none
file a.out
break main.cpp:6
```

Then I run the following `gdb` command.

```bash
gdb -batch \
    -ex "source path/to/printer.py" \
    -x commands.txt \
    -ex "run"
```

In this case it will do `source`, `file`, `break`, `run`. There should be no surprises here.

<br>

## A minimal working example

For a real-life example, I started with the ["visualization_tests.cpp" file](https://github.com/boostorg/unordered/blob/f734e399e33c29f3e7d5548a4f04a8afd3f79a6d/test/debuggability/visualization_tests.cpp) I wrote for Boost.Unordered.

1. Compile the file into an executable "sample"
2. Write a short "commands.txt"
3. Try running it

Here is my "commands.txt". In Boost.Unordered, as of 1.87, the pretty-printers are embedded in your ELF executable by default, so there's no need to load the pretty-printers here.

```none
set print pretty on
file build/sample
break visualization_tests.cpp:120
run
print fca_set
```

Now I run `gdb -batch -x commands.txt` and see what happens.

```none
$ gdb -batch -x commands.txt
Breakpoint 1 at 0xcd89: visualization_tests.cpp:120. (2 locations)
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".

Breakpoint 1.1, visualization_test<default_tester_> (tester=...) at .../visualization_tests.cpp:120
120       goto break_here;
$1 = boost::unordered_set with 5 elements = {
  [0] = "6",
  [1] = "0",
  [2] = "2",
  [3] = "8",
  [4] = "4"
}
```

This is perfect. We have a reliable way to run GDB and get the output

<br>

## Trials up to this point

Earlier I said I was banging my head against the wall. It's because of a few simple mistakes I didn't notice, and ended up spending a few hours fixing the wrong things. Maybe you've also experienced a similar sequence of mistakes.

I compiled with `-DBOOST_ALL_NO_EMBEDDED_GDB_SCRIPTS` to disable embedding the printer in the executable, as I may want to make live modifications. Then on my first attempt with `gdb -batch` I encountered my first mistake: I loaded the script "boost_unordered_printer.py". If you're as astute as I am, you won't see what's wrong with this. The actual name of the file says "printers" whereas I wrote "printer" singular. This took over an hour to figure out...

Next, I was getting output that looked like this. The ellipses are my own, after removing pages of text.

```none
$1 = boost::unordered_set with 5 elements = {
  [0] = {
    static __endian_factor = 2,
    __r_ = {
      <std::__1::__compressed_pair_elem<std::__1::basic_string<...
      ...}
    static npos = 18446744073709551615
  },
  [1] = {...
  },
  ...
}
```

I checked `info pretty-printer` inside GDB, and the standard library printers weren't even loaded! How is that even possible? Depending on how well you know the internals of the popular C++ standard library implementations, you might recognize this as libc++. It took me a long time to notice, but once I did, I knew the issue. I was compiling with Clang with `-stdlib=libc++`, something I've never done with pretty-printers before. That was mistake number 2 so far.

To remove variability, and to just get a minimal working example, I switched to GCC with libstdc++. That is, I just removed `-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="-stdlib=libc++"` from my `cmake` command. After this, it works as I showed in the section above, with the correctly displayed `unordered_set`.

Now that that's under control, let's switch back to Clang, but not deal with libc++ yet.

```none
$1 = boost::unordered_set with 5 elements = {
  [0] = Python Exception <class 'gdb.error'>: There is no member named _M_p.
,
  [1] = Python Exception <class 'gdb.error'>: There is no member named _M_p.
,
  [2] = Python Exception <class 'gdb.error'>: There is no member named _M_p.
,
  [3] = Python Exception <class 'gdb.error'>: There is no member named _M_p.
,
  [4] = Python Exception <class 'gdb.error'>: There is no member named _M_p.

}
```

Oh no.

When I encountered this problem, I decided to take a break from trying to make it work, and start writing this article instead. Luckily, as I made it to this section right now, I remembered that I dealt with a similar problem last year. Surprise, this is actually a rubber-ducking session.

Last year I was having problems with getting the correct GDB output when using Clang. I can't retrace my actual steps, but I came across [this GitHub issue](https://github.com/dotnet/runtime/issues/90791#issuecomment-1684394378) with a comment saying:

> I've found that adding `-glldb` option to the compiler options fixes the problem

After this, the output is correct with Clang with libstdc++. Now you're caught up to where I am.

<br>

## Side note: CMake and `-stdlib=libc++`

Up until this point, I had these lines in my CMakeLists.txt.

```cmake
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif()
```

Yes I know this isn't ideal practice for production code, and these things shouldn't be hard-coded into the CMakeLists.txt, but right now I'm looking for ease of iteration. This is a quick and dirty way to give me 1 less thing to remember to do.

But I wanted to make it slightly less "quick and dirty" for the sake of this article, so I decided to rewrite this as some sort of [generator expression](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html). Unfortunately, this sapped another hour trying to debug.

(Actually the _real_ story is that I first started working on the auto-loading stuff in the next section below, and then I couldn't link against libc++ anymore. I spent way too long trying to fix the auto-loading, before realizing I had actually made a simple CMake mistake, and I hadn't in fact irreparably damaged my entire `/usr/share` directory.)

I want to do this for all targets in the project, not just for this current one, in case I add other targets for further testing. Again, this will just be 1 less thing to bite me later. So instead of using `target_compile_options` for every target, I decided to use `add_compile_options`.

```cmake
add_compile_options($<$<CXX_COMPILER_ID:Clang>:-stdlib=libc++>)
```

Seems simple enough. To check that it works, I added a conditional `#error` to the source.

```cpp
#ifndef _LIBCPP_VERSION
#error Not using libc++
#endif
```

The error was firing. So `add_compile_options` didn't work. Backtracking and using `target_compile_options` instead, I was getting gargantuan linker errors, boiling down to being unable to link against libc++. I added a corresponding `target_link_options` call with the same arguments, and that fixed the issue.

Turning both of those functions from their `target_*` variant to their global `add_*` variant, the error was firing again. With the error disabled, the compile succeeded, using libstdc++.

Anyway, to make a long story short, I just needed to specify `add_compile_options` and `add_link_options` ***before*** the call to `add_executable`. That's it. That solved it. Here is what I have.

```cmake
add_compile_options($<$<CXX_COMPILER_ID:Clang>:-stdlib=libc++>)
add_link_options($<$<CXX_COMPILER_ID:Clang>:-stdlib=libc++>)
add_executable(...)
```

I had never run into this before, so I'm documenting it for future reference. I wasted too much time on this tiny issue for it to go undocumented.

Don't use the global version of these commands. But if you have to, be careful about the calling order.

<br>

## GDB pretty-printers for libc++

Using the new-found CMake insight, I add `-stdlib=libc++` back to the compile options for Clang and I get that same issue from before. There aren't any printers loaded for the standard library types.

```none
$1 = boost::unordered_set with 5 elements = {
  [0] = {
    static __endian_factor = 2,
    __r_ = {
      <std::__1::__compressed_pair_elem<std::__1::basic_string<...
      ...}
    static npos = 18446744073709551615
  },
  ...
}
```

As a matter of fact, after a bit of digging, I don't think the libc++ pretty-printers are installed on my system at all. They exist out in the world, but not here. I assumed I would already have the pretty-printers, since I already have packages `libc++-<N>-dev`, `libc++abi-<N>-dev`, and `clang-<N>`. I hope I'm wrong here and they actually are installed with some package, but I haven't been able to find them.

The printers are [available on the "llvm-project" GitHub](https://github.com/llvm/llvm-project/blob/main/libcxx/utils/gdb/libcxx/printers.py). I downloaded this Python file, and then I tried loading it into GDB. I added this command.

```none
source path/to/printers.py
```

Still nothing happens. That's because this script itself doesn't run anything on its own. It does expose a function though, `register_libcxx_printer_loader` at the very bottom of the script. I just need to call this function from within GDB to load the printers.

```none
source path/to/printers.py
python register_libcxx_printer_loader()
```

With these commands added to GDB, the output is still failing, but it's different.

```none
$1 = boost::unordered_set with 5 elements = {
  [0] = Python Exception <class 'gdb.error'>: There is no member or method named __rep_.
,
  [1] = Python Exception <class 'gdb.error'>: There is no member or method named __rep_.
,
  [2] = Python Exception <class 'gdb.error'>: There is no member or method named __rep_.
,
  [3] = Python Exception <class 'gdb.error'>: There is no member or method named __rep_.
,
  [4] = Python Exception <class 'gdb.error'>: There is no member or method named __rep_.

}
```

I looked into it and realized that the implementation of `std::basic_string` [changed within the last year](https://github.com/llvm/llvm-project/commit/27c83382d83dce0f33ae67abb3bc94977cb3031f#diff-f53db39e97bedb6f59c0092b732ca224a7f03b9ae14c39fc3d1d85bc2d1110ffR202). While I downloaded the latest version of "printers.py", I am using libc++ from LLVM 17.0.6, nearly 2 years ago. After downloading the "printers.py" file from the correct tag of the llvm-project repo, here is the new output.

```none
$1 = boost::unordered_set with 5 elements = {
  [0] = "6",
  [1] = "0",
  [2] = "2",
  [3] = "8",
  [4] = "4"
}
```

Finally, success!

Unfortunately, this isn't even full success for my ultimate goal. I want to test GDB pretty-printers, and I've done all this work just to _use_ pretty-printers with libc++.

<br>

## Side note: The prior art of testing pretty-printers

[I found the diff](https://reviews.llvm.org/D65609) where the "printers.py" file was first added into libc++. To my surprise, this also included tests for the pretty-printers! That's incredible, this is exactly what I want to do! There is [a Python file](https://github.com/llvm/llvm-project/blob/main/libcxx/test/libcxx/gdb/gdb_pretty_printer_test.py) and [a C++ file](https://github.com/llvm/llvm-project/blob/main/libcxx/test/libcxx/gdb/gdb_pretty_printer_test.sh.cpp).

I would also be remiss if I didn't mention Dmitry Arkhipov's ["debugger_utils" library](https://github.com/cppalliance/debugger_utils), which may be proposed for inclusion into Boost as a tool in the future. Included in this library is [a script](https://github.com/cppalliance/debugger_utils/blob/develop/embed-gdb-extension.py) for embedding pretty-printers into an ELF binary, which looks like a more robust version of [the script](https://github.com/ned14/quickcpplib/blob/master/scripts/generate_gdb_printer.py) that Niall Douglas and I developed. Additionally there is [a script](https://github.com/cppalliance/debugger_utils/blob/develop/generate-gdb-test-runner.py) and a framework for testing the GDB pretty-printers.

I too want to work on a pretty-printer test framework, as well as some tools to ensure that working with pretty-printers is as easy as possible. Once I begin working on the testing aspect, these will be required reading for me. I recommend taking a look if you're interested.

<br>

## Learning about GDB auto-loading

I still had this burning question. Why do the libc++ printers require all this additional work, while the libstdc++ printers work out of the box, without the `source` command followed by calling a `python` function?

The answer, this happens for libstdc++ because of auto-loading, a topic I know next-to-nothing about. From the GDB docs:

> GDB sometimes reads files with commands and settings automatically, without being explicitly told so by the user. We call this feature _auto-loading_. While auto-loading is useful for automatically adapting GDB to the needs of your project, it can sometimes produce unexpected results or introduce security risks (e.g., if the file comes from untrusted sources).

Now, after looking into it further, I know a little bit more than nothing. Next-to-next-to-nothing one might say. From my understanding, the following is how GDB auto-loads a script, using libstdc++ as the example.

* Checking `ldd build/sample`, my binary dynamically links against "libstdc++.so.6", located at "/lib/x86_64-linux-gnu/libstdc++.so.6".
* The real path of this shared object is "/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32", with all the links stripped away using `readlink -f <path>`.
* The full path of the auto-loaded Python script will be the real path of the SO with "/usr/share/gdb/auto-load" prepended and "-gdb.py" appended.
* In my case, that means the auto-loaded script is "/usr/share/gdb/auto-load/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32-gdb.py".

Looking at this script, I see some conditional updates to the `os.path` to ensure "/usr/share/gcc/python" is part of the path. At this directory, "./libstdcxx/v6" is the location to the libstdc++ pretty-printers themselves. Therefore, once the path is added to `os.path`, this script loads the printers.

```py
from libstdcxx.v6 import register_libstdcxx_printers
register_libstdcxx_printers(gdb.current_objfile())
```

The last piece of the puzzle is a line I have previously added to my user GDB settings, which for me is a file called "~/.config/gdb/gdbinit".

```none
~/.config/gdb$ cat gdbinit
...
add-auto-load-safe-path /usr/share/gdb/auto-load/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32-gdb.py
...
```

I have a bunch of other lines in this file, but this `add-auto-load-safe-path` is the one that matters. This line is gating whether the auto-loading will actually happen. If you don't have this line, you'll get a warning when GDB tries to auto-load this script.

```none
warning: File "/usr/share/gdb/auto-load/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32-gdb.py" auto-loading has been declined by your `auto-load safe-path' set to "...etc..."
To enable execution of this file add
        add-auto-load-safe-path /usr/share/gdb/auto-load/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.32-gdb.py
line to your configuration file "/home/braden/.config/gdb/gdbinit".
To completely disable this security protection add
        set auto-load safe-path /
line to your configuration file "/home/braden/.config/gdb/gdbinit".
For more information about this security protection see the
"Auto-loading safe path" section in the GDB manual.  E.g., run from the shell:
        info "(gdb)Auto-loading safe path"
```

The fix is spelled out explicitly right here, but it's still something that needs to be done manually.

<br>

## Auto-loading libc++ pretty-printers

I'll go through a similar line of reasoning that I did for libstdc++ above.

* Checking `ldd build/sample`, my binary links against "libc++.so.1" at the path "/usr/lib/x86_64-linux-gnu/libc++.so.1".
* In my case currently, the real path is actually "/usr/lib/llvm-17/lib/libc++.so.1.0".
* Therefore my GDB auto-load script should be called "/usr/share/gdb/auto-load/usr/lib/llvm-17/lib/libc++.so.1.0-gdb.py".
* This script does not yet exist, but I will make it.

The contents of the auto-loading script will be much simpler than GCC's, as I am doing this in a relatively quick and dirty way. I downloaded the pretty-printers to "/usr/local/share/gdb/libcxx/libcxx_printers_tag_llvmorg_17_0_6.py". In the auto-loading script:

1. Add "/usr/local/share/gdb/libcxx" to `os.path` if it's not already present.
2. Import `libcxx_printers_tag_llvmorg_17_0_6`
3. Then call `register_libcxx_printer_loader()`

It's a simple process, there are just a few finicky steps to get right. Now that I've done it, running GDB on my executable I see this.

```none
warning: File "/usr/share/gdb/auto-load/usr/lib/llvm-17/lib/libc++.so.1.0-gdb.py" auto-loading has been declined by your `auto-load safe-path' set to ...
...etc...
```

Perfect! This means it sees the script. After adding it as an auto-load safe path, I finally see this.

```
Breakpoint 1 at 0x8604: visualization_tests.cpp:120. (2 locations)
Loading libc++ pretty-printers.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/lib/x86_64-linux-gnu/libthread_db.so.1".

Breakpoint 1.1, visualization_test<default_tester_> (tester=...) at .../visualization_tests.cpp:120
120       goto break_here;
$1 = boost::unordered_set with 5 elements = {
  [0] = "6",
  [1] = "0",
  [2] = "2",
  [3] = "8",
  [4] = "4"
}
```

It worked. After all this, it worked.

<br>

## Setting up libc++ pretty-printer auto-loading with a script

More than just writing an article, I've actually been working on a Python script to automate this process. You can check out ["install_libcxx_printers.py"](https://github.com/k3DW/debug/blob/f9f1a1a9a9a5562177265a55596e32839d684d64/gdb/install_libcxx_printers.py) if you're interested to use it. My script does the following.

1. Download libc++'s "printers.py" file from the given git tag with `-t`/`--tag`, from the given git branch with `-b`/`--branch`, or from the given git commit hash with `-c`/`--commit`. These are optional, mutually exclusive arguments. Rename the file based on the tag, branch, or commit.
2. The download directory is optionally set by `-d`/`--download-to`, otherwise it defaults to "/usr/local/share/gdb/libcxx".
3. The target libc++ SO is optionally set with `-l`/`--libcxx-so`, otherwise it defaults to "/usr/lib/x86_64-linux-gnu/libc++.so.1".
4. If a tag, branch, or commit are not set, then the appropriate version is inferred using the `-l` argument.
5. The inferred version is checked against the installed packages on the system using the Linux command `dpkg -l`, and the script will fail if they don't match up.
6. After downloading the pretty-printers successfully, generate an auto-load script at the appropriate location for the given libc++ SO.

It's been a fun exercise to write this script, and it means that (hopefully) I won't have to go through this process again. As I mentioned above, if you're interested, [check it out](https://github.com/k3DW/debug/blob/f9f1a1a9a9a5562177265a55596e32839d684d64/gdb/install_libcxx_printers.py). I was also going to do the `set-auto-load-safe-path` gdbinit modification too, but I decided against it. First, I don't think there's a universal location where this file will be located, and you can't simply query GDB for it from my understanding. Secondly, This doesn't feel very security-minded. I would rather not tamper with these files in an automated script, and instead let the user do it.

I do wish that an installation of libc++ automatically did these things. I want the printers to already be available on my system, for the correct version of libc++ I have installed. I also want the GDB auto-load script to be created/installed in the correct location for the libc++ installation. The pretty-printers already exist, so the hard part has been taken care of. This is just the plumbing.

Hopefully a future version of LLVM will make my script obsolete.

<br>

## Going forward

From start to finish, this article took me a week, and it took me in a completely different direction from where I wanted to go. As I said at the beginning, this article was meant to be about the journey of setting up automated testing for GDB pretty-printers. In the end, it's still about that, but a few steps behind where I thought I would be. Writing an article while working through this issue has actually helped me quite a lot. I've retained the information much better, since I have an "audience" to teach while I'm learning.

Soon I'll start working on the actual GDB testing. That's my ultimate goal. I want some sort of simple framework to write tests for my GDB pretty-printers. Whether that takes the form of a script, or guidelines, or something else entirely, I don't know. We'll see what happens.

For now, I hope this journey of mine has helped someone else to understand the plumbing that goes into using GDB pretty-printers.
