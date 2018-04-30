# Dst Language Introduction

Dst is a dynamic, lightweight programming language with strong functional
capabilities as well as support for imperative programming. It to be used
for short lived scripts as well as for building real programs. It can also
be extended with native code (C modules) for better performance and interfacing with
existing software. Dst takes ideas from Lua, Scheme, Clojure, Smalltalk, and 
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

Any programming language will have some way to do arithmetic. 

```
# Prints 13
# (1 + (2*2) + (10/5) + 3 + 4 + (5 - 6))
(print (+ 1 (* 2 2) (/ 10 5) 3 4 (- 5 6)))
```

Just like the print function, all arithmetic operators are entered in
prefix notation. Dst also supports the modulo operator, or `%`, which returns
the remainder of integer division. For example, `(% 10 3)` is 1, and `(% 10.5 3)` is
1.5. The lines that begin with `#` are comments.

Dst actually has to flavors of numbers; integers and real numbers. Integers are any
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

# Functions

Dst is a functional language - that means that one of the basic building blocks of your
program will be defining functions (the other is using data structures). Because dst
is a Lisp, functions are values just like numbers or strings - they can be passed around and
created as needed.

Functions can be defined with the `defn` macro, like so:

```
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

```
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

Prints the location in memory of the function triangle area. This idea can be used
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
