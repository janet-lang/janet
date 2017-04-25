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

