# Copyright (c) 2019 Calvin Rose
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
(start-suite 1)

(assert (= 400 (math/sqrt 160000)) "sqrt(160000)=400")

(def test-struct {'def 1 'bork 2 'sam 3 'a 'b 'het @[1 2 3 4 5]})
(assert (= (get test-struct 'def) 1) "struct get")
(assert (= (get test-struct 'bork) 2) "struct get")
(assert (= (get test-struct 'sam) 3) "struct get")
(assert (= (get test-struct 'a) 'b) "struct get")
(assert (= :array (type (get test-struct 'het))) "struct get")

(defn myfun [x]
  (var a 10)
  (set a (do
         (def y x)
         (if x 8 9))))

(assert (= (myfun true) 8) "check do form regression")
(assert (= (myfun false) 9) "check do form regression")

(defn assert-many [f n e]
 (var good true)
 (loop [i :range [0 n]]
  (if (not (f))
   (set good false)))
 (assert good e))

(assert-many (fn [] (>= 1 (math/random) 0)) 200 "(random) between 0 and 1")

## Table prototypes

(def roottab @{
 :parentprop 123
})

(def childtab @{
 :childprop 456
})

(table/setproto childtab roottab)

(assert (= 123 (get roottab :parentprop)) "table get 1")
(assert (= 123 (get childtab :parentprop)) "table get proto")
(assert (= nil (get roottab :childprop)) "table get 2")
(assert (= 456 (get childtab :childprop)) "proto no effect")

# Long strings

(assert (= "hello, world" `hello, world`) "simple long string")
(assert (= "hello, \"world\"" `hello, "world"`) "long string with embedded quotes")
(assert (= "hello, \\\\\\ \"world\"" `hello, \\\ "world"`)
        "long string with embedded quotes and backslashes")

# More fiber semantics

(var myvar 0)
(defn fiberstuff [&]
  (++ myvar)
  (def f (fiber/new (fn [&] (++ myvar) (debug) (++ myvar))))
  (resume f)
  (++ myvar))

(def myfiber (fiber/new fiberstuff :dey))

(assert (= myvar 0) "fiber creation does not call fiber function")
(resume myfiber)
(assert (= myvar 2) "fiber debug statement breaks at proper point")
(assert (= (fiber/status myfiber) :debug) "fiber enters debug state")
(resume myfiber)
(assert (= myvar 4) "fiber resumes properly from debug state")
(assert (= (fiber/status myfiber) :dead) "fiber properly dies from debug state")

# Test max triangle program

# Find the maximum path from the top (root)
# of the triangle to the leaves of the triangle.

(defn myfold [xs ys]
  (let [xs1 [;xs 0]
        xs2 [0 ;xs]
        m1 (map + xs1 ys)
        m2 (map + xs2 ys)]
    (map max m1 m2)))

(defn maxpath [t]
 (extreme > (reduce myfold () t)))

# Test it
# Maximum path is 3 -> 10 -> 3 -> 9 for a total of 25

(def triangle '[
 [3]
 [7 10]
 [4 3 7]
 [8 9 1 3]
])

(assert (= (maxpath triangle) 25) `max triangle`)

(assert (= (string/join @["one" "two" "three"]) "onetwothree") "string/join 1 argument")
(assert (= (string/join @["one" "two" "three"] ", ") "one, two, three") "string/join 2 arguments")
(assert (= (string/join @[] ", ") "") "string/join empty array")

(assert (= (string/find "123" "abc123def") 3) "string/find positive")
(assert (= (string/find "1234" "abc123def") nil) "string/find negative")

# Test destructuring
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

# Marshal

(def um-lookup (env-lookup (fiber/getenv (fiber/current))))
(def m-lookup (invert um-lookup))

(defn testmarsh [x msg]
  (def marshx (marshal x m-lookup))
  (def out (marshal (unmarshal marshx um-lookup) m-lookup))
  (assert (= (string marshx) (string out)) msg))

(testmarsh nil "marshal nil")
(testmarsh false "marshal false")
(testmarsh true "marshal true")
(testmarsh 1 "marshal small integers")
(testmarsh -1 "marshal integers (-1)")
(testmarsh 199 "marshal small integers (199)")
(testmarsh 5000 "marshal medium integers (5000)")
(testmarsh -5000 "marshal small integers (-5000)")
(testmarsh 10000 "marshal large integers (10000)")
(testmarsh -10000 "marshal large integers (-10000)")
(testmarsh 1.0 "marshal double")
(testmarsh "doctordolittle" "marshal string")
(testmarsh :chickenshwarma "marshal symbol")
(testmarsh @"oldmcdonald" "marshal buffer")
(testmarsh @[1 2 3 4 5] "marshal array")
(testmarsh [tuple 1 2 3 4 5] "marshal tuple")
(testmarsh @{1 2 3 4}  "marshal table")
(testmarsh {1 2 3 4}  "marshal struct")
(testmarsh (fn [x] x) "marshal function 0")
(testmarsh (fn name [x] x) "marshal function 1")
(testmarsh (fn [x] (+ 10 x 2)) "marshal function 2")
(testmarsh (fn thing [x] (+ 11 x x 30)) "marshal function 3")
(testmarsh map "marshal function 4")
(testmarsh reduce "marshal function 5")
(testmarsh (fiber/new (fn [] (yield 1) 2)) "marshal simple fiber 1")
(testmarsh (fiber/new (fn [&] (yield 1) 2)) "marshal simple fiber 2")

(def strct {:a @[nil]})
(put (strct :a) 0 strct)
(testmarsh strct "cyclic struct")

# Large functions
(def manydefs (seq [i :range [0 300]] (tuple 'def (gensym) (string "value_" i))))
(array/push manydefs (tuple * 10000 3 5 7 9))
(def f (compile ['do ;manydefs] (fiber/getenv (fiber/current))))
(assert (= (f) (* 10000 3 5 7 9)) "long function compilation")

# Some higher order functions and macros

(def my-array @[1 2 3 4 5 6])
(def x (if-let [x (get my-array 5)] x))
(assert (= x 6) "if-let")
(def x (if-let [y (get @{} :key)] 10 nil))
(assert (not x) "if-let 2")

(assert (= 14 (sum (map inc @[1 2 3 4]))) "sum map")
(def myfun (juxt + - * /))
(assert (= '[2 -2 2 0.5] (myfun 2)) "juxt")

# Case statements
(assert
  (= :six (case (+ 1 2 3)
            1 :one
            2 :two
            3 :three
            4 :four
            5 :five
            6 :six
            7 :seven
            8 :eight
            9 :nine)) "case macro")

(assert (= 7 (case :a :b 5 :c 6 :u 10 7)) "case with default")

# Testing the loop and for macros
(def xs (apply tuple (seq [x :range [0 10] :when (even? x)] (tuple (/ x 2) x))))
(assert (= xs '((0 0) (1 2) (2 4) (3 6) (4 8))) "seq macro 1")

(def xs (apply tuple (seq [x :down [8 -2] :when (even? x)] (tuple (/ x 2) x))))
(assert (= xs '((4 8) (3 6) (2 4) (1 2) (0 0))) "seq macro 2")

# Some testing for not=
(assert (not= 1 1 0) "not= 1")
(assert (not= 0 1 1) "not= 2")

# Closure in while loop
(def closures (seq [i :range [0 5]] (fn [] i)))
(assert (= 0 ((get closures 0))) "closure in loop 0")
(assert (= 1 ((get closures 1))) "closure in loop 1")
(assert (= 2 ((get closures 2))) "closure in loop 2")
(assert (= 3 ((get closures 3))) "closure in loop 3")
(assert (= 4 ((get closures 4))) "closure in loop 4")

# More numerical tests
(assert (== 1 1.0) "numerical equal 1")
(assert (== 0 0.0) "numerical equal 2")
(assert (== 0 -0.0) "numerical equal 3")
(assert (== 2_147_483_647 2_147_483_647.0) "numerical equal 4")
(assert (== -2_147_483_648 -2_147_483_648.0) "numerical equal 5")

# Array tests

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
(assert (array= @[:one :two :three :four :five] @[:one :two :three :four :five]) "array comparison 3")
(assert (array= (array/slice @[1 2 3] 0 2) @[1 2]) "array/slice 1")
(assert (array= (array/slice @[0 7 3 9 1 4] 2 -2) @[3 9 1]) "array/slice 2")

(end-suite)
