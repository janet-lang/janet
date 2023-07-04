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

# Tuple types
# c6edf03ae
(assert (= (tuple/type '(1 2 3)) :parens) "normal tuple")
(assert (= (tuple/type [1 2 3]) :parens) "normal tuple 1")
(assert (= (tuple/type '[1 2 3]) :brackets) "bracketed tuple 2")
(assert (= (tuple/type (-> '(1 2 3) marshal unmarshal)) :parens)
        "normal tuple marshalled/unmarshalled")
(assert (= (tuple/type (-> '[1 2 3] marshal unmarshal)) :brackets)
        "normal tuple marshalled/unmarshalled")

# Dynamic bindings
# 7918add47, 513d551d
(setdyn :a 10)
(assert (= 40 (with-dyns [:a 25 :b 15] (+ (dyn :a) (dyn :b)))) "dyn usage 1")
(assert (= 10 (dyn :a)) "dyn usage 2")
(assert (= nil (dyn :b)) "dyn usage 3")
(setdyn :a 100)
(assert (= 100 (dyn :a)) "dyn usage 4")

(end-suite)

