# Special Forms

Janet is a lisp and so is defined in terms of mostly S-expressions, or
in terms of Janet, tuples. Tuples are used to represent function calls, macros,
and special forms. Most functionality is exposed through functions, some
through macros, and a minimal amount through special forms. Special forms
are neither functions nor macros -- they are used by the compiler to directly
express a low level construct that can not be expressed through macros or functions.
Special forms can be thought of as forming the real 'core' language of janet.

Below is a reference for all of the special forms in Janet.

## (def name meta... value)

This special form binds a value to a symbol. The symbol can the be substituted
for the value in subsequent expression for the same result. A binding made by def
is a constant and cannot be updated. A symbol can be redefined to a new value, but previous
uses of the binding will refer to the previous value of the binding.

```lisp
(def anumber (+ 1 2 3 4 5))

(print anumber) # prints 15
```

Def can also take a tuple, array, table or struct to perform destructuring
on the value. This allows us to do multiple assignments in one def.

```lisp
(def [a b c] (range 10))
(print a " " b " " c) # prints 0 1 2

(def {:x x} @{:x (+ 1 2)})
(print x) # prints 3

(def [y {:x x}] @[:hi @{:x (+ 1 2)}])
(print y x) # prints hi3
```

Def can also append metadata and a docstring to the symbol when in the global scope.
If not in the global scope, the extra metadata will be ignored.

```lisp
(def mydef :private 3) # Adds the :private key to the metadata table.
(def mydef2 :private "A docstring" 4) # Add a docstring

# The metadata will be ignored here because mydef is
# accessible outside of the do form.
(do
 (def mydef :private 3)
 (+ mydef 1))
```

## (var name meta... value)

Similar to def, but bindings set in this manner can be updated using set. In all other respects is the
same as def.

```lisp
(var a 1)
(defn printa [] (print a))

(printa) # prints 1
(++ a)
(printa) # prints 2
(set a :hi)
(printa) # prints hi
```

## (fn name? args body...)

Compile a function literal (closure). A function literal consists of an optional name, an
argument list, and a function body. The optional name is allowed so that functions can
more easily be recursive. The argument list is a tuple of named parameters, and the body
is 0 or more forms. The function will evaluate to the last form in the body. The other forms
will only be evaluated for side effects.

Functions also introduced a new lexical scope, meaning the defs and vars inside a function
body will not escape outside the body.

```lisp
(fn []) # The simplest function literal. Takes no arguments and returns nil.
(fn [x] x) # The identity function
(fn identity [x] x) # The identity function - the name will also make stacktraces nicer.
(fn [] 1 2 3 4 5) # A function that returns 5
(fn [x y] (+ x y)) # A function that adds its two arguments.

(fn [& args] (length args)) # A variadic function that counts its arguments.

# A function that doesn't strictly check the number of arguments.
# Extra arguments are ignored, and arguments not passed are nil.
(fn [w x y z &] (tuple w w x x y y z z))
```

## (do body...)

Execute a series of forms for side effects and evaluates to the final form. Also
introduces a new lexical scope without creating or calling a function.

```lisp
(do 1 2 3 4) # Evaluates to 4

# Prints 1, 2 and 3, then evaluates to (print 3), which is nil
(do (print 1) (print 2) (print 3))

# Prints 1
(do
 (def a 1)
 (print a))

# a is not defined here, so fails
a
```

## (quote x)

Evaluates to the literal value of the first argument. The argument is not compiled
and is simply used as a constant value in the compiled code. Preceding a form with a
single quote is shorthand for `(quote expression)`.

```lisp
(quote 1) # evaluates to 1
(quote hi) # evaluates to the symbol hi
(quote quote) # evaluates to the symbol quote

`(1 2 3) # Evaluates to a tuple (1 2 3)
`(print 1 2 3) # Evaluates to a tuple (print 1 2 3)
```

## (if condition when-true when-false?)

Introduce a branching construct. The first form is the condition, the second
form is the form to evaluate when the condition is true, and the optional
third form is the form to evaluate when the condition is false. If no third
form is provided it defaults to nil.

The if special form will not evaluate the when-true or when-false forms unless
it needs to - it is a lazy form, which is why it cannot be a function or macro.

The condition is considered false only if it evaluates to nil or false - all other values
are considered true.

```lisp
(if true 10) # evaluates to 10
(if false 10) # evaluates to nil
(if true (print 1) (print 2)) # prints 1 but not 2
```

## (splice x)

The splice special form is an interesting form that doesn't have an analog in most lisps.
It only has an effect in two places - as an argument in a function call, or as the argument
to the unquote form. Outside of these two settings, the splice special form simply evaluates
directly to it's argument x. The shorthand for splice is prefixing a form with a semicolon.

In the context of a function call, splice will insert *the contents* of x in the parameter list.

```lisp
(+ 1 2 3) # evaluates to 6

(+ @[1 2 3]) # bad

(+ (splice @[1 2 3])) # also evaluates to 6

(+ ;@[1 2 3]) # Same as above

(+ ;(range 100)) # Sum the first 100 natural numbers

(+ ;(range 100) 1000) # Sum the first 100 natural numbers and 1000
```

Notice that this means we rarely will need the `apply` function, as the splice operator is more flexible.

The splice operator can also be used inside an unquote form, where it will behave like
an `unquote-splicing` special in other lisps.

## (while condition body...)

The while special form compiles to a C-like while loop. The body of the form will be continuously evaluated
until the condition is false or nil. Therefor, it is expected that the body will contain some side effects
of the loop will go on for ever. The while loop always evaluates to nil.

```lisp
(var i 0)
(while (< i 10)
 (print i)
 (++ i))
```

## (set l-value r-value)

Update the value of a var l-value to a new value r-value. The set special form will then evaluate to r-value.

The r-value can be any expression, and the l-value should be a bound var.

## (quasiquote x)

Similar to `(quote x)`, but allows for unquoting within x. This makes quasiquote useful for
writing macros, as a macro definition often generates a lot of templated code with a
few custom values. The shorthand for quasiquote is a leading tilda `~` before a form. With
that form, `(unquote x)` will evaluate and insert x into the unquote form. The shorthand for
`(unquote x)` is `,x`.

## (unquote x)

Unquote a form within a quasiquote. Outside of a quasiquote, unquote is invalid.
