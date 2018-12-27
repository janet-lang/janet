# Hello, world!

Following tradition, a simple Janet program will print "Hello, world!".

```
(print "Hello, world!")
```

Put the following code in a file named `hello.janet`, and run `./janet hello.janet`.
The words "Hello, world!" should be printed to the console, and then the program
should immediately exit. You now have a working janet program!

Alternatively, run the program `./janet` without any arguments to enter a REPL,
or read eval print loop. This is a mode where Janet functions like a calculator,
reading some input from the user, evaluating it, and printing out the result, all
in an infinite loop. This is a useful mode for exploring or prototyping in Janet.

This hello world program is about the simplest program one can write, and consists of only
a few pieces of syntax. This first element is the `print` symbol. This is a function
that simply prints its arguments to the console. The second argument is the
string literal "Hello, world!", which is the one and only argument to the
print function. Lastly, the print symbol and the string literal are wrapped
in parentheses, forming a tuple. In Janet, parentheses and brackets are interchangeable,
brackets are used mostly when the resulting tuple is not a function call. The tuple
above indicates that the function `print` is to be called with one argument, `"Hello, world"`.

Like all lisps, all operations in Janet are in prefix notation; the name of the
operator is the first value in the tuple, and the arguments passed to it are
in the rest of the tuple.

# A bit more - Arithmetic

Any programming language will have some way to do arithmetic. Janet is no exception,
and supports the basic arithmetic operators

```
# Prints 13
# (1 + (2*2) + (10/5) + 3 + 4 + (5 - 6))
(print (+ 1 (* 2 2) (/ 10 5) 3 4 (- 5 6)))
```

Just like the print function, all arithmetic operators are entered in
prefix notation. Janet also supports the remainder operator, or `%`, which returns
the remainder of division. For example, `(% 10 3)` is 1, and `(% 10.5 3)` is
1.5. The lines that begin with `#` are comments.

All janet numbers are IEEE 754 floating point numbers. They can be used to represent
both integers and real numbers to a finite precision.

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

Besides the 5 main arithmetic functions, janet also supports a number of math functions
taken from the C library `<math.h>`, as well as bitwise operators that behave like they
do in C or Java. Functions like `math/sin`, `math/cos`, `math/log`, and `math/exp` will
behave as expected to a C programmer. They all take either 1 or 2 numeric arguments and
return a real number (never an integer!) Bitwise functions are all prefixed with b.
Thet are `bnot`, `bor`, `bxor`, `band`, `blshift`, `brshift`, and `brushift`. Bitwise
functions only work on integers.

# Strings, Keywords and Symbols

Janet supports several varieties of types that can be used as labels for things in
your program. The most useful type for this purpose is the keyword type. A keyword
begins with a semicolon, and then contains 0 or more alphanumeric or a few other common
characters. For example, `:hello`, `:my-name`, `::`, and `:ABC123_-*&^%$` are all keywords.
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
# Evaluates to true
(= :hello :hello)

# Evaluates to false, everything in janet is case sensitive
(= :hello :HeLlO)

# Look up into a table - evaluates to 25
(get {
    :name "John"
    :age 25
    :occupation "plumber"
} :age)
```

Strings can be used similarly to keywords, but there primary usage is for defining either text
or arbitrary sequences of bytes. Strings (and symbols) in janet are what is sometimes known as
"8-bit clean"; they can hold any number of bytes, and are completely unaware of things like character
encodings. This is completely compatible with ASCII and UTF-8, two of the most common character
encodings. By being encoding agnostic, janet strings can be very simple, fast, and useful for
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

Janet is a functional language - that means that one of the basic building blocks of your
program will be defining functions (the other is using data structures). Because janet
is a Lisp, functions are values just like numbers or strings - they can be passed around and
created as needed.

Functions can be defined with the `defn` macro, like so:

```lisp
(defn triangle-area
 "Calculates the area of a triangle."
 [base height]
 (print "calculating area of a triangle...")
 (* base height 0.5))
```

A function defined with `defn` consists of a name, a number of optional flags for def, and
finally a function body. The example above is named triangle-area and takes two parameters named base and height. The body of the function will print a message and then evaluate to the area of the triangle.

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
as arguments to other functions as well.

```
(print triangle-area)
```

This prints the location in memory of the function triangle area.

Functions don't need to have names. The `fn` keyword can be used to introduce function
literals without binding them to a symbol.

```
# Evaluates to 40
((fn [x y] (+ x x y)) 10 20)
# Also evaluates to 40
((fn [x y &] (+ x x y)) 10 20)

# Will throw an error about the wrong arity
((fn [x] x) 1 2)
# Will not throw an error about the wrong arity
((fn [x &] x) 1 2)
```

The first expression creates an anonymous function that adds twice
the first argument to the second, and then calls that function with arguments 10 and 20.
This will return (10 + 10 + 20) = 40.

There is a common macro `defn` that can be used for creating functions and immediately binding
them to a name. `defn` works as expected at both the top level and inside another form. There is also
the corresponding

Note that putting an ampersand at the end of the argument list inhibits strict arity checking.
This means that such a function will accept fewer or more arguments than specified.

```lisp
(defn myfun [x y]
 (+ x x y))

# You can think of defn as a shorthand for def and fn together
(def myfun-same (fn [x y]
            (+ x x Y)))

(myfun 3 4) # -> 10
```

Janet has many macros provided for you (and you can write your own).
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
languages, use the `var` keyword. The assignment special form `set` can then be used to update
a var.

```
(var myvar 1)
(print myvar)
(set myvar 10)
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
There are two main ways to create a scope in Janet.

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
to work with collections of things. Janet has a small number of core data structure types
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
(def my-buffer2 @```
 This is also mutable ":)"
 ```)
```

To read the values in a data structure, use the get function. The first parameter is the data structure
itself, and the second parameter is the key.

```lisp
(get @{:a 1} :a) # -> 1
(get {:a 1} :a) # -> 1
(get @[:a :b :c] 2) # -> :c
(get (tuple "a" "b" "c") 1) # -> "b"
(get @"hello, world" 1) # -> 101
(get "hello, world" 0) # -> 104
```

### Destructuring

In many cases, however, you do not need the `get` function at all. Janet supports destructuring, which
means both the `def` and `var` special forms can extract values from inside structures themselves.

```lisp
# Before, we might do
(def my-array @[:mary :had :a :little :lamb])
(def lamb (get my-array 4))
(print lamb) # Prints :lamb

# Now, with destructuring,
(def [_ _ _ _ lamb] my-array)
(print lamb) # Again, prints :lamb

# Destructuring works with tables as well
(def person @{:name "Bob Dylan" :age 77}
(def
  {:name person-name
   :age person-age} person)
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

Janet has only two built in primitives to change flow while inside a function. The first is the
`if` special form, which behaves as expected in most functional languages. It takes two or three parameters:
a condition, an expression to evaluate to if the condition is true (not nil or false),
and an optional condition to evaluate to when the condition is nil or false. If the optional parameter
is omitted, the if form evaluates to nil.

```lisp
(if (> 4 3)
  "4 is greater than 3"
  "4 is not greater then three") # Evaluates to the first statement

(if true
  (print "Hey")) # Will print

(if false
  (print "Oy!")) # Will not print
```

The second primitive control flow construct is the while loop. The while behaves much the same
as in many other programming languages, including C, Java, and Python. The while loop takes
two or more parameters: the first is a condition (like in the `if` statement), that is checked before
every iteration of the loop. If it is nil or false, the while loop ends and evaluates to nil. Otherwise,
the rest of the parameters will be evaluated sequentially and then the program will return to the beginning
of the loop.

```
# Loop from 100 down to 1 and print each time
(var i 100)
(while (pos? i)
  (print "the number is " i)
  (-- i))

# Print ... until a random number in range [0, 1) is >= 0.9
# (math/random evaluates to a value between 0 and 1)
(while (> 0.9 (math/random))
  (print "..."))
```

Besides these special forms, Janet has many macros for both conditional testing and looping
that are much better for the majority of cases. For conditional testing, the `cond`, `switch`, and
`when` macros can be used to great effect. `cond` can be used for making an if-else chain, where using
just raw if forms would result in many parentheses. `case` For looping, the `loop`, `seq`, and `generate`
implement janet's form of list comprehension, as in Python or Clojure.

# The Core Library

Janet has a built in core library of over 300 functions and macros at the time of writing.
While some of these functions may be refactored into separate modules, it is useful to get to know
the core to avoid rewriting provided functions.

For any given function, use the `doc` macro to view the documentation for it in the repl.

```lisp
(doc defn) -> Prints the documentation for "defn"
```
To see a list of all global functions in the repl, type the command

```lisp
(table/getproto *env*)
# Or
(all-symbols)
```
Which will print out every built-in global binding
(it will not show your global bindings). To print all
of your global bindings, just use \*env\*, which is a var
that is bound to the current environment.

The convention of surrounding a symbol in stars is taken from lisp
and Clojure, and indicates a global dynamic variable rather than a normal
definition. To get the static environment at the time of compilation, use the
`_env` symbol.

# Prototypes

To support basic generic programming, Janet tables support a prototype
table. A prototype table contains default values for a table if certain keys
are not found in the original table. This allows many similar tables to share
contents without duplicating memory.

```lisp
# One of many Object Oriented schemes that can
# be implented in janet.
(def proto1 @{:type :custom1
              :behave (fn [self x] (print "behaving " x))})
(def proto2 @{:type :custom2
              :behave (fn [self x] (print "behaving 2 " x))})

(def thing1 (table/setproto @{} proto1))
(def thing2 (table/setproto @{} proto2))

(print thing1:type) # prints :custom1
(print thing2:type) # prints :custom2

(thing1:behave thing1 :a) # prints "behaving :a"
(thing2:behave thing2 :b) # prints "behaving 2 :b"
```

Looking up in a table with a prototype can be summed up with the following algorithm.

1. `(get my-table my-key)` is called.
2. my-table is checked for the key if my-key. If there is a value for the key, it is returned.
3. if there is a prototype table for my-table, set `my-table = my-table's prototype` and got to 2.
4. Return nil as the key was not found.

Janet will check up to about a 1000 prototypes recursively by default before giving up and returning nil. This
is to prevent an infinite loop. This value can be changed by adjusting the `JANET_RECURSION_GUARD` value
in janet.h.

Note that Janet prototypes are not as expressive as metatables in Lua and many other languages.
This is by design, as adding Lua or Python like capabilities would not be technically difficult.
Users should prefer plain data and functions that operate on them rather than mutable objects
with methods.

# Fibers

Janet has support for single-core asynchronous programming via coroutines, or fibers.
Fibers allow a process to stop and resume execution later, essentially enabling
multiple returns from a function. This allows many patterns such a schedules, generators,
iterators, live debugging, and robust error handling. Janet's error handling is actually built on
top of fibers (when an error is thrown, the parent fiber will handle the error).

A temporary return from a fiber is called a yield, and can be invoked with the `yield` function.
To resume a fiber that has been yielded, use the `resume` function. When resume is called on a fiber,
it will only return when that fiber either returns, yields, throws an error, or otherwise emits
a signal.

Different from traditional coroutines, Janet's fibers implement a signaling mechanism, which
is used to differentiate different kinds of returns. When a fiber yields or throws an error,
control is returned to the calling fiber. The parent fiber must then check what kind of state the
fiber is in to differentiate errors from return values from user defined signals.

To create a fiber, user the `fiber/new` function. The fiber constructor take one or two arguments.
the first, necessary argument is the function that the fiber will execute. This function must accept
an arity of zero. The next optional argument is a collection of flags checking what kinds of
signals to trap and return via `resume`. This is useful so
the programmer does not need to handle all different kinds of signals from a fiber. Any un-trapped signals
are simply propagated to the next fiber.

```lisp
(def f (fiber/new (fn []
                   (yield 1)
                   (yield 2)
                   (yield 3)
                   (yield 4)
                   5)))

# Get the status of the fiber (:alive, :dead, :debug, :new, :pending, or :user0-:user9)
(print (fiber/status f)) # -> :new

(print (resume f)) # -> prints 1
(print (resume f)) # -> prints 2
(print (resume f)) # -> prints 3
(print (resume f)) # -> prints 4
(print (fiber/status f)) # -> print :pending
(print (resume f)) # -> prints 5
(print (fiber/status f)) # -> print :dead
(print (resume f)) # -> throws an error because the fiber is dead
```

## Using Fibers to Capture Errors

Besides being used as coroutines, fibers can be used to implement error handling (exceptions).

```lisp
(defn my-function-that-errors [x]
 (print "start function with " x)
 (error "oops!")
 (print "never gets here"))

# Use the :e flag to only trap errors.
(def f (fiber/new my-function-that-errors :e))
(def result (resume f))
(if (= (fiber/status f) :error)
 (print "result contains the error")
 (print "result contains the good result"))
```

# Macros

Janet supports macros like most lisps. A macro is like a function, but transforms
the code itself rather than data. They let you extend the syntax of the language itself.

You have seen some macros already. The `let`, `loop`, and `defn` forms are macros. When the compiler
sees a macro, it evaluates the macro and then compiles the result. We say the macro has been
*expanded* after the compiler evaluates it. A simple version of the `defn` macro can
be thought of as transforming code of the form

```lisp
(defn1 myfun [x] body)
```
into
```lisp
(def myfun (fn myfun [x] body))
```

We could write such a macro like so:

```lisp
(defmacro defn1 [name args body]
 (tuple 'def name (tuple 'fn name args body)))
```

There are a couple of issues with this macro, but it will work for simple functions
quite well.

The first issue is that our defn2 macro can't define functions with multiple expressions
in the body. We can make the macro variadic, just like a function. Here is a second version
of this macro.

```lisp
(defmacro defn2 [name args & body]
 (tuple 'def name (apply tuple 'fn name args body)))
```

Great! Now we can define functions with multiple elements in the body. We can still improve this
macro even more though. First, we can add a docstring to it. If someone is using the function later,
they can use `(doc defn3)` to get a description of the function. Next, we can rewrite the macro
using janet's builtin quasiquoting facilities.

```lisp
(defmacro defn3
 "Defines a new function."
 [name args & body]
 `(def ,name (fn ,name ,args ,;body)))
```

This is functionally identical to our previous version `defn2`, but written in such
a way that the macro output is more clear. The leading backtick is shorthand for the
`(quasiquote x)` special form, which is like `(quote x)` except we can unquote
expressions inside it. The comma in front of `name` and `args` is an unquote, which
allows us to put a value in the quasiquote. Without the unquote, the symbol \'name\'
would be put in the returned tuple. Without the unquote, every function we defined
would be called \'name\'!.

Similar to name, we must also unquote body. However, a normal unquote doesn't work.
See what happens if we use a normal unquote for body as well.

```lisp
(def name 'myfunction)
(def args '[x y z])
(defn body '[(print x) (print y) (print z)])

`(def ,name (fn ,name ,args ,body))
# -> (def myfunction (fn myfunction (x y z) ((print x) (print y) (print z))))
```

There is an extra set of parentheses around the body of our function! We don't
want to put the body *inside* the form `(fn args ...)`, we want to *splice* it
into the form. Luckily, janet has the `(splice x)` special form for this purpose,
and a shorthand for it, the ; character.
When combined with the unquote special, we get the desired output.

```lisp
`(def ,name (fn ,name ,args ,;body))
# -> (def myfunction (fn myfunction (x y z) (print x) (print y) (print z)))
```

## Hygiene

Sometime when we write macros, we must generate symbols for local bindings. Ignoring that
it could be written as a function, consider
the following macro

```lisp
(defmacro max1
 "Get the max of two values."
 [x y]
 `(if (> ,x ,y) ,x ,y))
```

This almost works, but will evaluate both x and y twice. This is because both show up
in the macro twice. For example, `(max1 (do (print 1) 1) (do (print 2) 2))` will
print both 1 and 2 twice, which is surprising to a user of this macro.

We can do better:

```lisp
(defmacro max2
 "Get the max of two values."
 [x y]
 `(let [x ,x
        y ,y]
    (if (> x y) x y)))
```

Now we have no double evaluation problem! But we now have an even more subtle problem.
What happens in the following code?

```lisp
(def x 10)
(max2 8 (+ x 4))
```

We want the max to be 14, but this will actually evaluate to 12! This can be understood
if we expand the macro. You can expand macro once in janet using the `(macex1 x)` function.
(To expand macros until there are no macros left to expand, use `(macex x)`. Be careful,
 janet has many macros, so the full expansion may be almost unreadable).

```lisp
(macex1 '(max2 8 (+ x 4)))
# -> (let (x 8 y (+ x 4)) (if (> x y) x y))
```

After expansion, y wrongly refers to the x inside the macro (which is bound to 8) rather than the x defined
to be 10. The problem is the reuse of the symbol x inside the macro, which overshadowed the original
binding.

Janet provides a general solution to this problem in terms of the `(gensym)` function, which returns
a symbol which is guarenteed to be unique and not collide with any symbols defined previously. We can define
our macro once more for a fully correct macro.

```lisp
(defmacro max3
 "Get the max of two values."
 [x y]
 (def $x (gensym))
 (def $y (gensym))
 `(let [,$x ,x
        ,$y ,y]
    (if (> ,$x ,$y) ,$x ,$y)))
```

As you can see, macros are very powerful but also are prone to subtle bugs. You must remember that
at their core, macros are just functions that output code, and the code that they return must
work in many contexts!
