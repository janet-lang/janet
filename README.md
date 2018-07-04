# dst

[![Build Status](https://travis-ci.org/bakpakin/dst.svg?branch=master)](https://travis-ci.org/bakpakin/dst)
[![Appveyor Status](https://ci.appveyor.com/api/projects/status/32r7s2skrgm9ubva?svg=true)](https://ci.appveyor.com/project/bakpakin/dst)

Dst is a functional and imperative programming language and bytecode interpreter. It is a
modern lisp, but lists are replaced
by other data structures with better utility and performance (arrays, tables, structs, tuples).
The language can also easily bridge to native code written in C, and supports abstract datatypes
for interfacing with C. Also support meta programming with macros, and bytecode assembly for the
dst abstract machine. The bytecode vm is a register based vm loosely inspired by the LuaJIT
bytecode format, but simpler and safer (bytecode can be verified by the assembler).

There is a repl for trying out the language, as well as the ability
to run script files. This client program is separate from the core runtime, so
dst could be embedded into other programs.

Implemented in mostly standard C99, dst runs on Windows, Linux and macOS.
The few features that are not standard C (dynamic library loading, compiler specific optimizations),
are fairly straight forward. Dst can be easily ported to new platforms.

There is not much in the way of documentation yet because it is still a "personal project" and
I don't want to freeze features prematurely. You can look in the examples directory, the test directory,
or the file `src/core/core.dst` to get a sense of what dst code looks like.

For syntax highlighting, there is some preliminary vim syntax highlighting in [dst.vim](https://github.com/bakpakin/dst.vim).
Generic lisp syntax highlighting should, however, provide good results.

## Features

* First class closures
* Garbage collection
* First class green threads (continuations)
* Mutable and immutable arrays (array/tuple)
* Mutable and immutable hashtables (table/struct)
* Mutable and immutable strings (buffer/string)
* Lisp Macros
* Byte code interpreter with an assembly interface, as well as bytecode verification
* Proper tail calls.
* Direct interop with C via abstract types and C functions
* Dynamically load C libraries
* Functional and imperative standard library
* Lexical scoping
* Imperative programming as well as functional
* REPL
* Interactive environment with detailed stack traces
* SQLite bindings

## Documentation

API documentation and design documents can be found in the
[wiki](https://github.com/bakpakin/dst/wiki). Not at all complete.

## Usage

A repl is launched when the binary is invoked with no arguments. Pass the -h flag
to display the usage information. Individual scripts can be run with `./dst myscript.dst`

If you are looking to explore, you can print a list of all available macros, functions, and constants
by entering the command `(all-symbols)` into the repl.

```
$ ./dst
Dst 0.0.0 alpha  Copyright (C) 2017-2018 Calvin Rose
dst:1:> (+ 1 2 3)
6
dst:2:> (print "Hello, World!")
Hello, World!
nil
dst:3:> (os.exit)
$ ./dst -h
usage: ./dst [options] scripts...
Options are:
  -h Show this help
  -v Print the version string
  -s Use raw stdin instead of getline like functionality
  -e Execute a string of dst
  -r Enter the repl after running all scripts
  -p Keep on executing if there is a top level error (persistent)
  -- Stop handling option
$
```

## Compiling and Running

Dst can be built with Make or CMake.
Use Make if you are on a posix system and don't like CMake.
Use CMake if you are on Windows or like CMake.

### Make

```sh
cd somewhere/my/projects/dst
make
make test
```

### CMake

On a posix system using make as the target build system, 
compiling and running is as follows (this is the same as
most CMake based projects).

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

## Examples

See the examples directory for some example dst code.

## SQLite bindings

There are some sqlite3 bindings in the directory natives/sqlite3. They serve mostly as a
proof of concept external c library. To use, first compile the module with Make.

```sh
make natives
```

Next, enter the repl and create a database and a table.

```
dst:1:> (import natives.sqlite3 :as sql)
nil
dst:2:> (def db (sql.open "test.db"))
<sqlite3.connection 0x5561A138C470>
dst:3:> (sql.eval db `CREATE TABLE customers(id INTEGER PRIMARY KEY, name TEXT);`)
@[]
dst:4:> (sql.eval db `INSERT INTO customers VALUES(:id, :name);` {:name "John" :id 12345})
@[]
dst:5:> (sql.eval db `SELECT * FROM customers;`)
@[{"id" 12345 "name" "John"}]
```

Finally, close the database connection when done with it.

```
dst:6:> (sql.close db)
nil
```
