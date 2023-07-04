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

# Regression Test
# 0378ba78
(assert (= 1 (((compile '(fn [] 1) @{})))) "regression test")

# Fix a compiler bug in the do special form
# 3e1e2585
(defn myfun [x]
  (var a 10)
  (set a (do
         (def y x)
         (if x 8 9))))

(assert (= (myfun true) 8) "check do form regression")
(assert (= (myfun false) 9) "check do form regression")

# Check x:digits: works as symbol and not a hex number
# 5baf70f4
(def x1 100)
(assert (= x1 100) "x1 as symbol")
(def X1 100)
(assert (= X1 100) "X1 as symbol")

# Edge case should cause old compilers to fail due to
# if statement optimization
# 17283241
(var var-a 1)
(var var-b (if false 2 (string "hello")))

(assert (= var-b "hello") "regression 1")

# d28925fda
(assert (= (string '()) (string [])) "empty bracket tuple literal")

# Bracket tuple issue
# 340a6c4
(let [do 3]
  (assert (= [3 1 2 3] [do 1 2 3]) "bracket tuples are never special forms"))
(assert (= ~(,defn 1 2 3) [defn 1 2 3]) "bracket tuples are never macros")
(assert (= ~(,+ 1 2 3) [+ 1 2 3]) "bracket tuples are never function calls")

# Crash issue #1174 - bad debug info
# e97299f
(defn crash []
  (debug/stack (fiber/current)))
(do
  (math/random)
  (defn foo [_]
    (crash)
    1)
  (foo 0)
  10)

(end-suite)

