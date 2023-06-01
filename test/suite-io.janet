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
(start-suite)

# Printing to buffers
# d47804d22
(def out-buf @"")
(def err-buf @"")
(with-dyns [:out out-buf :err err-buf]
  (print "Hello")
  (prin "hi")
  (eprint "Sup")
  (eprin "not much."))

(assert (= (string out-buf) "Hello\nhi") "print and prin to buffer 1")
(assert (= (string err-buf) "Sup\nnot much.")
        "eprint and eprin to buffer 1")

# Printing to functions
# 4e263b8c3
(def out-buf @"")
(defn prepend [x]
  (with-dyns [:out out-buf]
    (prin "> " x)))
(with-dyns [:out prepend]
  (print "Hello world"))

(assert (= (string out-buf) "> Hello world\n")
        "print to buffer via function")

# c2f844157, 3c523d66e
(with [f (file/temp)]
  (assert (= 0 (file/tell f)) "start of file")
  (file/write f "foo\n")
  (assert (= 4 (file/tell f)) "after written string")
  (file/flush f)
  (file/seek f :set 0)
  (assert (= 0 (file/tell f)) "start of file again")
  (assert (= (string (file/read f :all)) "foo\n") "temp files work"))

# issue #1055 - 2c927ea76
(let [b @""]
  (defn dummy [a b c]
    (+ a b c))
  (trace dummy)
  (defn errout [arg]
    (buffer/push b arg))
  (assert (= 6 (with-dyns [*err* errout] (dummy 1 2 3)))
          "trace to custom err function")
  (assert (deep= @"trace (dummy 1 2 3)\n" b) "trace buffer correct"))

(end-suite)

