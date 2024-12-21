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

# Let
# 807f981
(assert (= (let [a 1 b 2] (+ a b)) 3) "simple let")
(assert (= (let [[a b] @[1 2]] (+ a b)) 3) "destructured let")
(assert (= (let [[a [c d] b] @[1 (tuple 4 3) 2]] (+ a b c d)) 10)
        "double destructured let")

# Macros
# b305a7c
(defn dub [x] (+ x x))
(assert (= 2 (dub 1)) "defn macro")
(do
  (defn trip [x] (+ x x x))
  (assert (= 3 (trip 1)) "defn macro triple"))
(do
  (var i 0)
  (when true
    (++ i)
    (++ i)
    (++ i)
    (++ i)
    (++ i)
    (++ i))
  (assert (= i 6) "when macro"))

# Add truthy? to core
# ded08b6
(assert (= true ;(map truthy? [0 "" true @{} {} [] '()])) "truthy values")
(assert (= false ;(map truthy? [nil false])) "non-truthy values")

## Polymorphic comparison -- Issue #272
# 81d301a42

# confirm polymorphic comparison delegation to primitive comparators:
(assert (= 0 (cmp 3 3)) "compare-primitive integers (1)")
(assert (= -1 (cmp 3 5)) "compare-primitive integers (2)")
(assert (= 1 (cmp "foo" "bar")) "compare-primitive strings")
(assert (= 0 (compare 1 1)) "compare integers (1)")
(assert (= -1 (compare 1 2)) "compare integers (2)")
(assert (= 1 (compare "foo" "bar")) "compare strings (1)")

(assert (compare< 1 2 3 4 5 6) "compare less than integers")
(assert (not (compare> 1 2 3 4 5 6)) "compare not greater than integers")
(assert (compare< 1.0 2.0 3.0 4.0 5.0 6.0) "compare less than reals")
(assert (compare> 6 5 4 3 2 1) "compare greater than integers")
(assert (compare> 6.0 5.0 4.0 3.0 2.0 1.0) "compare greater than reals")
(assert (not (compare< 6.0 5.0 4.0 3.0 2.0 1.0)) "compare less than reals")
(assert (compare<= 1 2 3 3 4 5 6) "compare less than or equal to integers")
(assert (compare<= 1.0 2.0 3.0 3.0 4.0 5.0 6.0)
        "compare less than or equal to reals")
(assert (compare>= 6 5 4 4 3 2 1)
        "compare greater than or equal to integers")
(assert (compare>= 6.0 5.0 4.0 4.0 3.0 2.0 1.0)
        "compare greater than or equal to reals")
(assert (compare< 1.0 nil false true
           (fiber/new (fn [] 1))
           "hi"
           (quote hello)
           :hello
           (array 1 2 3)
           (tuple 1 2 3)
           (table "a" "b" "c" "d")
           (struct 1 2 3 4)
           (buffer "hi")
           (fn [x] (+ x x))
           print) "compare type ordering")

# test polymorphic compare with 'objects' (table/setproto)
(def mynum
  @{:type :mynum :v 0 :compare
    (fn [self other]
      (case (type other)
      :number (cmp (self :v) other)
      :table (when (= (get other :type) :mynum)
               (cmp (self :v) (other :v)))))})

(let [n3 (table/setproto @{:v 3} mynum)]
  (assert (= 0 (compare 3 n3)) "compare num to object (1)")
  (assert (= -1 (compare n3 4)) "compare object to num (2)")
  (assert (= 1 (compare (table/setproto @{:v 4} mynum) n3))
          "compare object to object")
  (assert (compare< 2 n3 4) "compare< poly")
  (assert (compare> 4 n3 2) "compare> poly")
  (assert (compare<= 2 3 n3 4) "compare<= poly")
  (assert (compare= 3 n3 (table/setproto @{:v 3} mynum)) "compare= poly")
  (assert (deep= (sorted @[4 5 n3 2] compare<) @[2 n3 4 5])
          "polymorphic sort"))

# Add any? predicate to core
# 7478ad11
(assert (= nil (any? [])) "any? 1")
(assert (= nil (any? [false nil])) "any? 2")
(assert (= false (any? [nil false])) "any? 3")
(assert (= 1 (any? [1])) "any? 4")
(assert (nan? (any? [nil math/nan nil])) "any? 5")
(assert (= true
           (any? [nil nil false nil nil true nil nil nil nil false :a nil]))
        "any? 6")

(assert (= true (every? [])) "every? 1")
(assert (= true (every? [1 true])) "every? 2")
(assert (= 1 (every? [true 1])) "every? 3")
(assert (= nil (every? [nil])) "every? 4")
(assert (= 2 (every? [1 math/nan 2])) "every? 5")
(assert (= false
           (every? [1 1 true 1 1 false 1 1 1 1 true :a nil]))
        "every? 6")

# Some higher order functions and macros
# 5e2de33
(def my-array @[1 2 3 4 5 6])
(assert (= (if-let [x (get my-array 5)] x) 6) "if-let 1")
(assert (= (if-let [y (get @{} :key)] 10 nil) nil) "if-let 2")
(assert (= (if-let [a my-array k (next a)] :t :f) :t) "if-let 3")
(assert (= (if-let [a my-array k (next a 5)] :t :f) :f) "if-let 4")
(assert (= (if-let [[a b] my-array] a) 1) "if-let 5")
(assert (= (if-let [{:a a :b b} {:a 1 :b 2}] b) 2) "if-let 6")
(assert (= (if-let [[a b] nil] :t :f) :f) "if-let 7")

# #1191
(var cnt 0)
(defmacro upcnt [] (++ cnt))
(assert (= (if-let [a true b true c true] nil (upcnt)) nil) "issue #1191")
(assert (= cnt 1) "issue #1191")

(assert (= 14 (sum (map inc @[1 2 3 4]))) "sum map")
(def myfun (juxt + - * /))
(assert (= [2 -2 2 0.5] (myfun 2)) "juxt")

# Case statements
# 5249228
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

# Testing the seq, tabseq, catseq, and loop macros
# 547529e
(def xs (apply tuple (seq [x :range [0 10] :when (even? x)]
                       (tuple (/ x 2) x))))
(assert (= xs '((0 0) (1 2) (2 4) (3 6) (4 8))) "seq macro 1")

# 624be87c9
(def xs (apply tuple (seq [x :down [8 -2] :when (even? x)]
                       (tuple (/ x 2) x))))
(assert (= xs '((4 8) (3 6) (2 4) (1 2) (0 0))) "seq macro 2")

# Looping idea
# 45f8db0
(def xs
  (seq [x :in [-1 0 1] y :in [-1 0 1] :when (not= x y 0)] (tuple x y)))
(def txs (apply tuple xs))

(assert (= txs [[-1 -1] [-1 0] [-1 1] [0 -1] [0 1] [1 -1] [1 0] [1 1]])
        "nested seq")

# :unless modifier
(assert (deep= (seq [i :range [0 10] :unless (odd? i)] i)
               @[0 2 4 6 8])
        ":unless modifier")

# 515891b03
(assert (deep= (tabseq [i :in (range 3)] i (* 3 i))
               @{0 0 1 3 2 6}))

(assert (deep= (tabseq [i :in (range 3)] i)
               @{}))

# ccd874fe4
(def xs (catseq [x :range [0 3]] [x x]))
(assert (deep= xs @[0 0 1 1 2 2]) "catseq")

# :range-to and :down-to
# e0c9910d8
(assert (deep= (seq [x :range-to [0 10]] x) (seq [x :range [0 11]] x))
        "loop :range-to")
(assert (deep= (seq [x :down-to [10 0]] x) (seq [x :down [10 -1]] x))
        "loop :down-to")

# one-term :range forms
(assert (deep= (seq [x :range [10]] x) (seq [x :range [0 10]] x))
        "one-term :range")
(assert (deep= (seq [x :down [10]] x) (seq [x :down [10 0]] x))
        "one-term :down")

# 7880d7320
(def res @{})
(loop [[k v] :pairs @{1 2 3 4 5 6}]
  (put res k v))
(assert (and
          (= (get res 1) 2)
          (= (get res 3) 4)
          (= (get res 5) 6)) "loop :pairs")

# Issue #428
# 08a3687eb
(var result nil)
(defn f [] (yield {:a :ok}))
(assert-no-error "issue 428 1"
                 (loop [{:a x} :in (fiber/new f)] (set result x)))
(assert (= result :ok) "issue 428 2")

# Generators
# 184fe31e0
(def gen (generate [x :range [0 100] :when (pos? (% x 4))] x))
(var gencount 0)
(loop [x :in gen]
  (++ gencount)
  (assert (pos? (% x 4)) "generate in loop"))
(assert (= gencount 75) "generate loop count")

# more loop checks
(assert (deep= (seq [i :range [0 10]] i) @[0 1 2 3 4 5 6 7 8 9]) "seq 1")
(assert (deep= (seq [i :range [0 10 2]] i) @[0 2 4 6 8]) "seq 2")
(assert (deep= (seq [i :range [10]] i) @[0 1 2 3 4 5 6 7 8 9]) "seq 3")
(assert (deep= (seq [i :range-to [10]] i) @[0 1 2 3 4 5 6 7 8 9 10]) "seq 4")
(def gen (generate [x :range-to [0 nil 2]] x))
(assert (deep= (take 5 gen) @[0 2 4 6 8]) "generate nil limit")
(def gen (generate [x :range [0 nil 2]] x))
(assert (deep= (take 5 gen) @[0 2 4 6 8]) "generate nil limit 2")

# Even and odd
# ff163a5ae
(assert (odd? 9) "odd? 1")
(assert (odd? -9) "odd? 2")
(assert (not (odd? 10)) "odd? 3")
(assert (not (odd? 0)) "odd? 4")
(assert (not (odd? -10)) "odd? 5")
(assert (not (odd? 1.1)) "odd? 6")
(assert (not (odd? -0.1)) "odd? 7")
(assert (not (odd? -1.1)) "odd? 8")
(assert (not (odd? -1.6)) "odd? 9")

(assert (even? 10) "even? 1")
(assert (even? -10) "even? 2")
(assert (even? 0) "even? 3")
(assert (not (even? 9)) "even? 4")
(assert (not (even? -9)) "even? 5")
(assert (not (even? 0.1)) "even? 6")
(assert (not (even? -0.1)) "even? 7")
(assert (not (even? -10.1)) "even? 8")
(assert (not (even? -10.6)) "even? 9")

# Map arities
# 25ded775a
(assert (deep= (map inc [1 2 3]) @[2 3 4]))
(assert (deep= (map + [1 2 3] [10 20 30]) @[11 22 33]))
(assert (deep= (map + [1 2 3] [10 20 30] [100 200 300]) @[111 222 333]))
(assert (deep= (map + [1 2 3] [10 20 30] [100 200 300] [1000 2000 3000])
               @[1111 2222 3333]))
(assert (deep= (map +
                    [1 2 3] [10 20 30] [100 200 300] [1000 2000 3000]
                    [10000 20000 30000])
               @[11111 22222 33333]))
# 77e62a2
(assert (deep= (map +
                    [1 2 3] [10 20 30] [100 200 300] [1000 2000 3000]
                    [10000 20000 30000] [100000 200000 300000])
               @[111111 222222 333333]))

# Mapping uses the shortest sequence
# a69799aa4
(assert (deep= (map + [1 2 3 4] [10 20 30]) @[11 22 33]))
(assert (deep= (map + [1 2 3 4] [10 20 30] [100 200]) @[111 222]))
(assert (deep= (map + [1 2 3 4] [10 20 30] [100 200] [1000]) @[1111]))
# 77e62a2
(assert (deep= (map + [1 2 3 4] [10 20 30] [100 200] [1000] []) @[]))

# Variadic arguments to map-like functions
# 77e62a2
(assert (deep= (mapcat tuple [1 2 3 4] [5 6 7 8]) @[1 5 2 6 3 7 4 8]))
(assert (deep= (keep |(if (> $1 0) (/ $0 $1)) [1 2 3 4 5] [1 2 1 0 1])
               @[1 1 3 5]))

(assert (= (count = [1 3 2 4 3 5 4 2 1] [1 2 3 4 5 4 3 2 1]) 4))

(assert (= (some not= (range 5) (range 5)) nil))
(assert (= (some = [1 2 3 4 5] [5 4 3 2 1]) true))

(assert (= (all = (range 5) (range 5)) true))
(assert (= (all not= [1 2 3 4 5] [5 4 3 2 1]) false))

# 4194374
(assert (= false (deep-not= [1] [1])) "issue #1149")

# Merge sort
# f5b29b8
# Imperative (and verbose) merge sort merge
(defn merge-sort
  [xs ys]
  (def ret @[])
  (def xlen (length xs))
  (def ylen (length ys))
  (var i 0)
  (var j 0)
  # Main merge
  (while (if (< i xlen) (< j ylen))
    (def xi (get xs i))
    (def yj (get ys j))
    (if (< xi yj)
      (do (array/push ret xi) (set i (+ i 1)))
      (do (array/push ret yj) (set j (+ j 1)))))
  # Push rest of xs
  (while (< i xlen)
    (def xi (get xs i))
    (array/push ret xi)
    (set i (+ i 1)))
  # Push rest of ys
  (while (< j ylen)
    (def yj (get ys j))
    (array/push ret yj)
    (set j (+ j 1)))
  ret)

(assert (apply <= (merge-sort @[1 3 5] @[2 4 6])) "merge sort merge 1")
(assert (apply <= (merge-sort @[1 2 3] @[4 5 6])) "merge sort merge 2")
(assert (apply <= (merge-sort @[1 3 5] @[2 4 6 6 6 9])) "merge sort merge 3")
(assert (apply <= (merge-sort '(1 3 5) @[2 4 6 6 6 9])) "merge sort merge 4")

(assert (deep= @[1 2 3 4 5] (sort @[5 3 4 1 2])) "sort 1")
(assert (deep= @[{:a 1} {:a 4} {:a 7}]
               (sort-by |($ :a) @[{:a 4} {:a 7} {:a 1}])) "sort 2")
(assert (deep= @[1 2 3 4 5] (sorted [5 3 4 1 2])) "sort 3")
(assert (deep= @[{:a 1} {:a 4} {:a 7}]
               (sorted-by |($ :a) [{:a 4} {:a 7} {:a 1}])) "sort 4")

# Sort function
# 2ca9300bf
(assert (deep=
          (range 99)
          (sort (mapcat (fn [[x y z]] [z y x]) (partition 3 (range 99)))))
        "sort 5")
(assert (<= ;(sort (map (fn [x] (math/random)) (range 1000)))) "sort 6")

# #1283
(assert (deep=
          (partition 2 (generate [ i :in [:a :b :c :d :e]] i))
          '@[(:a :b) (:c :d) (:e)]))
(assert (= (mean (generate [i :in [2 3 5 7 11]] i))
           5.6))

# And and or
# c16a9d846
(assert (= (and true true) true) "and true true")
(assert (= (and true false) false) "and true false")
(assert (= (and false true) false) "and false true")
(assert (= (and true true true) true) "and true true true")
(assert (= (and 0 1 2) 2) "and 0 1 2")
(assert (= (and 0 1 nil) nil) "and 0 1 nil")
(assert (= (and 1) 1) "and 1")
(assert (= (and) true) "and with no arguments")
(assert (= (and 1 true) true) "and with trailing true")
(assert (= (and 1 true 2) 2) "and with internal true")

(assert (= (or true true) true) "or true true")
(assert (= (or true false) true) "or true false")
(assert (= (or false true) true) "or false true")
(assert (= (or false false) false) "or false true")
(assert (= (or true true false) true) "or true true false")
(assert (= (or 0 1 2) 0) "or 0 1 2")
(assert (= (or nil 1 2) 1) "or nil 1 2")
(assert (= (or 1) 1) "or 1")
(assert (= (or) nil) "or with no arguments")

# And/or checks
# 6123c41f1
(assert (= false (and false false)) "and 1")
(assert (= false (or false false)) "or 1")

# 11cd1279d
(assert (deep= @{:a 1 :b 2 :c 3} (zipcoll '[:a :b :c] '[1 2 3])) "zipcoll")

# bc8be266f
(def- a 100)
(assert (= a 100) "def-")

# bc8be266f
(assert (= :first
          (match @[1 3 5]
                 @[x y z] :first
                 :second)) "match 1")

(def val1 :avalue)
(assert (= :second
          (match val1
                 @[x y z] :first
                 :avalue :second
                 :third)) "match 2")

(assert (= 100
           (match @[50 40]
                  @[x x] (* x 3)
                  @[x y] (+ x y 10)
                  0)) "match 3")

# Match checks
# 47e8f669f
(assert (= :hi (match nil nil :hi)) "match 1")
(assert (= :hi (match {:a :hi} {:a a} a)) "match 2")
(assert (= nil (match {:a :hi} {:a a :b b} a)) "match 3")
(assert (= nil (match [1 2] [a b c] a)) "match 4")
(assert (= 2 (match [1 2] [a b] b)) "match 5")
# db631097b
(assert (= [2 :a :b] (match [1 2 :a :b] [o & rest] rest)) "match 6")
(assert (= [] (match @[:a] @[x & r] r :fallback)) "match 7")
(assert (= :fallback (match @[1] @[x y & r] r :fallback)) "match 8")
(assert (= [1 2 3 4] (match @[1 2 3 4] @[x y z & r] [x y z ;r] :fallback))
        "match 9")

# Test cases for #293
# d3b9b8d45
(assert (= :yes (match [1 2 3] [_ a _] :yes :no)) "match wildcard 1")
(assert (= :no (match [1 2 3] [__ a __] :yes :no)) "match wildcard 2")
(assert (= :yes (match [1 2 [1 2 3]] [_ a [_ _ _]] :yes :no))
        "match wildcard 3")
(assert (= :yes (match [1 2 3] (_ (even? 2)) :yes :no)) "match wildcard 4")
(assert (= :yes (match {:a 1} {:a _} :yes :no)) "match wildcard 5")
(assert (= false (match {:a 1 :b 2 :c 3}
                   {:a a :b _ :c _ :d _} :no
                   {:a _ :b _ :c _} false
                   :no)) "match wildcard 6")
(assert (= nil (match {:a 1 :b 2 :c 3}
                 {:a a :b _ :c _ :d _} :no
                 {:a _ :b _ :c _} nil
                 :no)) "match wildcard 7")
# issue #529 - 602010600
(assert (= "t" (match [true nil] [true _] "t")) "match wildcard 8")

# quoted match test
# 425a0fcf0
(assert (= :yes (match 'john 'john :yes _ :nope)) "quoted literal match 1")
(assert (= :nope (match 'john ''john :yes _ :nope)) "quoted literal match 2")

# Some macros
# 7880d7320
(assert (= 2 (if-not 1 3 2)) "if-not 1")
(assert (= 3 (if-not false 3)) "if-not 2")
(assert (= 3 (if-not nil 3 2)) "if-not 3")
(assert (= nil (if-not true 3)) "if-not 4")

(assert (= 4 (unless false (+ 1 2 3) 4)) "unless")

# take
# 18da183ef
(assert (deep= (take 0 []) []) "take 1")
(assert (deep= (take 10 []) []) "take 2")
(assert (deep= (take 0 [1 2 3 4 5]) []) "take 3")
(assert (deep= (take 10 [1 2 3]) [1 2 3]) "take 4")
(assert (deep= (take -1 [:a :b :c]) [:c]) "take 5")
# 34019222c
(assert (deep= (take 3 (generate [x :in [1 2 3 4 5]] x)) @[1 2 3])
        "take from fiber")
# NB: repeatedly resuming a fiber created with `generate` includes a `nil`
# as the final element. Thus a generate of 2 elements will create an array
# of 3.
(assert (= (length (take 4 (generate [x :in [1 2]] x))) 2)
        "take from short fiber")

# take-until
# 18da183ef
(assert (deep= (take-until pos? @[]) []) "take-until 1")
(assert (deep= (take-until pos? @[1 2 3]) []) "take-until 2")
(assert (deep= (take-until pos? @[-1 -2 -3]) [-1 -2 -3]) "take-until 3")
(assert (deep= (take-until pos? @[-1 -2 3]) [-1 -2]) "take-until 4")
(assert (deep= (take-until pos? @[-1 1 -2]) [-1]) "take-until 5")
(assert (deep= (take-until |(= $ 115) "books") "book") "take-until 6")
(assert (deep= (take-until |(= $ 115) (generate [x :in "books"] x))
               @[98 111 111 107]) "take-until from fiber")

# take-while
# 18da183ef
(assert (deep= (take-while neg? @[]) []) "take-while 1")
(assert (deep= (take-while neg? @[1 2 3]) []) "take-while 2")
(assert (deep= (take-while neg? @[-1 -2 -3]) [-1 -2 -3]) "take-while 3")
(assert (deep= (take-while neg? @[-1 -2 3]) [-1 -2]) "take-while 4")
(assert (deep= (take-while neg? @[-1 1 -2]) [-1]) "take-while 5")
(assert (deep= (take-while neg? (generate [x :in  @[-1 1 -2]] x))
               @[-1]) "take-while from fiber")

# drop
# 18da183ef
(assert (deep= (drop 0 []) []) "drop 1")
(assert (deep= (drop 10 []) []) "drop 2")
(assert (deep= (drop 0 [1 2 3 4 5]) [1 2 3 4 5]) "drop 3")
(assert (deep= (drop 10 [1 2 3]) []) "drop 4")
(assert (deep= (drop -1 [1 2 3]) [1 2]) "drop 5")
(assert (deep= (drop -10 [1 2 3]) []) "drop 6")
(assert (deep= (drop 1 "abc") "bc") "drop 7")
(assert (deep= (drop 10 "abc") "") "drop 8")
(assert (deep= (drop -1 "abc") "ab") "drop 9")
(assert (deep= (drop -10 "abc") "") "drop 10")

# drop-until
# 75dc08f
(assert (deep= (drop-until pos? @[]) []) "drop-until 1")
(assert (deep= (drop-until pos? @[1 2 3]) [1 2 3]) "drop-until 2")
(assert (deep= (drop-until pos? @[-1 -2 -3]) []) "drop-until 3")
(assert (deep= (drop-until pos? @[-1 -2 3]) [3]) "drop-until 4")
(assert (deep= (drop-until pos? @[-1 1 -2]) [1 -2]) "drop-until 5")
(assert (deep= (drop-until |(= $ 115) "books") "s") "drop-until 6")

# take-drop symmetry #1178
(def items-list ['abcde :abcde "abcde" @"abcde" [1 2 3 4 5] @[1 2 3 4 5]])

(each items items-list
  (def len (length items))
  (for i 0 (+ len 1)
    (assert (deep= (take i items) (drop (- i len) items)) (string/format "take-drop symmetry %q %d" items i))
    (assert (deep= (take (- i) items) (drop (- len i) items)) (string/format "take-drop symmetry %q %d" items i))))

(defn squares []
  (coro
    (var [a b] [0 1])
    (forever (yield a) (+= a b) (+= b 2))))

(def sqr1 (squares))
(assert (deep= (take 10 sqr1) @[0 1 4 9 16 25 36 49 64 81]))
(assert (deep= (take 1 sqr1) @[100]) "take fiber next value")

(def sqr2 (drop 10 (squares)))
(assert (deep= (take 1 sqr2) @[100]) "drop fiber next value")

(def dict @{:a 1 :b 2 :c 3 :d 4 :e 5})
(def dict1 (take 2 dict))
(def dict2 (drop 2 dict))

(assert (= (length dict1) 2) "take dictionary")
(assert (= (length dict2) 3) "drop dictionary")
(assert (deep= (merge dict1 dict2) dict) "take-drop symmetry for dictionary")

# Comment macro
# issue #110 - 698e89aba
(comment 1)
(comment 1 2)
(comment 1 2 3)
(comment 1 2 3 4)

# comp should be variadic
# 5c83ebd75, 02ce3031
(assert (= 10 ((comp +) 1 2 3 4)) "variadic comp 1")
(assert (= 11 ((comp inc +) 1 2 3 4)) "variadic comp 2")
(assert (= 12 ((comp inc inc +) 1 2 3 4)) "variadic comp 3")
(assert (= 13 ((comp inc inc inc +) 1 2 3 4)) "variadic comp 4")
(assert (= 14 ((comp inc inc inc inc +) 1 2 3 4)) "variadic comp 5")
(assert (= 15 ((comp inc inc inc inc inc +) 1 2 3 4)) "variadic comp 6")
(assert (= 16 ((comp inc inc inc inc inc inc +) 1 2 3 4))
        "variadic comp 7")

# Function shorthand
# 44e752d73
(assert (= (|(+ 1 2 3)) 6) "function shorthand 1")
(assert (= (|(+ 1 2 3 $) 4) 10) "function shorthand 2")
(assert (= (|(+ 1 2 3 $0) 4) 10) "function shorthand 3")
(assert (= (|(+ $0 $0 $0 $0) 4) 16) "function shorthand 4")
(assert (= (|(+ $ $ $ $) 4) 16) "function shorthand 5")
(assert (= (|4) 4) "function shorthand 6")
(assert (= (((|||4))) 4) "function shorthand 7")
(assert (= (|(+ $1 $1 $1 $1) 2 4) 16) "function shorthand 8")
(assert (= (|(+ $0 $1 $3 $2 $6) 0 1 2 3 4 5 6) 12) "function shorthand 9")
# 5f5147652
(assert (= (|(+ $0 $99) ;(range 100)) 99) "function shorthand 10")

# 655d4b3aa
(defn idx= [x y] (= (tuple/slice x) (tuple/slice y)))

# Simple take, drop, etc. tests.
(assert (idx= (take 10 (range 100)) (range 10)) "take 10")
(assert (idx= (drop 10 (range 100)) (range 10 100)) "drop 10")

# with-vars
# 6ceaf9d28
(var abc 123)
(assert (= 356 (with-vars [abc 456] (- abc 100))) "with-vars 1")
(assert-error "with-vars 2" (with-vars [abc 456] (error :oops)))
(assert (= abc 123) "with-vars 3")

# Top level unquote
# 2487162cc
(defn constantly
  []
  (comptime (math/random)))

(assert (= (constantly) (constantly)) "comptime 1")

# issue #232 - b872ee024
(assert-error "arity issue in macro" (eval '(each [])))
# c6b639b93
(assert-error "comptime issue" (eval '(comptime (error "oops"))))

# 962cd7e5f
(var counter 0)
(when-with [x nil |$]
           (++ counter))
(when-with [x 10 |$]
           (+= counter 10))

(assert (= 10 counter) "when-with 1")

(if-with [x nil |$] (++ counter) (+= counter 10))
(if-with [x true |$] (+= counter 20) (+= counter 30))

(assert (= 40 counter) "if-with 1")

# a45509d28
(def a @[])
(eachk x [:a :b :c :d]
  (array/push a x))
(assert (deep= (range 4) a) "eachk 1")

# issue 609 - 1fcaffe
(with-dyns [:err @""]
  (tracev (def my-unique-var-name true))
  (assert my-unique-var-name "tracev upscopes"))

# Prompts and Labels
# 59d288c
(assert (= 10 (label a (for i 0 10 (if (= i 5) (return a 10))))) "label 1")

(defn recur
  [lab x y]
  (when (= x y) (return lab :done))
  (def res (label newlab (recur (or lab newlab) (+ x 1) y)))
  (if lab :oops res))
(assert (= :done (recur nil 0 10)) "label 2")

(assert (= 10 (prompt :a (for i 0 10 (if (= i 5) (return :a 10)))))
        "prompt 1")

(defn- inner-loop
  [i]
  (if (= i 5)
    (return :a 10)))

(assert (= 10 (prompt :a (for i 0 10 (inner-loop i)))) "prompt 2")

(defn- inner-loop2
  [i]
  (try
    (if (= i 5)
      (error 10))
    ([err] (return :a err))))

(assert (= 10 (prompt :a (for i 0 10 (inner-loop2 i)))) "prompt 3")

# chr
# issue 304 - 77343e02e
(assert (= (chr "a") 97) "chr 1")

# Reduce2
# 3eb0927a2
(assert (= (reduce + 0 (range 1 10)) (reduce2 + (range 10))) "reduce2 1")
# 65379741f
(assert (= (reduce * 1 (range 2 10)) (reduce2 * (range 1 10))) "reduce2 2")
(assert (= nil (reduce2 * [])) "reduce2 3")

# Accumulate
# 3eb0927a2
(assert (deep= (accumulate + 0 (range 5)) @[0 1 3 6 10]) "accumulate 1")
(assert (deep= (accumulate2 + (range 5)) @[0 1 3 6 10]) "accumulate2 1")
# 65379741f
(assert (deep= @[] (accumulate2 + [])) "accumulate2 2")
(assert (deep= @[] (accumulate 0 + [])) "accumulate 2")

# in vs get regression
# issue #340 - b63a0796f
(assert (nil? (first @"")) "in vs get 1")
(assert (nil? (last @"")) "in vs get 1")

# index-of
# 259812314
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

# e78a3d1
# NOTE: These is a motivation for the has-value? and has-key? functions below

# returns false despite key present
(assert (= false (index-of 8 {true 7 false 8}))
        "index-of corner key (false) 1")
(assert (= false (index-of 8 @{false 8}))
        "index-of corner key (false) 2")
# still returns null
(assert (= nil (index-of 7 {false 8})) "index-of corner key (false) 3")

# has-value?
(assert (= false (has-value? [] "foo")) "has-value? 1")
(assert (= true (has-value? [4 7 1 3] 4)) "has-value? 2")
(assert (= false (has-value? [4 7 1 3] 22)) "has-value? 3")
(assert (= false (has-value? @[1 2 3] 4)) "has-value? 4")
(assert (= true (has-value? @[:a :b :c] :a)) "has-value? 5")
(assert (= false (has-value? {} :foo)) "has-value? 6")
(assert (= true (has-value? {:a :A :b :B} :A)) "has-value? 7")
(assert (= true (has-value? {:a :A :b :B} :A)) "has-value? 7")
(assert (= true (has-value? @{:a :A :b :B} :A)) "has-value? 8")
(assert (= true (has-value? "abc" (chr "a"))) "has-value? 9")
(assert (= false (has-value? "abc" "1")) "has-value? 10")
# weird true/false corner cases, should align with "index-of corner
# key {k}" cases
(assert (= true (has-value? {true 7 false 8} 8))
        "has-value? corner key (false) 1")
(assert (= true (has-value? @{false 8} 8))
        "has-value? corner key (false) 2")
(assert (= false (has-value? {false 8} 7))
        "has-value? corner key (false) 3")

# has-key?
(do
  (var test-has-key-auto 0)
  (defn test-has-key [col key expected &keys {:name name}]
    ``Test that has-key has the outcome `expected`, and that if
    the result is true, then ensure (in key) does not fail either``
    (assert (boolean? expected))
    (default name (string "has-key? " (++ test-has-key-auto)))
    (assert (= expected (has-key? col key)) name)
    (if
      # guaranteed by `has-key?` to never fail
      expected (in col key)
      # if `has-key?` is false, then `in` should fail (for indexed types)
      #
      # For dictionary types, it should return nil
      (let [[success retval] (protect (in col key))]
        (def should-succeed (dictionary? col))
        (assert
          (= success should-succeed)
          (string/format
            "%s: expected (in col key) to %s, but got %q"
            name (if expected "succeed" "fail") retval)))))

  (test-has-key [] 0 false) # 1
  (test-has-key [4 7 1 3] 2 true) # 2
  (test-has-key [4 7 1 3] 22 false) # 3
  (test-has-key @[1 2 3] 4 false) # 4
  (test-has-key @[:a :b :c] 2 true) # 5
  (test-has-key {} :foo false) # 6
  (test-has-key {:a :A :b :B} :a true) # 7
  (test-has-key {:a :A :b :B} :A false) # 8
  (test-has-key @{:a :A :b :B} :a true) # 9
  (test-has-key "abc" 1 true) # 10
  (test-has-key "abc" 4 false) # 11
  # weird true/false corner cases
  #
  # Tries to mimic the corresponding corner cases in has-value? and
  # index-of, but with keys/values inverted
  #
  # in the first two cases (truthy? (get val col)) would have given false
  # negatives
  (test-has-key {7 true 8 false} 8 true :name
                "has-key? corner value (false) 1")
  (test-has-key @{8 false} 8 true :name
                "has-key? corner value (false) 2")
  (test-has-key @{8 false} 7 false :name
                "has-key? corner value (false) 3"))

# Regression
# issue #463 - 7e7498350
(assert (= {:x 10} (|(let [x $] ~{:x ,x}) 10)) "issue 463")

# macex testing
# 7e7498350
(assert (deep= (macex1 '~{1 2 3 4}) '~{1 2 3 4}) "macex1 qq struct")
(assert (deep= (macex1 '~@{1 2 3 4}) '~@{1 2 3 4}) "macex1 qq table")
(assert (deep= (macex1 '~(1 2 3 4)) '~[1 2 3 4]) "macex1 qq tuple")
(assert (= :brackets (tuple/type (1 (macex1 '~[1 2 3 4]))))
        "macex1 qq bracket tuple")
(assert (deep= (macex1 '~@[1 2 3 4 ,blah]) '~@[1 2 3 4 ,blah])
        "macex1 qq array")

# Sourcemaps in threading macros
# b6175e429
(defn check-threading [macro expansion]
  (def expanded (macex1 (tuple macro 0 '(x) '(y))))
  (assert (= expanded expansion) (string macro " expansion value"))
  (def smap-x (tuple/sourcemap (get expanded 1)))
  (def smap-y (tuple/sourcemap expanded))
  (def line first)
  (defn column [t] (t 1))
  (assert (not= smap-x [-1 -1]) (string macro " x sourcemap existence"))
  (assert (not= smap-y [-1 -1]) (string macro " y sourcemap existence"))
  (assert (or (< (line smap-x) (line smap-y))
              (and (= (line smap-x) (line smap-y))
                   (< (column smap-x) (column smap-y))))
          (string macro " relation between x and y sourcemap")))

(check-threading '-> '(y (x 0)))
(check-threading '->> '(y (x 0)))

# keep-syntax
# b6175e429
(let [brak '[1 2 3]
      par '(1 2 3)]

  (tuple/setmap brak 2 1)

  (assert (deep= (keep-syntax brak @[1 2 3]) @[1 2 3])
          "keep-syntax brackets ignore array")
  (assert (= (keep-syntax! brak @[1 2 3]) '[1 2 3])
          "keep-syntax! brackets replace array")

  (assert (= (keep-syntax! par (map inc @[1 2 3])) '(2 3 4))
          "keep-syntax! parens coerce array")
  (assert (not= (keep-syntax! brak @[1 2 3]) '(1 2 3))
          "keep-syntax! brackets not parens")
  (assert (not= (keep-syntax! par @[1 2 3]) '[1 2 3])
          "keep-syntax! parens not brackets")
  (assert (= (tuple/sourcemap brak)
             (tuple/sourcemap (keep-syntax! brak @[1 2 3])))
          "keep-syntax! brackets source map")

  (keep-syntax par brak)
  (assert (not= (tuple/sourcemap brak) (tuple/sourcemap par))
          "keep-syntax no mutate")
  (assert (= (keep-syntax 1 brak) brak) "keep-syntax brackets ignore type"))

# Curenv
# 28439d822, f7c556e
(assert (= (curenv) (curenv 0)) "curenv 1")
(assert (= (table/getproto (curenv)) (curenv 1)) "curenv 2")
(assert (= nil (curenv 1000000)) "curenv 3")
(assert (= root-env (curenv 1)) "curenv 4")

# Import macro test
# a31e079f9
(assert-no-error "import macro 1" (macex '(import a :as b :fresh maybe)))
(assert (deep= ~(,import* "a" :as "b" :fresh maybe)
               (macex '(import a :as b :fresh maybe))) "import macro 2")

# #477 walk preserving bracket type
# 0a1d902f4
(assert (= :brackets (tuple/type (postwalk identity '[])))
        "walk square brackets 1")
(assert (= :brackets (tuple/type (walk identity '[])))
        "walk square brackets 2")

# Issue #751
# 547fda6a4
(def t {:side false})
(assert (nil? (get-in t [:side :note])) "get-in with false value")
(assert (= (get-in t [:side :note] "dflt") "dflt")
        "get-in with false value and default")

# Evaluate stream with `dofile`
# 9cc4e4812
(def [r w] (os/pipe))
(:write w "(setdyn :x 10)")
(:close w)
(def stream-env (dofile r))
(assert (= (stream-env :x) 10) "dofile stream 1")

# Test thaw and freeze
# 9cc0645a1
(def table-to-freeze @{:c 22 :b [1 2 3 4] :d @"test" :e "test2"})
(def table-to-freeze-with-inline-proto
  @{:a @[1 2 3] :b @[1 2 3 4] :c 22 :d @"test" :e @"test2"})
(def struct-to-thaw
  (struct/with-proto {:a [1 2 3]} :c 22 :b [1 2 3 4] :d "test" :e "test2"))
(table/setproto table-to-freeze @{:a @[1 2 3]})

(assert (deep= struct-to-thaw (freeze table-to-freeze)))
(assert (deep= table-to-freeze-with-inline-proto (thaw table-to-freeze)))
(assert (deep= table-to-freeze-with-inline-proto (thaw struct-to-thaw)))

# Check that freezing mutable keys is deterministic
# for issue #1535
(def hashes @{})
(repeat 200
  (def x (freeze {@"" 1 @"" 2 @"" 3 @"" 4 @"" 5}))
  (put hashes (hash x) true))
(assert (= 1 (length hashes)) "freeze mutable keys is deterministic")

# Make sure Carriage Returns don't end up in doc strings
# e528b86
(assert (not (string/find "\r"
                          (get ((fiber/getenv (fiber/current)) 'cond)
                               :doc "")))
        "no \\r in doc strings")

# cff718f37
(var counter 0)
(def thunk (delay (++ counter)))
(assert (= (thunk) 1) "delay 1")
(assert (= counter 1) "delay 2")
(assert (= (thunk) 1) "delay 3")
(assert (= counter 1) "delay 4")

# maclintf
(def env (table/clone (curenv)))
((compile '(defmacro foo [] (maclintf :strict "oops")) env :anonymous))
(def lints @[])
(compile (tuple/setmap '(foo) 1 2) env :anonymous lints)
(assert (deep= lints @[[:strict 1 2 "oops"]]) "maclintf 1")

(def env (table/clone (curenv)))
((compile '(defmacro foo [& body] (maclintf :strict "foo-oops") ~(do ,;body)) env :anonymous))
((compile '(defmacro bar [] (maclintf :strict "bar-oops")) env :anonymous))
(def lints @[])
# Compile (foo (bar)), but with explicit source map values
(def bar-invoke (tuple/setmap '(bar) 3 4))
(compile (tuple/setmap ~(foo ,bar-invoke) 1 2) env :anonymous lints)
(assert (deep= lints @[[:strict 1 2 "foo-oops"]
                       [:strict 3 4 "bar-oops"]])
        "maclintf 2")

# Bad bytecode wrt. using result from break expression
(defn bytecode-roundtrip
  [f]
  (assert-no-error "bytecode round-trip" (unmarshal (marshal f make-image-dict))))

(defn case-1 [&] (def x (break 1)))
(bytecode-roundtrip case-1)
(defn foo [&])
(defn case-2 [&]
  (foo (break (foo)))
  (foo))
(bytecode-roundtrip case-2)
(defn case-3 [&]
  (def x (break (do (foo)))))
(bytecode-roundtrip case-3)
(defn case-4 [&]
  (def x (break (break (foo)))))
(bytecode-roundtrip case-4)
(defn case-4 [&]
  (def x (break (break (break)))))
(bytecode-roundtrip case-4)
(defn case-5 []
  (def foo (fn [one two] one))
  (foo 100 200))
(bytecode-roundtrip case-5)

# Debug bytecode of these functions
# (pp (disasm case-1))
# (pp (disasm case-2))
# (pp (disasm case-3))

# Regression #1330
(defn regress-1330 [&]
  (def a [1 2 3])
  (def b [;a])
  (identity a))
(assert (= [1 2 3] (regress-1330)) "regression 1330")

# Issue 1341
(assert (= () '() (macex '())) "macex ()")
(assert (= '[] (macex '[])) "macex []")

(assert (= :a (with-env @{:b :a} (dyn :b))) "with-env dyn")
(assert-error "unknown symbol +" (with-env @{} (eval '(+ 1 2))))

(setdyn *debug* true)
(def source '(defn a [x] (+ x x)))
(eval source)
(assert (= 20 (a 10)))
(assert (deep= (get (dyn 'a) :source-form) source))
(setdyn *debug* nil)

# issue #1516
(assert-error "assertf 1 argument" (macex '(assertf true)))
(assert (assertf true "fun message") "assertf 2 arguments")
(assert (assertf true "%s message" "mystery") "assertf 3 arguments")
(assert (assertf (not nil) "%s message" "ordinary") "assertf not nil")
(assert-error "assertf error 2" (assertf false "fun message"))
(assert-error "assertf error 3" (assertf false "%s message" "mystery"))
(assert-error "assertf error 4" (assertf nil "%s %s" "alice" "bob"))

# issue #1535
(loop [i :range [1 1000]]
  (assert (deep-not= @{:key1 "value1" @"key" "value2"}
                     @{:key1 "value1" @"key" "value2"}) "deep= mutable keys"))
(assert (deep-not= {"abc" 123} {@"abc" 123}) "deep= mutable keys vs immutable key")
(assert (deep-not= {@"" 1 @"" 2 @"" 3} {@"" 1 @"" 2 @"" 3}) "deep= duplicate mutable keys")
(assert (deep-not= {@"" @"" @"" @"" @"" 3} {@"" @"" @"" @"" @"" 3}) "deep= duplicate mutable keys 2")
(assert (deep-not= {@[] @"" @[] @"" @[] 3} {@[] @"" @[] @"" @[] 3}) "deep= duplicate mutable keys 3")
(assert (deep-not= {@{} @"" @{} @"" @{} 3} {@{} @"" @{} @"" @{} 3}) "deep= duplicate mutable keys 4")
(assert (deep-not= @{:key1 "value1" @"key2" @"value2"}
                   @{:key1 "value1" @"key2" "value2"}) "deep= mutable keys")
(assert (deep-not= @{:key1 "value1" [@"key2"] @"value2"}
                   @{:key1 "value1" [@"key2"] @"value2"}) "deep= mutable keys")

(end-suite)
