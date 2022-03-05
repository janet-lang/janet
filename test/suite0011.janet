# Copyright (c) 2021 Calvin Rose & contributors
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
(start-suite 11)

# math gamma

(assert (< 11899423.08 (math/gamma 11.5) 11899423.085) "math/gamma")
(assert (< 2605.1158 (math/log-gamma 500) 2605.1159) "math/log-gamma")

# missing symbols

(defn lookup-symbol [sym] (defglobal sym 10) (dyn sym))

(setdyn :missing-symbol lookup-symbol)

(assert (= (eval-string "(+ a 5)") 15) "lookup missing symbol")

(setdyn :missing-symbol nil)
(setdyn 'a nil)

(assert-error "compile error" (eval-string "(+ a 5)"))

# 919
(defn test
  []
  (var x 1)
  (set x ~(,x ()))
  x)

(assert (= (test) '(1 ())) "issue #919")

(assert (= (hash 0) (hash (* -1 0))) "hash -0 same as hash 0")

(end-suite)

