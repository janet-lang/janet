# Dst Language Introduction

Dst is a dynamic, lightweight programming language with strong functional
capabilities as well as support for imperative programming. It to be used
for short lived scripts as well as for building real programs. It can also
be extended with native code (C modules) for better performance and interfacing with
existing software. Dst takes ideas from Lua, Scheme, Racket, Clojure, Smalltalk, Erlang, and 
a whole bunch of other dynamic languages.

# Hello, world!

Following tradition, a simple Dst program will simply print "Hello, world!".

```
(print "Hello, world!")
```

Put the following code in a file call `hello.dst`, and run `./dst hello.dst`.
The words "Hello, world!" should be printed to the console, and then the program
should immediately exit. You now have a working dst program!

Alternatively, run the program `./dst` without any arguments to enter a REPL,
or read eval print loop. This is a mode where Dst functions like a calculator,
reading some input from stdin, evaluating it, and printing out the result, all
in an inifinte loop. This is a useful mode for exploring or prototyping in Dst.

This is about the simplest program one can write, and consists of precisely
three elements. This first element is the `print` symbol. This is a function
that simply prints its arguments to standard out. The second argument is the
string literal "Hello, world!", which is the one and only argument to the
print function. Lastly, the print symbol and the string literal are wrapped
in parentheses, forming a tuple. In Dst, parentheses and brackets are interchangeable,
brackets are used mostly when the resulting tuple is not a function call. The tuple
above indicates that the function `print` is to be called with one argument, `"Hello, world"`.

Like all lisps, all operations in Dst are in prefix notation; the name of the
operator is the first value in the tuple, and the arguments passed to it are
in the rest of the tuple.

# A bit more - Arithmetic

Any programming language will have some way to do arithmetic. Dst is no exception,
and supports the basic arithemtic operators

```
# Prints 13
# (1 + (2*2) + (10/5) + 3 + 4 + (5 - 6))
(print (+ 1 (* 2 2) (/ 10 5) 3 4 (- 5 6)))
```

Just like the print function, all arithmetic operators are entered in
prefix notation. Dst also supports the modulo operator, or `%`, which returns
the remainder of integer division. For example, `(% 10 3)` is 1, and `(% 10.5 3)` is
1.5. The lines that begin with `#` are comments.

Dst actually has two "flavors" of numbers; integers and real numbers. Integers are any
integer value between -2,147,483,648 and 2,147,483,647 (32 bit signed integer). 
Reals are real numbers, and are represented by IEEE-754 double precision floating point
numbers. That means that they can represent any number an integer can represent, as well
fractions to very high precision.

Although real numbers can represent any value an integer can, try to distinguish between
real numbers and integers in your program. If you are using a number to index into a structure,
you probably want integers. Otherwise, you may want to use reals (this is only a rule of thumb).

Arithmetic operator will convert integers to real numbers if needed, but real numbers
will not be converted to integers, as not all real numbers can be safely convert to integers.

## Numeric literals

Numeric literals can be written in many ways. Numbers can be written in base 10, with
underscores used to separate digits into groups. A decimal point can be used for floating
point numbers. Numbers can also be written in other bases by prefixing the number with the desired
base and the character 'r'. For example, 16 can be written as `16`, `1_6`, `16r10`, `4r100`, or `0x10`. The
`0x` prefix can be used for hexadecimal as it is so common. The radix must be themselves written in base 10, and
can be any integer from 2 to 36. For any radix above 10, use the letters as digits (not case sensitive).

Numbers can also be in scientific notation such as `3e10`. A custom radix can be used as well
as for scientific notation numbers, (the exponent will share the radix). For numbers in scientific
notation with a radix besides 10, use the `&` symbol to indicate the exponent rather then `e`.

## Arithmetic Functions

Besides the 5 main arithmetic functions, dst also supports a number of math functions
taken from the C libary `<math.h>`, as well as bitwise operators that behave like they
do in C or Java.

# Strings, Keywords and Symbols

Dst supports several varieties of types that can be used as labels for things in
your program. The most useful type for this purpose is the keyword type. A keyword
begins with a semicolon, and then contains 0 or more alphanumeric or a few other common
characters. For example, `:hello`, `:my-name`, `:=`, and `:ABC123_-*&^%$` are all keywords.
Keywords are actually just special cases of symbols, which are similar but don't start with
a semicolon. The difference between symbols and keywords is that keywords evaluate to themselves, while
symbols evaluate to whatever they are bound to. To have a symbol evaluate to itself, it must be
quoted.

```lisp
# Evaluates to :monday
:monday

# Will throw a compile error as monday is not defined
monday

# Quote it - evaluates to the symbol monday
'monday

# Or first define monday
(def monday "It is monday")

# Now the evaluation should work - monday evaluates to "It is monday"
monday
```

The most common thing to do with a keyword is to check it for equality or use it as a key into
a table or struct. Note that symbols, keywords and strings are all immutable. Besides making your
code easier to reason about, it allows for many optimizations involving these types.

```lisp
# Prints true
(= :hello :hello) 

# Prints false, everything in dst is case sensitive
(= :hello :HeLlO) 

# Look up into a table - evaluates to 25
(get {
    :name "John"
    :age 25
    :occupation "plumber"
} :age)
```

Strings can be used similarly to keywords, but there primary usage is for defining either text
or arbitrary sequences of bytes. Strings (and symbols) in dst are what is sometimes known as 
"8-bit clean"; they can hold any number of bytes, and are completely unaware of things like character
encodings. This is completely compatible with ASCII and UTF-8, two of the most common character
encodings. By being encoding agnostic, dst strings can be very simple, fast, and useful for 
for other uses besides holding text.

Literal text can be entered inside quotes, as we have seen above.

```
"Hello, this is a string."

# We can also add escape characters for newlines, double quotes, backslash, tabs, etc.
"Hello\nThis is on line two\n\tThis is indented\n"

# For long strings where you don't want to type a lot of escape characters,
# you can use 1 or more backticks (`\``) to delimit a string.
# To close this string, simply repeat the opening sequence of backticks
``
This is a string.
Line 2
    Indented
"We can just type quotes here", and backslashes \ no problem.
``
```

# Functions

Dst is a functional language - that means that one of the basic building blocks of your
program will be defining functions (the other is using data structures). Because dst
is a Lisp, functions are values just like numbers or strings - they can be passed around and
created as needed.

Functions can be defined with the `defn` macro, like so:

```lisp
(defn triangle-area [base height]
 (print "calculating area of a triangle...")
 (* base height 0.5))
```

A function defined with `defn` has a number of parts. First, it has it's name, triangle-area. This
is just a symbol used to access the function later. Next is the list of parameters this function takes,
in this case two parameters named base and height. Lastly, a function made with defn has
a number of body statements, which get executed each time the function is called. The last
form in the body is what the function evaluates to, or returns.

Once a function like the above one is defined, the programmer can use the `triangle-area`
function just like any other, say `print` or `+`.

```lisp
# Prints "calculating area of a triangle..." and then "25"
(print (triangle-area 5 10))
```

Note that when nesting function calls in other function calls like above (a call to triangle-area is
nested inside a call to print), the inner function calls are evaluated first. Also, arguments to
a function call are evaluated in order, from first argument to last argument).

Because functions are first-class values like numbers or strings, they can be passed
as arguments to other functions as well

```
(print triangle-area)
```

This prints the location in memory of the function triangle area. This idea can be used
to build some powerful constructs purely out of functions, or closures as they are known
in many contexts.

Functions don't need to have names. The `fn` keyword can be used to introduce function
literals without binding them to a symbol.

```
# Evaluates to 40
((fn [x y] (+ x x y)) 10 20)
```

The above expression first creates an anonymous function that adds twice
the first argument to the second, and then calls that function with arguments 10 and 20.
This will return (10 + 10 + 20) = 40.

There is a common macro `defn` that can be used for creating functions and immediately binding
them to a name. `defn` works as expected at both the top level and inside another form. There is also
the corresponding

```lisp
(defn myfun [x y]
 (+ x x y))

# You can think of defn as a shorthand for def and fn together
(def myfun-same (fn [x y]
            (+ x x Y)))

(myfun 3 4) # -> 10
```

Dst has many macros provided for you (and you can write your own).
Macros are just functions that take your source code
and transform it into some other source code, usually automating some repetitive pattern for you.

# Defs and Vars

Values can be bound to symbols for later use using the keyword `def`. Using undefined
symbols will raise an error.

```
(def a 100)
(def b (+ 1 a))
(def c (+ b b))
(def d (- c 100))
```

Bindings created with def have lexical scoping. Also, bindings created with def are immutable; they
cannot be changed after definition. For mutable bindings, like variables in other programming
languages, use the `var` keyword. The assignment special form `:=` can then be used to update
a var.

```
(var myvar 1)
(print myvar)
(:= myvar 10)
(print myvar)
```

In the global scope, you can use the `:private` option on a def or var to prevent it from
being exported to code that imports your current module. You can also add documentation to
a function by passing a string the def or var command.

```lisp
(def mydef :private "This will have priavte scope. My doc here." 123)
(var myvar "docstring here" 321)
```

## Scopes

Defs and vars (collectively known as bindings) live inside what is called a scope. A scope is
simply where the bindings are valid. If a binding is referenced outside of its scope, the compiler
will throw an error. Scopes are useful for organizing your bindings and my extension your programs.
There are two main ways to create a scope in Dst.

The first is to use the `do` special form. `do` executes a series of statements in a scope
and evaluates to the last statement. Bindings create inside the form do not escape outside
of its scope.

```lisp
(def a :outera)

(do
 (def a 1)
 (def b 2)
 (def c 3)
 (+ a b c)) # -> 6

a # -> :outera
b # -> compile error: "unknown symbol \"b\""
c # -> compile error: "unknown symbol \"c\""
```

Any attempt to reference the bindings from the do form after it has finished
executing will fail. Also notice who defining `a` inside the do form did not
overwrite the original definition of `a` for the global scope.

The second way to create a scope is to create a closure.
The `fn` special form also introduces a scope just like
the `do` special form.

There is another built in macro, `let`, that does multiple defs at once, and then introduces a scope.
`let` is a wrapper around a combination of defs and dos, and is the most "functional" way of
creating bindings.

```lisp
(let [a 1
      b 2
      c 3]
      (+ a b c)) # -> 6
```

The above is equivalent to the example using `do` and `def`.
This is the preferable form in most cases,
but using do with multiple defs is fine as well.

# Data Structures

Once you have a handle on functions and the primitive value types, you may be wondering how
to work with collections of things. Dst has a small number of core data structure types
that are very versatile. Tables, Structs, Arrays, Tuples, Strings, and Buffers, are the 6 main
built in data structure types. These data structures can be arranged in a useful table describing
there relationship to each other.

|            | Mutable | Immutable       |
| ---------- | ------- | --------------- |
| Indexed    | Array   | Tuple           |
| Dictionary | Table   | Struct          |
| Byteseq    | Buffer  | String (Symbol) |

Indexed types are linear lists of elements than can be accessed in constant time with an integer index.
Indexed types are backed by a single chunk of memory for fast access, and are indexed from 0 as in C.
Dictionary types associate keys with values. The difference between dictionaries and indexed types
is that dictionaries are not limited to integer keys. They are backed by a hashtable and also offer
constant time lookup (and insertion for the mutable case).
Finally, the 'byteseq' abstraction is any type that contains a sequence of bytes. A byteseq associates
integer keys (the indices) with integer values between 0 and 255 (the byte values). In this way,
they behave much like Arrays and Tuples. However, one cannot put non integer values into a byteseq.

```lisp
(def mytuple (tuple 1 2 3))

(def myarray @(1 2 3))
(def myarray (array 1 2 3))

(def mystruct {
 :key "value"
 :key2 "another"
 1 2
 4 3})

(def another-struct
 (struct :a 1 :b 2))

(def my-table @{
 :a :b
 :c :d
 :A :qwerty})
(def another-table
 (table 1 2 3 4))

(def my-buffer @"thisismutable")
(def my-buffer2 @\====\
 This is also mutable ":)"
 \====\)
```

To read the values in a data structure, use the get function. The first parameter is the data structure
itself, and the second parameter is the key.

```lisp
(get @{:a 1} :a) # -> 1
(get {:a 1} :a) # -> 1
(get @[:a :b :c] 2) # -> :c
(get (tuple "a" "b" "c") 1) # -> "a"
(get @"hello, world" 1) # -> 101
(get "hello, world" 0) # -> 104
```
To update a mutable data structure, use the `put` function. It takes 3 arguments, the data structure,
the key, and the value, and returns the data structure. The allowed types keys and values
depend on what data structure is passed in.

```lisp
(put @[] 100 :a)
(put @{} :key "value")
(put @"" 100 92)
```

Note that for Arrays and Buffers, putting an index that is outside the length of the data structure
will extend the data structure and fill it with nils in the case of the Array, 
or 0s in the case of the Buffer.

The last generic function for all data structures is the `length` function. This returns the number of
values in a data structure (the number of keys in a dictionary type).

# Flow Control

:)

# Combinators

:)

# Modules

:)

# The Core Library

Dst has a built in core library of over 200 functions and macros at the time of writing.
While some of these functions may be refactored into separate modules, it is useful to get to know
the core to avoid rewriting provided functions.

For any given function, use the `doc` macro to view the documentation for it in the repl.

```lisp
(doc defn) -> Prints the documentation for "defn"
```
To see a list of all global functions in the repl, type the command

```lisp
(getproto *env*)
```
Which will print out every built-in global binding
(it will not show your global bindings). To print all
of your global bindings, just use *env*, which is a var
that is bound to the current environment.

# Prototypes

:)

# Fibers

:)

# Macros

:)

# IO

:)

# The Parser Library

:)

# The Assembler

:)

# Interfacing with C

:)
