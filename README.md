# gst

GST is a language and vm that is small and embeddable, has metaprogramming
facilities, can interoperate with C, and has enough features to make it
a useful general purpose programming language. It is a modern variant of
Lisp with several native useful datatypes. Some of the more interesting and
useful features are first class functions and closures, immutable and mutable
hashtables, arrays, and bytebuffers, macros (NYI), tail-call optimization,
and continuations (coroutines, error handling). The runtime and bootstrapping
compiler are written in C99, but should eventually be completely compatible
with C89 compilers.

As of April 2017, still WIP. While the basic runtime is in place, as are many
native functions, several important features are still being implemented and
defined, like continuations and macros. These are also dependant on the
compiler being ported to gst, which is ongoing. The module system is also not yet
fully defined.

Currently, there is a repl for trying out the language, as well as the ability
to run script files. This client program is separate from the core runtime, so
gst could be embedded into other programs.

## Compiling and Running

Clone the repository and run:
```sh
make run
```
To build the runtime and launch a repl. The GNU readline library is
also required for the repl/client, but is not a dependency of the gst runtime.

## Basic programs

To run some basic programs, run the client with one argument, the name of the
file to run. For example, the client that is built will be located in the
client directory, so running a program `script.gst` from the project directory
 would be

```sh
client/gst script.gst 
```

## Todo

* Use serialization to allow creation of bytecode files that can be loaded.
  This includes defining a file format for the bytecode files.
* Better error messages, expecially for compilation. This probably means string
  formating functions.
* Pattern matching/regex library.
* Macro/specials system that happens before compilation
* Module system that happens before compilation. This would be closely tied to
  the macro/specials system (require implemented as compile time special). For
  now, a runtime require might make more sense.
* Change name (gst is the name of many projects, including GNU Smalltalk).
  Maybe make logo :)?
* Automated testing, including Makefile target.
* Travis CI.
* Change C API to be stack based for fewer potential memory management
  problems. This could mean making current C API internal and use separate
  API externally.
* Store source information in parallel data structure after parsing
* Use source information during compilation
* Bitwise operators that operate on integer types
* Use Lua style memory alocator backend C API (one single function for
  allocating/reallocating/freeing memory).
* More CLI functionality for main client
* Document builtin libraries and functions.
* Document C API.
* More builtin libraires.
* Fuzzing

