# Copyright (c) 2020 Calvin Rose & contributors
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
(start-suite 10)

# index-of
(assert (= nil (index-of 10 [])) "index-of 1")
(assert (= nil (index-of 10 [1 2 3])) "index-of 2")
(assert (= 1 (index-of 2 [1 2 3])) "index-of 3")
(assert (= 0 (index-of :a [:a :b :c])) "index-of 4")
(assert (= nil (index-of :a {})) "index-of 5")
(assert (= :a (index-of :A {:a :A :b :B})) "index-of 6")
(assert (= :a (index-of :A @{:a :A :b :B})) "index-of 7")
(assert (= 0 (index-of (chr "a") "abc")) "index-of 8")
(assert (= nil (index-of (chr "a") "")) "index-of 9")
(assert (= nil (index-of 10 @[])) "index-of 10")
(assert (= nil (index-of 10 @[1 2 3])) "index-of 11")

# Regression
(assert (= {:x 10} (|(let [x $] ~{:x ,x}) 10)) "issue 463")

# macex testing
(assert (deep= (macex1 '~{1 2 3 4}) '~{1 2 3 4}) "macex1 qq struct")
(assert (deep= (macex1 '~@{1 2 3 4}) '~@{1 2 3 4}) "macex1 qq table")
(assert (deep= (macex1 '~(1 2 3 4)) '~[1 2 3 4]) "macex1 qq tuple")
(assert (= :brackets (tuple/type (1 (macex1 '~[1 2 3 4])))) "macex1 qq bracket tuple")
(assert (deep= (macex1 '~@[1 2 3 4 ,blah]) '~@[1 2 3 4 ,blah]) "macex1 qq array")

# Cancel test
(def f (fiber/new (fn [&] (yield 1) (yield 2) (yield 3) 4) :yti))
(assert (= 1 (resume f)) "cancel resume 1")
(assert (= 2 (resume f)) "cancel resume 2")
(assert (= :hi (cancel f :hi)) "cancel resume 3")
(assert (= :error (fiber/status f)) "cancel resume 4")

# Curenv
(assert (= (curenv) (curenv 0)) "curenv 1")
(assert (= (table/getproto (curenv)) (curenv 1)) "curenv 2")
(assert (= nil (curenv 1000000)) "curenv 3")
(assert (= root-env (curenv 1)) "curenv 4")

# Import macro test
(assert-no-error "import macro 1" (macex '(import a :as b :fresh maybe)))
(assert (deep= ~(,import* "a" :as "b" :fresh maybe) (macex '(import a :as b :fresh maybe))) "import macro 2")

# #477 walk preserving bracket type
(assert (= :brackets (tuple/type (postwalk identity '[]))) "walk square brackets 1")
(assert (= :brackets (tuple/type (walk identity '[]))) "walk square brackets 2")

(end-suite)
