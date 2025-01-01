[![Join the chat](https://img.shields.io/badge/zulip-join_chat-brightgreen.svg)](https://janet.zulipchat.com)
&nbsp;
[![builds.sr.ht status](https://builds.sr.ht/~bakpakin/janet/commits/master/freebsd.yml.svg)](https://builds.sr.ht/~bakpakin/janet/commits/master/freebsd.yml?)
[![builds.sr.ht status](https://builds.sr.ht/~bakpakin/janet/commits/master/openbsd.yml.svg)](https://builds.sr.ht/~bakpakin/janet/commits/master/openbsd.yml?)
[![Actions Status](https://github.com/janet-lang/janet/actions/workflows/test.yml/badge.svg)](https://github.com/janet-lang/janet/actions/workflows/test.yml)

<img src="https://raw.githubusercontent.com/janet-lang/janet/master/assets/janet-w200.png" alt="Janet logo" width=200 align="left">

**Janet** is a programming language for system scripting, expressive automation, and
extending programs written in C or C++ with user scripting capabilities.

Janet makes a good system scripting language, or a language to embed in other programs.
It's like Lua and GNU Guile in that regard. It has more built-in functionality and a richer core language than
Lua, but smaller than GNU Guile or Python. However, it is much easier to embed and port than Python or Guile.

There is a REPL for trying out the language, as well as the ability
to run script files. This client program is separate from the core runtime, so
Janet can be embedded in other programs. Try Janet in your browser at
<https://janet-lang.org>.

<br>

## Examples

See the examples directory for all provided example programs.

### Game of Life

```janet
# John Conway's Game of Life

(def- window
  (seq [x :range [-1 2]
         y :range [-1 2]
         :when (not (and (zero? x) (zero? y)))]
       [x y]))

(defn- neighbors
  [[x y]]
  (map (fn [[x1 y1]] [(+ x x1) (+ y y1)]) window))

(defn tick
  "Get the next state in the Game Of Life."
  [state]
  (def cell-set (frequencies state))
  (def neighbor-set (frequencies (mapcat neighbors state)))
  (seq [coord :keys neighbor-set
         :let [count (get neighbor-set coord)]
         :when (or (= count 3) (and (get cell-set coord) (= count 2)))]
      coord))

(defn draw
  "Draw cells in the game of life from (x1, y1) to (x2, y2)"
  [state x1 y1 x2 y2]
  (def cellset @{})
  (each cell state (put cellset cell true))
  (loop [x :range [x1 (+ 1 x2)]
         :after (print)
         y :range [y1 (+ 1 y2)]]
    (file/write stdout (if (get cellset [x y]) "X " ". ")))
  (print))

# Print the first 20 generations of a glider
(var *state* '[(0 0) (-1 0) (1 0) (1 1) (0 2)])
(for i 0 20
  (print "generation " i)
  (draw *state* -7 -7 7 7)
  (set *state* (tick *state*)))
```

### TCP Echo Server

```janet
# A simple TCP echo server using the built-in socket networking and event loop.

(defn handler
  "Simple handler for connections."
  [stream]
  (defer (:close stream)
    (def id (gensym))
    (def b @"")
    (print "Connection " id "!")
    (while (:read stream 1024 b)
      (printf " %v -> %v" id b)
      (:write stream b)
      (buffer/clear b))
    (printf "Done %v!" id)
    (ev/sleep 0.5)))

(net/server "127.0.0.1" "8000" handler)
```

### Windows FFI Hello, World!

```janet
# Use the FFI to popup a Windows message box - no C required

(ffi/context "user32.dll")

(ffi/defbind MessageBoxA :int
  [w :ptr text :string cap :string typ :int])

(MessageBoxA nil "Hello, World!" "Test" 0)
```

## Language Features

* 600+ functions and macros in the core library
* Built-in socket networking, threading, subprocesses, and file system functions.
* Parsing Expression Grammars (PEG) engine as a more robust Regex alternative
* Macros and compile-time computation
* Per-thread event loop for efficient IO (epoll/IOCP/kqueue)
* First-class green threads (continuations) as well as OS threads
* Erlang-style supervision trees that integrate with the event loop
* First-class closures
* Garbage collection
* Distributed as janet.c and janet.h for embedding into a larger program.
* Python-style generators (implemented as a plain macro)
* Mutable and immutable arrays (array/tuple)
* Mutable and immutable hashtables (table/struct)
* Mutable and immutable strings (buffer/string)
* Tail recursion
* Interface with C functions and dynamically load plugins ("natives").
* Built-in C FFI for when the native bindings are too much work
* REPL development with debugger and inspectable runtime

## Documentation

* For a quick tutorial, see [the introduction](https://janet-lang.org/docs/index.html) for more details.
* For the full API for all functions in the core library, see [the core API doc](https://janet-lang.org/api/index.html).

Documentation is also available locally in the REPL.
Use the `(doc symbol-name)` macro to get API
documentation for symbols in the core library. For example,
```
(doc apply)
```
shows documentation for the `apply` function.

To get a list of all bindings in the default
environment, use the `(all-bindings)` function. You
can also use the `(doc)` macro with no arguments if you are in the REPL
to show bound symbols.

## Source

You can get the source on [GitHub](https://github.com/janet-lang/janet) or
[SourceHut](https://git.sr.ht/~bakpakin/janet). While the GitHub repo is the official repo,
the SourceHut mirror is actively maintained.

## Building

### macOS and Unix-like

The Makefile is non-portable and requires GNU-flavored make.

```sh
cd somewhere/my/projects/janet
make
make test
make repl
make install
make install-jpm-git
```

Find out more about the available make targets by running `make help`.

### 32-bit Haiku

32-bit Haiku build instructions are the same as the UNIX-like build instructions,
but you need to specify an alternative compiler, such as `gcc-x86`.

```sh
cd somewhere/my/projects/janet
make CC=gcc-x86
make test
make repl
make install
make install-jpm-git
```

### FreeBSD

FreeBSD build instructions are the same as the UNIX-like build instructions,
but you need `gmake` to compile. Alternatively, install the package directly with `pkg install lang/janet`.

```sh
cd somewhere/my/projects/janet
gmake
gmake test
gmake repl
gmake install
gmake install-jpm-git
```

### NetBSD

NetBSD build instructions are the same as the FreeBSD build instructions.
Alternatively, install the package directly with `pkgin install janet`.

### Windows

1. Install [Visual Studio](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Community&rel=15#) or [Visual Studio Build Tools](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=BuildTools&rel=15#).
2. Run a Visual Studio Command Prompt (`cl.exe` and `link.exe` need to be on your PATH) and `cd` to the directory with Janet.
3. Run `build_win` to compile Janet.
4. Run `build_win test` to make sure everything is working.

To build an `.msi` installer executable, in addition to the above steps, you will have to:

5. Install, or otherwise add to your PATH the [WiX 3.14 Toolset](https://github.com/wixtoolset/wix3/releases).
6. Run `build_win dist`.

Now you should have an `.msi`. You can run `build_win install` to install the `.msi`, or execute the file itself.

### Meson

Janet also has a build file for [Meson](https://mesonbuild.com/), a cross-platform build
system. Although Meson has a Python dependency, Meson is a very complete build system that
is maybe more convenient and flexible for integrating into existing pipelines.
Meson also provides much better IDE integration than Make or batch files, as well as support
for cross-compilation.

For the impatient, building with Meson is as follows. The options provided to
`meson setup` below emulate Janet's Makefile.

```sh
git clone https://github.com/janet-lang/janet.git
cd janet
meson setup build \
          --buildtype release \
          --optimization 2 \
          --libdir /usr/local/lib \
          -Dgit_hash=$(git log --pretty=format:'%h' -n 1)
ninja -C build

# Run the binary
build/janet

# Installation
ninja -C build install
```

## Development

Janet can be hacked on with pretty much any environment you like, but for IDE
lovers, [Gnome Builder](https://wiki.gnome.org/Apps/Builder) is probably the
best option, as it has excellent Meson integration. It also offers code completion
for Janet's C API right out of the box, which is very useful for exploring. VSCode, Vim,
Emacs, and Atom each have syntax packages for the Janet language, though.

## Installation

If you just want to try out the language, you don't need to install anything.
In this case you can also move the `janet` executable wherever you want on
your system and run it.  However, for a fuller setup, please see the
[Introduction](https://janet-lang.org/docs/index.html) for more details.

## Usage

A REPL is launched when the binary is invoked with no arguments. Pass the `-h` flag
to display the usage information. Individual scripts can be run with `./janet myscript.janet`.

If you are looking to explore, you can print a list of all available macros, functions, and constants
by entering the command `(all-bindings)` into the REPL.

```
$ janet
Janet 1.7.1-dev-951e10f  Copyright (C) 2017-2020 Calvin Rose
janet:1:> (+ 1 2 3)
6
janet:2:> (print "Hello, World!")
Hello, World!
nil
janet:3:> (os/exit)
$ janet -h
usage: janet [options] script args...
Options are:
  -h : Show this help
  -v : Print the version string
  -s : Use raw stdin instead of getline like functionality
  -e code : Execute a string of janet
  -E code arguments... : Evaluate an expression as a short-fn with arguments
  -d : Set the debug flag in the REPL
  -r : Enter the REPL after running all scripts
  -R : Disables loading profile.janet when JANET_PROFILE is present
  -p : Keep on executing if there is a top-level error (persistent)
  -q : Hide logo (quiet)
  -k : Compile scripts but do not execute (flycheck)
  -m syspath : Set system path for loading global modules
  -c source output : Compile janet source code into an image
  -i : Load the script argument as an image file instead of source code
  -n : Disable ANSI color output in the REPL
  -l lib : Use a module before processing more arguments
  -w level : Set the lint warning level - default is "normal"
  -x level : Set the lint error level - default is "none"
  -- : Stop handling options
```

If installed, you can also run `man janet` to get usage information.

## Embedding

Janet can be embedded in a host program very easily. The normal build
will create a file `build/janet.c`, which is a single C file
that contains all the source to Janet. This file, along with
`src/include/janet.h` and `src/conf/janetconf.h`, can be dragged into any C
project and compiled into it. Janet should be compiled with `-std=c99`
on most compilers, and will need to be linked to the math library, `-lm`, and
the dynamic linker, `-ldl`, if one wants to be able to load dynamic modules. If
there is no need for dynamic modules, add the define
`-DJANET_NO_DYNAMIC_MODULES` to the compiler options.

See the [Embedding Section](https://janet-lang.org/capi/embedding.html) on the website for more information.

## Discussion

Feel free to ask questions and join the discussion on the [Janet Zulip Instance](https://janet.zulipchat.com/)

## FAQ

### How fast is it?

It is about the same speed as most interpreted languages without a JIT compiler. Tight, critical
loops should probably be written in C or C++ . Programs tend to be a bit faster than
they would be in a language like Python due to the discouragement of slow Object-Oriented abstraction
with lots of hash-table lookups, and making late-binding explicit. All values are boxed in an 8-byte
representation by default and allocated on the heap, with the exception of numbers, nils and booleans. The
PEG engine is a specialized interpreter that can efficiently process string and buffer data.

The GC is simple and stop-the-world, but GC knobs are exposed in the core library and separate threads
have isolated heaps and garbage collectors. Data that is shared between threads is reference counted.

YMMV.

### Where is (favorite feature from other language)?

It may exist, it may not. If you want to propose a major language feature, go ahead and open an issue, but
it will likely be closed as "will not implement". Often, such features make one usecase simpler at the expense
of 5 others by making the language more complicated.

### Is there a language spec?

There is not currently a spec besides the documentation at <https://janet-lang.org>.

### Is this Scheme/Common Lisp? Where are the cons cells?

Nope. There are no cons cells here.

### Is this a Clojure port?

No. It's similar to Clojure superficially because I like Lisps and I like the aesthetics.
Internally, Janet is not at all like Clojure, Scheme, or Common Lisp.

### Are the immutable data structures (tuples and structs) implemented as hash tries?

No. They are immutable arrays and hash tables. Don't try and use them like Clojure's vectors
and maps, instead they work well as table keys or other identifiers.

### Can I do object-oriented programming with Janet?

To some extent, yes. However, it is not the recommended method of abstraction, and performance may suffer.
That said, tables can be used to make mutable objects with inheritance and polymorphism, where object
methods are implemented with keywords.

```clj
(def Car @{:honk (fn [self msg] (print "car " self " goes " msg)) })
(def my-car (table/setproto @{} Car))
(:honk my-car "Beep!")
```

### Why can't we add (feature from Clojure) into the core?

Usually, one of a few reasons:
- Often, it already exists in a different form and the Clojure port would be redundant.
- Clojure programs often generate a lot of garbage and rely on the JVM to clean it up.
  Janet does not run on the JVM and has a more primitive garbage collector.
- We want to keep the Janet core small. With Lisps, a feature can usually be added as a library
  without feeling "bolted on", especially when compared to ALGOL-like languages. Adding features
  to the core also makes it a bit more difficult to keep Janet maximally portable.

### Can I bind to Rust/Zig/Go/Java/Nim/C++/D/Pascal/Fortran/Odin/Jai/(Some new "Systems" Programming Language)?

Probably, if that language has a good interface with C. But the programmer may need to do
some extra work to map Janet's internal memory model to that of the bound language. Janet
also uses `setjmp`/`longjmp` for non-local returns internally. This
approach is out of favor with many programmers now and doesn't always play well with other languages
that have exceptions or stack-unwinding.

### Why is my terminal spitting out junk when I run the REPL?

Make sure your terminal supports ANSI escape codes. Most modern terminals will
support these, but some older terminals, Windows consoles, or embedded terminals
will not. If your terminal does not support ANSI escape codes, run the REPL with
the `-n` flag, which disables color output. You can also try the `-s` flag if further issues
ensue.

## Why is it called "Janet"?

Janet is named after the almost omniscient and friendly artificial being in [The Good Place](https://en.wikipedia.org/wiki/The_Good_Place).
