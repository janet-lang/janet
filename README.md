# dst

[![Build Status](https://travis-ci.org/bakpakin/dst.svg?branch=master)](https://travis-ci.org/bakpakin/dst)
[![Appveyor Status](https://ci.appveyor.com/api/projects/status/32r7s2skrgm9ubva?svg=true)](https://ci.appveyor.com/project/bakpakin/dst)

dst is a functional programming language and vm. The language is a lisp that replaces
the list with other data structures that have better realworld characteristics and performance.
The language can also easily bridge to native code, and 
native useful datatypes. The bytecode vm is a register based vm loosely inspired
by the LuaJIT bytecode format. 

There is a repl for trying out the language, as well as the ability
to run script files. This client program is separate from the core runtime, so
dst could be embedded into other programs.

## Features

* First class closures
* Garbage collection
* Lexical scoping
* First class green threads (continuations)
* Mutable and immutable arrays (array/tuple)
* Mutable and immutable hashtables (table/struct)
* Mutable and immutable strings (buffer/string)
* Lisp Macros
* Byte code interpreter with an assembly interface, as well as bytecode verification
* Proper tail calls.
* Direct interop with C via abstract types and C functions
* Dynamically load C libraries
* REPL

## Compiling and Running

Dst is built using CMake. There used to be a hand-written Makefile, but in the interest of 
easier Windows support I have switched to CMake.

On a posix system using make, compiling and running is as follows (this is the same as 
most CMake based projects).

### Build
```sh
cd somewhere/my/projects/dst
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make test
```

The repl can also be run with the CMake run target.
```sh
make run
```
