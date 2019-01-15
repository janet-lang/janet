# Peg (Parsing Expression Grammars)

A common programming task is recognizing patterns in text, be it
filtering emails from a list or extracting data from a CSV file. Programming
languages and libraries usually offer a number of tools for this, including prebuilt
parsers, simple operations on strings (splitting a string on commas), and regular expressions.
The pre-built or custom-built parser is usually the most robust solution, but can
be very complex to maintain and may not exist for many languages. String functions are not 
powerful enough for a large class of languages, and regular expressions can be hard to read
(which characters are escaped?) and under-powered (don't parse HTML with regex!).

PEGs, or Parsing Expression Grammars, are another formalism for recognizing languages that
are easier to write than a custom parser and more powerful than regular expressions. They also
can produce grammars that are easily understandable and fast. PEGs can also be compiled
to a bytecode format that can be reused. Janet offers the `peg` module for writing and
evaluating PEGs.

Janet's `peg` module borrows syntax and ideas from both LPeg and REBOL/Red parse module. Janet has
no built in regex module because PEGs offer a superset of regex's functionality.

Below is a simple example for checking if a string is a valid IP address. Notice how
the grammar is descriptive enough that you can read it even if you don't know the peg
syntax (example is translated from a [RED language blog post](https://www.red-lang.org/2013/11/041-introducing-parse.html).
```clojure
(def ip-address
 '{:dig (range "09")
   :0-4 (range "04")
   :0-5 (range "05")
   :byte (choice
           (sequence "25" :0-5)
           (sequence "2" :0-4 :dig)
           (sequence "1" :dig :dig)
           (between 1 2 :dig))
   :main (sequence :byte "." :byte "." :byte "." :byte)})

(peg/match ip-address "0.0.0.0") # -> @[]
(peg/match ip-address "elephant") # -> nil
(peg/match ip-address "256.0.0.0") # -> nil
```

## The API

The `peg` module has few functions because the complexity is exposed through the
pattern syntax. Note that there is only one match function, `peg/match`. Variations
on matching, such as parsing or searching, can be implemented inside patterns.
PEGs can also be compiled ahead of time with `peg/compile` if a PEG will be reused
many times.

### `(peg/match peg text [,start=0] & arguments)`

Match a peg against some text. Returns an array of captured data if the text
matches, or nil if there is no match. The caller can provide an optional start
index to begin matching the text at, otherwise the PEG starts on the first character
of text. A peg can either a compile PEG object or peg source.

### `(peg/compile peg)`

Compiles a peg source data structure into a new PEG. Throws an error if there are problems
with the peg code.

## Primitive Patterns

Larger patterns are built up with primitive patterns, which recognize individual
characters, string literals, or a given number of characters. A character in Janet
is considered a byte, so PEGs will work on any string of bytes. No special meaning is
given to the 0 byte, or the string terminator in many languages.

| Pattern&nbsp;Signature         | What it Matches |
| ----------------- | ----------------|
| string ("cat")   | The literal string. |
| integer (3)      | Matches a number of characters, and advances that many characters. If negative, matches if not that many characters and does not advance. For example, -1 will match the end of a string |
| `(range "az" "AZ")` | Matches characters in a range and advances 1 character. Multiple ranges can be combined together. |
| `(set "abcd")`  | Match any character in the argument string. Advances 1 character. |

Primitve patterns are not that useful by themselves, but can be passed to `peg/match` and `peg/compile` as any pattern.

```clojure
(peg/match "hello" "hello") # -> @[]
(peg/match "hello" "hi") # -> nil
(peg/match 1 "hi") # -> @[]
(peg/match 1 "") # -> nil
(peg/match '(range "AZ") "F") # -> @[]
(peg/match '(range "AZ") "-") # -> nil
(peg/match '(set "AZ") "F") # -> nil
(peg/match '(set "ABCDEFGHIJKLMNOPQRSTUVWXYZ") "F") # -> @[]
```

## Combining Patterns

These primitive patterns are combined with a few specials to match a wide number of languages. These specials
can be thought of as the looping and branching forms in a traditional language
(that is how they are implemented when compiled to bytecode).

PEGs try to match an input text with a pattern in a greedy manner.
This means that if a rule fails to match, that rule will fail and not try again. The only
backtracking provided in a peg is provided by the `(choice x y z ...)` special, which will
try rules in order until one succeeds, and the whole pattern succeeds. If no sub pattern
succeeds, then the whole pattern fails. Note that this means that the order of `x y z` in choice
DOES matter. If y matches everything that z matches, z will never succeed.


| Pattern&nbsp;Signature | What it matches |
| ------- | --------------- |
| `(choice a b c ...)` | Tries to match a, then b, and so on. Will succeed on the first successful match, and fails if none of the arguments match the text. |
| `(+ a b c ...)` | Alias for `(choice a b c ...)` |
| `(sequence a b c)` | Tries to match a, b, c and so on in sequence. If any of these arguments fail to match the text, the whole pattern fails. |
| `(* a b c ...)` | Alias for `(sequence a b c ...)` |
| `(any x)` | Matches 0 or more repetitions of x. |
| `(some x)` | Matches 1 or more repetitions of x. |
| `(between min max x)` | Matches between min and max (inclusive) repetitions of x. |
| `(at-least n x)` | Matches at least n repetitions of x. |
| `(at-most n x)` |  Matches at most n repetitions of x. |
| `(if cond patt)` | Tries to match patt only if cond matches as well. cond will not produce any captures. |
| `(if-not cond patt)` | Tries to match only if cond does not match. cond will not produce any captures. |
| `(not patt)` | Matches only if patt does not match. Will not produce captures or advance any characters. |
| `(! patt)`   | Alias for `(not patt)` |
| `(look offset patt)` | Matches only if patt matches at a fixed offset. offset can be any integer. patt will not produce captures and the peg will not advance any characters. |
| `(> offset patt)` | Alias for `(look offset patt)` |

## Captures

So far we have only been concerned with "does this text match this language?". This is useful, but
it is often more useful to extract data from text if it does match a peg. The `peg` module
uses that concept of a capture stack to extract data from text. As the PEG is trying to match
a piece of text, some forms may push Janet values onto the capture stack as a side effect. If the
text matches the main peg language, `(peg/match)` will return the final capture stack as an array.

Capture specials will only push captures to the capture stack if their child pattern matches the text.
Most captures specials will match the same text as their first argument pattern.

| Pattern&nbsp;Signature | What it captures |
| ------- | ---------------- |
| `(capture patt)` | Captures all of the text in patt if patt matches, If patt contains any captures, then those captures will be pushed to the capture stack before the total text. |
| `(<- patt)` | Alias for `(capture patt)` |
| `(group patt) ` | Pops all of the captures in patt off of the capture stack and pushes them in an array if patt matches.
| `(replace patt subst)` | Replaces the captures produced by patt by applying subst to them. If subst is a table or struct, will push `(get subst last-capture)` to the capture stack after removing the old captures. If a subst is a function, will call subst with the captures of patt as arguments and push the result to the capture stack. Otherwise, will push subst literally to the capture stack. |
| `(/ patt subst)` | Alias for `(replace patt subst)` |
| `(constant k)` | Captures a constant value and advances no characters. |
| `(argument n)` | Captures the nth extra argument to the match function and does not advance. |
| `(position)` | Captures the current index into the text and advances no input. |
| `($)` | Alias for `(position)`. |
| `(substitute patt)` | Replace the text matched by all captures in patt with the capture values. Pushes the substituted text matched by patt to the capture stack. |
| `(% patt)` | Alias for `(substitute patt)`
| `(cmt patt fun)` | Invokes fun with all of the captures of patt as arguments (if patt matches). If the result is truthy, then captures the result. The whole expression fails if fun returns false or nil. |
| `(backref n)` | Duplicates the nth last capture and pushes it to the stack again (0 is the previous capture). If n is negative, pushes the nth capture value to the stack (-1 pushes the first captured value to the stack). If n is out of range for the stack, say if the stack is empty, then the match fails. |

## Grammars and Recursion

The feature that makes PEGs so much more powerful than pattern matching solutions like (vanilla) regex is mutual recursion.
To do recursion in a peg, you can wrap multiple patterns in a grammar, which is a Janet struct. The patterns must be named by
keywords, which can then be used in all sub-patterns in the grammar.

Each grammar, defined by a struct, must also have a main rule, called :main, that is the pattern that the entire grammar
is defined by.

An example grammar that uses mutual recursion:

```clojure
(def my-grammar
 '{:a (* "a" :b "a")
   :b (* "b" (+ :a 0) "b")
   :main (* "(" :b ")")})

(peg/match my-grammar "(bb)") # -> @[]
(peg/match my-grammar "(babbab)") # -> @[]
(peg/match my-grammar "(baab)") # -> nil
(peg/match my-grammar "(babaabab)") # -> nil
```

Keep in mind that recursion is implemented with a stack, meaning that very recursive grammars
can overflow the stack. The compiler is able to turn some recursion into iteration via tail call optimization, but some patterns
may fail on large inputs. It is also possible to construct (very poorly written) patterns that will result in long loops and be very
slow in general.

## String Searching and other Idioms

Although all pattern matching is done in anchored in mode, operations like global substitution
and searching can be implemented with the `peg/module`. A simple Janet function that produces PEGs
that search for strings shows how captures and looping specials can composed, and how quasiquoting
can be used to embed values in patterns.

```clojure
(defn finder
 "Creates a peg that finds all locations of str in the text."
 [str]
 (peg/compile ~(any (+ (* ($) ,str) 1))))

(def where-are-the-dogs? (finder "dog"))

(peg/match where-are-the-dogs? "dog dog cat dog") # -> @[0 4 12]
```
