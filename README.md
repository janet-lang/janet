# dst

[![Build Status](https://travis-ci.org/bakpakin/dst.svg?branch=master)](https://travis-ci.org/bakpakin/dst)

dst is a functional programming language and vm. It is a variant of
Lisp with several native useful datatypes. Some of the more interesting and
useful features are first class functions and closures, immutable and mutable
hashtables, arrays, and bytebuffers, macros, tail-call optimization,
and continuations. The runtime and 
compiler are written in C99.

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
* Byte code interpreter with an assembly interface, as well as bytecode verification
* Proper tail calls for functional code
* Direct interop with C via abstract types and C functions
* Dynamically load C libraries
* REPL (read eval print loop)

## Compiling and Running

To build the runtime and run test, run
```sh
make test
```

A repl can also be run with
```sh
make repl
```
