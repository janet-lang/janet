# Copyright (c) 2023 Calvin Rose
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

(import ./helper :prefix "" :exit true)
(start-suite)

# Array tests
# e05022f
(defn array=
  "Check if two arrays are equal in an element by element comparison"
  [a b]
  (if (and (array? a) (array? b))
    (= (apply tuple a) (apply tuple b))))
(assert (= (apply tuple @[1 2 3 4 5]) (tuple 1 2 3 4 5)) "array to tuple")
(def arr (array))
(array/push arr :hello)
(array/push arr :world)
(assert (array= arr @[:hello :world]) "array comparison")
(assert (array= @[1 2 3 4 5] @[1 2 3 4 5]) "array comparison 2")
(assert (array= @[:one :two :three :four :five]
                @[:one :two :three :four :five]) "array comparison 3")
(assert (array= (array/slice @[1 2 3] 0 2) @[1 2]) "array/slice 1")
(assert (array= (array/slice @[0 7 3 9 1 4] 2 -2) @[3 9 1]) "array/slice 2")

# Array remove
# 687a3c9
(assert (deep= (array/remove @[1 2 3 4 5] 2) @[1 2 4 5]) "array/remove 1")
(assert (deep= (array/remove @[1 2 3 4 5] 2 2) @[1 2 5]) "array/remove 2")
(assert (deep= (array/remove @[1 2 3 4 5] 2 200) @[1 2]) "array/remove 3")
(assert (deep= (array/remove @[1 2 3 4 5] -3 200) @[1 2 3]) "array/remove 4")


# array/peek
(assert (nil? (array/peek @[])) "array/peek empty")

# array/fill
(assert (deep= (array/fill @[1 1] 2) @[2 2]) "array/fill 1")

# array/concat
(assert (deep= (array/concat @[1 2] @[3 4] 5 6) @[1 2 3 4 5 6]) "array/concat 1")
(def a @[1 2])
(assert (deep= (array/concat a a) @[1 2 1 2]) "array/concat self")

# array/insert
(assert (deep= (array/insert @[:a :a :a :a] 2 :b :b) @[:a :a :b :b :a :a]) "array/insert 1")
(assert (deep= (array/insert @[:a :b] -1 :c :d) @[:a :b :c :d]) "array/insert 2")

# array/remove
(assert-error "removal index 3 out of range [0,2]" (array/remove @[1 2] 3))
(assert-error "expected non-negative integer for argument n, got -1" (array/remove @[1 2] 1 -1))

# array/pop
(assert (= (array/pop @[1]) 1) "array/pop 1")
(assert (= (array/pop @[]) nil) "array/pop empty")

# Code coverage
(def a @[1])
(array/pop a)
(array/trim a)
(array/ensure @[1 1] 6 2)


(end-suite)

