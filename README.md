[![Join the chat](https://badges.gitter.im/janet-language/community.svg)](https://gitter.im/janet-language/community)
&nbsp;
[![Appveyor Status](https://ci.appveyor.com/api/projects/status/bjraxrxexmt3sxyv/branch/master?svg=true)](https://ci.appveyor.com/project/bakpakin/janet/branch/master)
[![Build Status](https://travis-ci.org/janet-lang/janet.svg?branch=master)](https://travis-ci.org/janet-lang/janet)
[![builds.sr.ht status](https://builds.sr.ht/~bakpakin/janet/.freebsd.yaml.svg)](https://builds.sr.ht/~bakpakin/janet/.freebsd.yaml?)
[![builds.sr.ht status](https://builds.sr.ht/~bakpakin/janet/.openbsd.yaml.svg)](https://builds.sr.ht/~bakpakin/janet/.openbsd.yaml?)

<img src="https://raw.githubusercontent.com/janet-lang/janet/master/assets/janet-w200.png" alt="Janet logo" width=200 align="left">

**Janet** is a functional and imperative programming language and bytecode interpreter. It is a
modern lisp, but lists are replaced
by other data structures with better utility and performance (arrays, tables, structs, tuples).
The language also supports bridging to native code written in C, meta-programming with macros, and bytecode assembly.

There is a repl for trying out the language, as well as the ability
to run script files. This client program is separate from the core runtime, so
janet could be embedded into other programs. Try janet in your browser at
[https://janet-lang.org](https://janet-lang.org).

<br>

## Use Cases

Janet makes a good system scripting language, or a language to embed in other programs. Think Lua or Guile.

## Features

* Minimal setup - one binary and you are good to go!
* First class closures
* Garbage collection
* First class green threads (continuations)
* Python style generators (implemented as a plain macro)
* Mutable and immutable arrays (array/tuple)
* Mutable and immutable hashtables (table/struct)
* Mutable and immutable strings (buffer/string)
* Lisp Macros
* Byte code interpreter with an assembly interface, as well as bytecode verification
* Tailcall Optimization
* Direct interop with C via abstract types and C functions
* Dynamically load C libraries
* Functional and imperative standard library
* Lexical scoping
* Imperative programming as well as functional
* REPL
* Parsing Expression Grammars built in to the core library
* 300+ functions and macros in the core library
* Embedding Janet in other programs
* Interactive environment with detailed stack traces

## Documentation

* For a quick tutorial, see [the introduction](https://janet-lang.org/docs/index.html) for more details.
* For the full API for all functions in the core library, see [the core API doc](https://janet-lang.org/api/index.html)

Documentation is also available locally in the repl.
Use the `(doc symbol-name)` macro to get API
documentation for symbols in the core library. For example,
```
(doc doc)
```
Shows documentation for the doc macro.

To get a list of all bindings in the default
environment, use the `(all-bindings)` function.

## Source

You can get the source on [GitHub](https://github.com/janet-lang/janet) or
[SourceHut](https://git.sr.ht/~bakpakin/janet). While the GitHub repo is the official repo,
the SourceHut mirror is actively maintained.

## Building

### macos and Unix-like

The Makefile is non-portable and requires GNU-flavored make.

```
cd somewhere/my/projects/janet
make
make test
make repl
```

### 32-bit Haiku

32-bit Haiku build instructions are the same as the unix-like build instructions,
but you need to specify an alternative compiler, such as `gcc-x86`.

```
cd somewhere/my/projects/janet
make CC=gcc-x86
make test
make repl
```

### FreeBSD

FreeBSD build instructions are the same as the unix-like build instuctions,
but you need `gmake` to compile. Alternatively, install directly from
packages, using `pkg install lang/janet`.

```
cd somewhere/my/projects/janet
gmake
gmake test
gmake repl
```

### Windows

1. Install [Visual Studio](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Community&rel=15#) or [Visual Studio Build Tools](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=BuildTools&rel=15#)
2. Run a Visual Studio Command Prompt (cl.exe and link.exe need to be on the PATH) and cd to the directory with janet.
3. Run `build_win` to compile janet.
4. Run `build_win test` to make sure everything is working.

### Emscripten

To build janet for the web via [Emscripten](https://kripken.github.io/emscripten-site/), make sure you
have `emcc` installed and on your path. On a linux or macOS system, use `make emscripten` to build
`janet.js` and `janet.wasm` - both are needed to run janet in a browser or in node.
The JavaScript build is what runs the repl on the main website,
but really serves mainly as a proof of concept. Janet will run slower in a browser.
Building with emscripten on windows is currently unsupported.

### Meson

Janet also has a build file for [Meson](https://mesonbuild.com/), a cross platform build
system. Although Meson has a python dependency, Meson is a very complete build system that
is maybe more convenient and flexible for integrating into existing pipelines.
Meson also provides much better IDE integration than Make or batch files, as well as support
for cross compilation.

For the impatient, building with Meson is as simple as follows. The options provided to
`meson setup` below emulate Janet's Makefile.

```sh
git clone https://github.com/janet-lang/janet.git
cd janet
meson setup build \
          --buildtype release \
          --optimization 2 \
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
best option, as it has excellent meson integration. It also offers code completion
for Janet's C API right out of the box, which is very useful for exploring.

## Installation

See [the Introduction](https://janet-lang.org/introduction.html) for more details. If you just want
to try out the language, you don't need to install anything. You can also simply move the `janet` executable wherever you want on your system and run it.

## Usage

A repl is launched when the binary is invoked with no arguments. Pass the -h flag
to display the usage information. Individual scripts can be run with `./janet myscript.janet`

If you are looking to explore, you can print a list of all available macros, functions, and constants
by entering the command `(all-bindings)` into the repl.

```
$ ./janet
Janet 0.0.0 alpha  Copyright (C) 2017-2018 Calvin Rose
janet:1:> (+ 1 2 3)
6
janet:2:> (print "Hello, World!")
Hello, World!
nil
janet:3:> (os/exit)
$ ./janet -h
usage: ./janet [options] scripts...
Options are:
  -h Show this help
  -v Print the version string
  -s Use raw stdin instead of getline like functionality
  -e Execute a string of janet
  -r Enter the repl after running all scripts
  -p Keep on executing if there is a top level error (persistent)
  -- Stop handling option
$
```

If installed, you can also run `man janet` to get usage information.

## Embedding

The C API for Janet is not yet documented but coming soon.

Janet can be embedded in a host program very easily. There is a make target
`make amalg` which creates the file `build/janet.c`, which is a single C file
that contains all the source to Janet. This file, along with
`src/include/janet.h` and `src/include/janetconf.h` can dragged into any C
project and compiled into the project. Janet should be compiled with `-std=c99`
on most compilers, and will need to be linked to the math library, `-lm`, and
the dynamic linker, `-ldl`, if one wants to be able to load dynamic modules. If
there is no need for dynamic modules, add the define
`-DJANET_NO_DYNAMIC_MODULES` to the compiler options.

## Examples

See the examples directory for some example janet code.

## Discussion

Feel free to ask questions and join discussion on the [Janet Gitter Channel](https://gitter.im/janet-language/community).
Alternatively, check out [the #janet channel on Freenode](https://webchat.freenode.net/)

## FAQ

### Why is my terminal is spitting out junk when I run the repl?

Make sure your terminal supports ANSI escape codes. Most modern terminals will
support these, but some older terminals, windows consoles, or embedded terminals
will not. If your terminal does not support ANSI escape codes, run the repl with
the `-n` flag, which disables color output. You can also try the `-s` if further issues
ensue.

## Why Janet

Janet is named after the almost omniscient and friendly artificial being in [The Good Place](https://en.wikipedia.org/wiki/The_Good_Place).

<img src="https://raw.githubusercontent.com/janet-lang/janet/master/assets/janet-the-good-place.gif" alt="Janet logo" width="115px" align="left">
