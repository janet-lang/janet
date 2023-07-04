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

# Regression Test #137
# affcb5b45
(def [a b c] (range 10))
(assert (= a 0) "regression #137 (1)")
(assert (= b 1) "regression #137 (2)")
(assert (= c 2) "regression #137 (3)")

(var [x y z] (range 10))
(assert (= x 0) "regression #137 (4)")
(assert (= y 1) "regression #137 (5)")
(assert (= z 2) "regression #137 (6)")

# Test destructuring
# 23dcfb986
(do
  (def test-tab @{:a 1 :b 2})
  (def {:a a :b b} test-tab)
  (assert (= a 1) "dictionary destructuring 1")
  (assert (= b 2) "dictionary destructuring 2"))
(do
  (def test-tab @{'a 1 'b 2 3 4})
  (def {'a a 'b b (+ 1 2) c} test-tab)
  (assert (= a 1) "dictionary destructuring 3")
  (assert (= b 2) "dictionary destructuring 4")
  (assert (= c 4) "dictionary destructuring 5 - expression as key"))

# cb5af974a
(let [test-tuple [:a :b 1 2]]
  (def [a b one two] test-tuple)
  (assert (= a :a) "tuple destructuring 1")
  (assert (= b :b) "tuple destructuring 2")
  (assert (= two 2) "tuple destructuring 3"))
(let [test-tuple [:a :b 1 2]]
  (def [a & rest] test-tuple)
  (assert (= a :a) "tuple destructuring 4 - rest")
  (assert (= rest [:b 1 2]) "tuple destructuring 5 - rest"))
(do
  (def [a b & rest] [:a :b nil :d])
  (assert (= a :a) "tuple destructuring 6 - rest")
  (assert (= b :b) "tuple destructuring 7 - rest")
  (assert (= rest [nil :d]) "tuple destructuring 8 - rest"))

# 71cffc973
(do
  (def [[a b] x & rest] [[1 2] :a :c :b :a])
  (assert (= a 1) "tuple destructuring 9 - rest")
  (assert (= b 2) "tuple destructuring 10 - rest")
  (assert (= x :a) "tuple destructuring 11 - rest")
  (assert (= rest [:c :b :a]) "tuple destructuring 12 - rest"))

# 651e12cfe
(do
  (def [a b & rest] [:a :b])
  (assert (= a :a) "tuple destructuring 13 - rest")
  (assert (= b :b) "tuple destructuring 14 - rest")
  (assert (= rest []) "tuple destructuring 15 - rest"))

(do
  (def [[a b & r1] c & r2] [[:a :b 1 2] :c 3 4])
  (assert (= a :a) "tuple destructuring 16 - rest")
  (assert (= b :b) "tuple destructuring 17 - rest")
  (assert (= c :c) "tuple destructuring 18 - rest")
  (assert (= r1 [1 2]) "tuple destructuring 19 - rest")
  (assert (= r2 [3 4]) "tuple destructuring 20 - rest"))

# Metadata
# ec2d7bf34
(def foo-with-tags :a-tag :bar)
(assert (get (dyn 'foo-with-tags) :a-tag)
        "extra keywords in def are metadata tags")

(def foo-with-meta {:baz :quux} :bar)
(assert (= :quux (get (dyn 'foo-with-meta) :baz))
        "extra struct in def is metadata")

(defn foo-fn-with-meta {:baz :quux}
  "This is a function"
  [x]
  (identity x))
(assert (= :quux (get (dyn 'foo-fn-with-meta) :baz))
        "extra struct in defn is metadata")
(assert (= "(foo-fn-with-meta x)\n\nThis is a function"
           (get (dyn 'foo-fn-with-meta) :doc))
        "extra string in defn is docstring")

# Break
# 4a111b38b
(var summation 0)
(for i 0 10
  (+= summation i)
  (if (= i 7) (break)))
(assert (= summation 28) "break 1")

(assert (= nil ((fn [] (break) 4))) "break 2")

# Break with value
# 8ba112116
# Shouldn't error out
(assert-no-error "break 3" (for i 0 10 (if (> i 8) (break i))))
(assert-no-error "break 4" ((fn [i] (if (> i 8) (break i))) 100))

# No useless splices
# 7d57f8700
(check-compile-error '((splice [1 2 3]) 0))
(check-compile-error '(if ;[1 2] 5))
(check-compile-error '(while ;[1 2 3] (print :hi)))
(check-compile-error '(def x ;[1 2 3]))
(check-compile-error '(fn [x] ;[x 1 2 3]))

# No splice propagation
(check-compile-error '(+ 1 (do ;[2 3 4]) 5))
(check-compile-error '(+ 1 (upscope ;[2 3 4]) 5))
# compiler inlines when condition is constant, ensure that optimization
# doesn't break
(check-compile-error '(+ 1 (if true ;[3 4])))
(check-compile-error '(+ 1 (if false nil ;[3 4])))

# Keyword arguments
# 3f137ed0b
(defn myfn [x y z &keys {:a a :b b :c c}]
  (+ x y z a b c))

(assert (= (+ ;(range 6)) (myfn 0 1 2 :a 3 :b 4 :c 5)) "keyword args 1")
(assert (= (+ ;(range 6)) (myfn 0 1 2 :a 1 :b 6 :c 5 :d 11))
        "keyword args 2")

# Named arguments
# 87fc339
(defn named-arguments
  [&named bob sally joe]
  (+ bob sally joe))

(assert (= 15 (named-arguments :bob 3 :sally 5 :joe 7)) "named arguments 1")

# a117252
(defn named-opt-arguments
  [&opt x &named a b c]
  (+ x a b c))

(assert (= 10 (named-opt-arguments 1 :a 2 :b 3 :c 4)) "named arguments 2")

#
# fn compilation special
#
# b8032ec61
(defn myfn1 [[x y z] & more]
  more)
(defn myfn2 [head & more]
  more)
(assert (= (myfn1 [1 2 3] 4 5 6) (myfn2 [:a :b :c] 4 5 6))
        "destructuring and varargs")

# Nested quasiquotation
# 4199c42fe
(def nested ~(a ~(b ,(+ 1 2) ,(foo ,(+ 1 3) d) e) f))
(assert (deep= nested '(a ~(b ,(+ 1 2) ,(foo 4 d) e) f))
        "nested quasiquote")

# Regression #400
# 7a84fc474
(assert (= nil (while (and false false)
                 (fn [])
                 (error "should not happen"))) "strangeloop 1")
(assert (= nil (while (not= nil nil)
                 (fn [])
                 (error "should not happen"))) "strangeloop 2")

# 919
# a097537a0
(defn test
  []
  (var x 1)
  (set x ~(,x ()))
  x)

(assert (= (test) '(1 ())) "issue #919")

(end-suite)

