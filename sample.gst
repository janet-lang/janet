# GST is a general purpose language. It is small, not slow, and supports meta-
# programming. It also should be structured and static enough to easily scale to
# large programs. Lastly, it is interoperable with C and C++.

# Syntax - There is very little syntax. This simplifies parsing and makes macros
# easier to implement, which are useful in metaprogramming. 
(+ 1 2 3)

# Unlike most lisps, it is not a pure functional language. Also unlike lisp, gst does
# not make much use of a list data structure, instead using arrays and objects for
# better performance at runtime.

