# Copyright (c) 2022 Calvin Rose & contributors
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

# os/execute regressions
(for i 0 10
  (assert (= i (os/execute [(dyn :executable) "-e" (string/format "(os/exit %d)" i)] :p)) (string "os/execute " i)))

# to/thru bug
(def pattern
  (peg/compile
    '{:dd (sequence :d :d)
      :sep (set "/-")
      :date (sequence :dd :sep :dd)
      :wsep (some (set " \t"))
      :entry (group (sequence (capture :date) :wsep (capture :date)))
      :main (some (thru :entry))}))

(def alt-pattern
  (peg/compile
    '{:dd (sequence :d :d)
      :sep (set "/-")
      :date (sequence :dd :sep :dd)
      :wsep (some (set " \t"))
      :entry (group (sequence (capture :date) :wsep (capture :date)))
      :main (some (choice :entry 1))}))

(def text "1800-10-818-9-818 16/12\n17/12 19/12\n20/12 11/01")
(assert (deep= (peg/match pattern text) (peg/match alt-pattern text)) "to/thru bug #971")

(assert-error
  "table rawget regression"
  (table/new -1))

# Named arguments
(defn named-arguments
  [&named bob sally joe]
  (+ bob sally joe))

(assert (= 15 (named-arguments :bob 3 :sally 5 :joe 7)) "named arguments 1")

(defn named-opt-arguments
  [&opt x &named a b c]
  (+ x a b c))

(assert (= 10 (named-opt-arguments 1 :a 2 :b 3 :c 4)) "named arguments 2")

(let [b @""]
  (defn dummy [a b c]
    (+ a b c))
  (trace dummy)
  (defn errout [arg]
    (buffer/push b arg))
  (assert (= 6 (with-dyns [*err* errout] (dummy 1 2 3))) "trace to custom err function")
  (assert (deep= @"trace (dummy 1 2 3)\n" b) "trace buffer correct"))

(end-suite)

