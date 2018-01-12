# dst

[![Build Status](https://travis-ci.org/bakpakin/dst.svg?branch=master)](https://travis-ci.org/bakpakin/dst)

dst is a functional programming language and vm. It is a variant of
Lisp with several native useful datatypes. Some of the more interesting and
useful features are first class functions and closures, immutable and mutable
hashtables, arrays, and bytebuffers, macros (NYI), tail-call optimization,
and continuations (coroutines, error handling). The runtime and 
compiler are written in C99, but should eventually be completely compatible
with C89 compilers.

There is a repl for trying out the language, as well as the ability
to run script files. This client program is separate from the core runtime, so
dst could be embedded into other programs.

## Features

First class closures
Garbage collection
lexical scoping
First class green threads (continuations)
Mutable and immutable arrays (array/tuple)
Mutable and immutable hashtables (table/struct)
Mutable and immutable strings (buffer/string)
Byte code interpreter with an assembly interface
Proper tail calls for functional code
Direct interop with C



## Compiling and Running

Clone the repository and run:
```sh
make run
```
To build the runtime and launch a repl.

## Basic programs

To run some basic programs, run the client with one argument, the name of the
file to run. For example, the client that is built will be located in the
client directory, so running a program `script.dst` from the project directory
would be
```bash
client/dst script.dst
```

You can also use the `--help` option to see more usage information for the vm.

## Running Tests

Simply run `make test` to run the currently minimal test suite.

## Todo

* Documentation, with the caveat that things may change.
* Use serialization to allow creation of bytecode files that can be loaded.
  This includes defining a file format for the bytecode files. This mostly done.
  The byte code serialization could also be useful for the module system for loading artifacts.
* Pattern matching/regex library, as well as string formatting functions. This
  would also be useful for implementing better error messages.
* Better error messages, expecially for compilation. This probably means string
  formating functions.
* Macro/specials system that happens before compilation
* Module system. Something similar to node's require.
* Change name (dst is the name of many projects, including GNU Smalltalk).
  Maybe make logo :)?
* Change C API to be stack based for fewer potential memory management
  problems. This could mean making current C API internal and use separate
  API externally.
* Store source information in parallel data structure after parsing
* Use source information during compilation
* Use Lua style memory alocator backend C API (one single function for
  allocating/reallocating/freeing memory).
* More builtin libraires.
* Fuzzing

