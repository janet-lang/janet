# janet

[![Build Status](https://travis-ci.org/bakpakin/janet.svg?branch=master)](https://travis-ci.org/bakpakin/janet)
[![Appveyor Status](https://ci.appveyor.com/api/projects/status/32r7s2skrgm9ubva?svg=true)](https://ci.appveyor.com/project/bakpakin/janet)

Janet is a functional and imperative programming language and bytecode interpreter. It is a
modern lisp, but lists are replaced
by other data structures with better utility and performance (arrays, tables, structs, tuples).
The language also bridging bridging to native code written in C, meta-programming with macros, and bytecode assembly.

There is a repl for trying out the language, as well as the ability
to run script files. This client program is separate from the core runtime, so
janet could be embedded into other programs. Try janet in your browser at 
[https://janet-lang.org](https://janet-lang.org).

Implemented in mostly standard C99, janet runs on Windows, Linux and macOS.
The few features that are not standard C (dynamic library loading, compiler specific optimizations),
are fairly straight forward. Janet can be easily ported to new platforms.

For syntax highlighting, there is some preliminary vim syntax highlighting in [janet.vim](https://github.com/bakpakin/janet.vim).
Generic lisp syntax highlighting should, however, provide good results.

## Use Cases

Janet makes a good system scripting language, or a language to embed in other programs. Think Lua or Guile.

## Features

* Minimal setup - one binary and you are good to go!
* First class closures
* Garbage collection
* First class green threads (continuations)
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
* Interactive environment with detailed stack traces

## Documentation

API documentation and design documents can be found in the
[wiki](https://github.com/bakpakin/janet/wiki). There is an introduction
section in the wiki that contains a good overview of the language.

## Usage

A repl is launched when the binary is invoked with no arguments. Pass the -h flag
to display the usage information. Individual scripts can be run with `./janet myscript.janet`

If you are looking to explore, you can print a list of all available macros, functions, and constants
by entering the command `(all-symbols)` into the repl.

```
$ ./janet
Janet 0.0.0 alpha  Copyright (C) 2017-2018 Calvin Rose
janet:1:> (+ 1 2 3)
6
janet:2:> (print "Hello, World!")
Hello, World!
nil
janet:3:> (os.exit)
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

## Compiling and Running

Janet only uses Make and batch files to compile on Posix and windows
respectively. To configure janet, edit the header file src/include/janet/janet.h
before compilation.

### Posix

On most platforms, use Make to build janet.

```sh
cd somewhere/my/projects/janet
make
make test
```

After building, run `make install` to install the janet binary and libs.
Will install in `/usr/local` by default, see the Makefile to customize.

It's also recommended to set the `JANET_PATH` variable in your profile.
This is where janet will look for imported libraries after the current directory.

### Windows

1. Install [Visual Studio](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=Community&rel=15#)
or [Visual Studio Build Tools](https://visualstudio.microsoft.com/thank-you-downloading-visual-studio/?sku=BuildTools&rel=15#)
2. Run a Visual Studio Command Prompt (cl.exe and link.exe need to be on the PATH) and cd to the directory with janet.
3. Run `build_win` to compile janet.
4. Run `build_win test` to make sure everything is working.

### Emscripten

To build janet for the web via [Emscripten](https://kripken.github.io/emscripten-site/), make sure you
have `emcc` installed and on your path. On a linux or macOS system, use `make janet.js` to build
`janet.js` and `janet.wasm` - both are needed to run janet in a browser or in node.
The JavaScript build is what runs the repl on the main website,
but really serves mainly as a proof of concept. Janet will run much slower in a browser.
Building with emscripten on windows is currently unsupported.

## Examples

See the examples directory for some example janet code.

## SQLite bindings

There are some sqlite3 bindings in the directory natives/sqlite3. They serve mostly as a
proof of concept external c library. To use, first compile the module with Make.

```sh
make natives
```

Next, enter the repl and create a database and a table.

```
janet:1:> (import natives.sqlite3 :as sql)
nil
janet:2:> (def db (sql.open "test.db"))
<sqlite3.connection 0x5561A138C470>
janet:3:> (sql.eval db `CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT);`)
@[]
janet:4:> (sql.eval db `INSERT INTO customers VALUES(:id, :name);` {:name "John" :id 12345})
@[]
janet:5:> (sql.eval db `SELECT * FROM customers;`)
@[{"id" 12345 "name" "John"}]
```

Finally, close the database connection when done with it.

```
janet:6:> (sql.close db)
nil
```
