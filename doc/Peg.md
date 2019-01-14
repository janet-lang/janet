# Peg (Parsing Expression Grammars)

A common task for developers is to recognize patterns in text, be it
filtering emails from a list or extracting data from a CSV file. Programming
languages and libraries usually offer a number of tools for this, including prebuilt
parsers, simple operations on strings (splitting a string on commas), and regular expressions.
The pre-built or custom-built parser is usually the most robust solution, but can
be very complex to maintain and may not exist for many languages. String functions are not 
powerful enough for a large class of languages, and regular expressions can be hard to read
(which characters are escaped?) and underpowered (don't parse HTML with regex!).

PEGs, or Parsing Expression Grammars, are another formalism for recognizing languages that
are easier to write as a custom parser and more powerful than regular expressions. They also
can produce grammars that are easily unerstandable and moderatly fast. PEGs can also be compiled
to a bytecode format that can be reused.

Below is a siimple example for checking if a string is a valid IP address. Notice how
the grammar is descriptive enough that you can read it even if you don't know the peg
syntax (example is translated from a (RED language blog post)[https://www.red-lang.org/2013/11/041-introducing-parse.html]).
```
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

## Primitive Patterns

Larger patterns are built up with primitive patterns, which recognize individual
characters, string literals, or a given number of characters. A character in Janet
is considered a byte, so PEGs will work on any string of bytes. No special meaning is
given to the 0 byte, or the string terminator in many languages.

| Pattern          | Alias | What it Matches |
| string ("cat")   |       | The literal string. |
| integer (3)      |       | Matches a number of characters, and advances that many characters. If negative, matches if not that many characters and does not advance. For example, -1 will match the end of a string |
| `(range "az" "AZ")` | | Matches characters in a range and advances 1 character. Multiple ranges can be combined together. |
| `(set "abcd")`   |       | Match any character in the argument string. Advances 1 character. |

## Combining Patterns

These primitve patterns are combined with a few specials to match a wide number of languages.


## Grammars and Recursion

Parsing Expression Grammars try to match an input text with a pattern in a greedy manner.
This means that if a rule fails to match, that rule will fail and not try again. The only
backtracking provided in a peg is provided by the `(choice x y z ...)` special, which will
try rules in order until one succeeds, and the whole pattern succeeds. If no sub pattern
succeeds, then the whole pattern fails. Note that this means that the order of `x y z` in choice
DOES matter. If y matches everything that z matches, z will never succeed.
