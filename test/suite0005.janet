# Copyright (c) 2023 Calvin Rose & contributors
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
(start-suite 5)

# Array remove

(assert (deep= (array/remove @[1 2 3 4 5] 2) @[1 2 4 5]) "array/remove 1")
(assert (deep= (array/remove @[1 2 3 4 5] 2 2) @[1 2 5]) "array/remove 2")
(assert (deep= (array/remove @[1 2 3 4 5] 2 200) @[1 2]) "array/remove 3")
(assert (deep= (array/remove @[1 2 3 4 5] -3 200) @[1 2 3]) "array/remove 4")

# Break

(var summation 0)
(for i 0 10
  (+= summation i)
  (if (= i 7) (break)))
(assert (= summation 28) "break 1")

(assert (= nil ((fn [] (break) 4))) "break 2")

# Break with value

# Shouldn't error out
(assert-no-error "break 3" (for i 0 10 (if (> i 8) (break i))))
(assert-no-error "break 4" ((fn [i] (if (> i 8) (break i))) 100))

# take

(assert (deep= (take 0 []) []) "take 1")
(assert (deep= (take 10 []) []) "take 2")
(assert (deep= (take 0 [1 2 3 4 5]) []) "take 3")
(assert (deep= (take 10 [1 2 3]) [1 2 3]) "take 4")
(assert (deep= (take -1 [:a :b :c]) []) "take 5")
(assert (deep= (take 3 (generate [x :in [1 2 3 4 5]] x)) @[1 2 3]) "take from fiber")
# NB: repeatedly resuming a fiber created with `generate` includes a `nil` as
# the final element. Thus a generate of 2 elements will create an array of 3.
(assert (= (length (take 4 (generate [x :in [1 2]] x))) 2) "take from short fiber")

# take-until

(assert (deep= (take-until pos? @[]) []) "take-until 1")
(assert (deep= (take-until pos? @[1 2 3]) []) "take-until 2")
(assert (deep= (take-until pos? @[-1 -2 -3]) [-1 -2 -3]) "take-until 3")
(assert (deep= (take-until pos? @[-1 -2 3]) [-1 -2]) "take-until 4")
(assert (deep= (take-until pos? @[-1 1 -2]) [-1]) "take-until 5")
(assert (deep= (take-until |(= $ 115) "books") "book") "take-until 6")
(assert (deep= (take-until |(= $ 115) (generate [x :in "books"] x))
               @[98 111 111 107]) "take-until from fiber")

# take-while

(assert (deep= (take-while neg? @[]) []) "take-while 1")
(assert (deep= (take-while neg? @[1 2 3]) []) "take-while 2")
(assert (deep= (take-while neg? @[-1 -2 -3]) [-1 -2 -3]) "take-while 3")
(assert (deep= (take-while neg? @[-1 -2 3]) [-1 -2]) "take-while 4")
(assert (deep= (take-while neg? @[-1 1 -2]) [-1]) "take-while 5")
(assert (deep= (take-while neg? (generate [x :in  @[-1 1 -2]] x))
               @[-1]) "take-while from fiber")

# drop

(assert (deep= (drop 0 []) []) "drop 1")
(assert (deep= (drop 10 []) []) "drop 2")
(assert (deep= (drop 0 [1 2 3 4 5]) [1 2 3 4 5]) "drop 3")
(assert (deep= (drop 10 [1 2 3]) []) "drop 4")
(assert (deep= (drop -2 [:a :b :c]) [:a :b :c]) "drop 5")
(assert-error :invalid-type (drop 3 {}) "drop 6")

# drop-until

(assert (deep= (drop-until pos? @[]) []) "drop-until 1")
(assert (deep= (drop-until pos? @[1 2 3]) [1 2 3]) "drop-until 2")
(assert (deep= (drop-until pos? @[-1 -2 -3]) []) "drop-until 3")
(assert (deep= (drop-until pos? @[-1 -2 3]) [3]) "drop-until 4")
(assert (deep= (drop-until pos? @[-1 1 -2]) [1 -2]) "drop-until 5")
(assert (deep= (drop-until |(= $ 115) "books") "s") "drop-until 6")

# Quasiquote bracketed tuples
(assert (= (tuple/type ~[1 2 3]) (tuple/type '[1 2 3])) "quasiquote bracket tuples")

(end-suite)
