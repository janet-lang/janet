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
(start-suite 0)

(assert (= 10 (+ 1 2 3 4)) "addition")
(assert (= -8 (- 1 2 3 4)) "subtraction")
(assert (= 24 (* 1 2 3 4)) "multiplication")
(assert (= 4 (blshift 1 2)) "left shift")
(assert (= 1 (brshift 4 2)) "right shift")
(assert (< 1 2 3 4 5 6) "less than integers")
(assert (< 1.0 2.0 3.0 4.0 5.0 6.0) "less than reals")
(assert (> 6 5 4 3 2 1) "greater than integers")
(assert (> 6.0 5.0 4.0 3.0 2.0 1.0) "greater than reals")
(assert (<= 1 2 3 3 4 5 6) "less than or equal to integers")
(assert (<= 1.0 2.0 3.0 3.0 4.0 5.0 6.0) "less than or equal to reals")
(assert (>= 6 5 4 4 3 2 1) "greater than or equal to integers")
(assert (>= 6.0 5.0 4.0 4.0 3.0 2.0 1.0) "greater than or equal to reals")
(assert (= 7 (% 20 13)) "modulo 1")
(assert (= -7 (% -20 13)) "modulo 2")

(assert (order< 1.0 nil false true
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
                print) "type ordering")

(assert (= (string (buffer "123" "456")) (string @"123456")) "buffer literal")
(assert (= (get {} 1) nil) "get nil from empty struct")
(assert (= (get @{} 1) nil) "get nil from empty table")
(assert (= (get {:boop :bap} :boop) :bap) "get non nil from struct")
(assert (= (get @{:boop :bap} :boop) :bap) "get non nil from table")
(assert (= (get @"\0" 0) 0) "get non nil from buffer")
(assert (= (get @"\0" 1) nil) "get nil from buffer oob")
(assert (put @{} :boop :bap) "can add to empty table")
(assert (put @{1 3} :boop :bap) "can add to non-empty table")

(assert (not false) "false literal")
(assert true "true literal")
(assert (not nil) "nil literal")
(assert (= 7 (bor 3 4)) "bit or")
(assert (= 0 (band 3 4)) "bit and")
(assert (= 0xFF (bxor 0x0F 0xF0)) "bit xor")
(assert (= 0xF0 (bxor 0xFF 0x0F)) "bit xor 2")

# Set global variables to prevent some possible compiler optimizations that defeat point of the test
(var zero 0)
(var one 1)
(var two 2)
(var three 3)
(var plus +)
(assert (= 22 (plus one (plus 1 2 two) (plus 8 (plus zero 1) 4 three))) "nested function calls")

# String literals
(assert (= "abcd" "\x61\x62\x63\x64") "hex escapes")
(assert (= "\e" "\x1B") "escape character")
(assert (= "\x09" "\t") "tab character")

# McCarthy's 91 function
(var f91 nil)
(set f91 (fn [n] (if (> n 100) (- n 10) (f91 (f91 (+ n 11))))))
(assert (= 91 (f91 10)) "f91(10) = 91")
(assert (= 91 (f91 11)) "f91(11) = 91")
(assert (= 91 (f91 20)) "f91(20) = 91")
(assert (= 91 (f91 31)) "f91(31) = 91")
(assert (= 91 (f91 100)) "f91(100) = 91")
(assert (= 91 (f91 101)) "f91(101) = 91")
(assert (= 92 (f91 102)) "f91(102) = 92")
(assert (= 93 (f91 103)) "f91(103) = 93")
(assert (= 94 (f91 104)) "f91(104) = 94")

# Fibonacci
(def fib (do (var fib nil) (set fib (fn [n] (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))))
(def fib2 (fn fib2 [n] (if (< n 2) n (+ (fib2 (- n 1)) (fib2 (- n 2))))))

(assert (= (fib 0) (fib2 0) 0) "fib(0)")
(assert (= (fib 1) (fib2 1) 1) "fib(1)")
(assert (= (fib 2) (fib2 2) 1) "fib(2)")
(assert (= (fib 3) (fib2 3) 2) "fib(3)")
(assert (= (fib 4) (fib2 4) 3) "fib(4)")
(assert (= (fib 5) (fib2 5) 5) "fib(5)")
(assert (= (fib 6) (fib2 6) 8) "fib(6)")
(assert (= (fib 7) (fib2 7) 13) "fib(7)")
(assert (= (fib 8) (fib2 8) 21) "fib(8)")
(assert (= (fib 9) (fib2 9) 34) "fib(9)")
(assert (= (fib 10) (fib2 10) 55) "fib(10)")

# Closure in non function scope
(def outerfun (fn [x y]
                (def c (do
                         (def someval (+ 10 y))
                         (def ctemp (if x (fn [] someval) (fn [] y)))
                         ctemp
                         ))
                (+ 1 2 3 4 5 6 7)
                c))

(assert (= ((outerfun 1 2)) 12) "inner closure 1")
(assert (= ((outerfun nil 2)) 2) "inner closure 2")
(assert (= ((outerfun false 3)) 3) "inner closure 3")

(assert (= '(1 2 3) (quote (1 2 3)) (tuple 1 2 3)) "quote shorthand")

((fn []
   (var accum 1)
   (var count 0)
   (while (< count 16)
     (set accum (blshift accum 1))
     (set count (+ 1 count)))
   (assert (= accum 65536) "loop in closure")))

(var accum 1)
(var count 0)
(while (< count 16)
  (set accum (blshift accum 1))
  (set count (+ 1 count)))
(assert (= accum 65536) "loop globally")

(assert (= (struct 1 2 3 4 5 6 7 8) (struct 7 8 5 6 3 4 1 2)) "struct order does not matter 1")
(assert (= (struct
             :apple 1
             6 :bork
             '(1 2 3) 5)
           (struct
             6 :bork
             '(1 2 3) 5
             :apple 1)) "struct order does not matter 2")

# Symbol function

(assert (= (symbol "abc" 1 2 3) 'abc123) "symbol function")

# Fiber tests

(def afiber (fiber/new (fn []
                         (def x (yield))
                         (error (string "hello, " x))) :ye))

(resume afiber) # first resume to prime
(def afiber-result (resume afiber "world!"))

(assert (= afiber-result "hello, world!") "fiber error result")
(assert (= (fiber/status afiber) :error) "fiber error status")

# yield tests

(def t (fiber/new (fn [&] (yield 1) (yield 2) 3)))

(assert (= 1 (resume t)) "initial transfer to new fiber")
(assert (= 2 (resume t)) "second transfer to fiber")
(assert (= 3 (resume t)) "return from fiber")
(assert (= (fiber/status t) :dead) "finished fiber is dead")

# Var arg tests

(def vargf (fn [more] (apply + more)))

(assert (= 0 (vargf @[])) "var arg no arguments")
(assert (= 1 (vargf @[1])) "var arg no packed arguments")
(assert (= 3 (vargf @[1 2])) "var arg tuple size 1")
(assert (= 10 (vargf @[1 2 3 4])) "var arg tuple size 2, 2 normal args")
(assert (= 110 (vargf @[1 2 3 4 10 10 10 10 10 10 10 10 10 10])) "var arg large tuple")

# Higher order functions

(def compose (fn [f g] (fn [& xs] (f (apply g xs)))))

(def -+ (compose - +))
(def +- (compose + -))

(assert (= (-+ 1 2 3 4) -10) "compose - +")
(assert (= (+- 1 2 3 4) -8) "compose + -")
(assert (= ((compose -+ +-) 1 2 3 4) 8) "compose -+ +-")
(assert (= ((compose +- -+) 1 2 3 4) 10) "compose +- -+")

# UTF-8

#🐙🐙🐙🐙

(def 🦊 :fox)
(def 🐮 :cow)
(assert (= (string "🐼" 🦊 🐮) "🐼foxcow") "emojis 🙉 :)")
(assert (not= 🦊 "🦊") "utf8 strings are not symbols and vice versa")

# Symbols with @ character

(def @ 1)
(assert (= @ 1) "@ symbol")
(def @-- 2)
(assert (= @-- 2) "@-- symbol")
(def @hey 3)
(assert (= @hey 3) "@hey symbol")

# Merge sort

# Imperative (and verbose) merge sort merge
(defn merge
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

(assert (apply <= (merge @[1 3 5] @[2 4 6])) "merge sort merge 1")
(assert (apply <= (merge @[1 2 3] @[4 5 6])) "merge sort merge 2")
(assert (apply <= (merge @[1 3 5] @[2 4 6 6 6 9])) "merge sort merge 3")
(assert (apply <= (merge '(1 3 5) @[2 4 6 6 6 9])) "merge sort merge 4")

# Gensym tests

(assert (not= (gensym) (gensym)) "two gensyms not equal")
((fn []
   (def syms (table))
   (var count 0)
   (while (< count 128)
     (put syms (gensym) true)
     (set count (+ 1 count)))
   (assert (= (length syms) 128) "many symbols")))

# Let

(assert (= (let [a 1 b 2] (+ a b)) 3) "simple let")
(assert (= (let [[a b] @[1 2]] (+ a b)) 3) "destructured let")
(assert (= (let [[a [c d] b] @[1 (tuple 4 3) 2]] (+ a b c d)) 10) "double destructured let")

# Macros

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

# Denormal tables and structs

(assert (= (length {1 2 nil 3}) 1) "nil key struct literal")
(assert (= (length @{1 2 nil 3}) 1) "nil key table literal")
(assert (= (length (struct 1 2 nil 3)) 1) "nil key struct ctor")
(assert (= (length (table 1 2 nil 3)) 1) "nil key table ctor")

(assert (= (length (struct (/ 0 0) 2 1 3)) 1) "nan key struct ctor")
(assert (= (length (table (/ 0 0) 2 1 3)) 1) "nan key table ctor")
(assert (= (length {1 2 nil 3}) 1) "nan key struct literal")
(assert (= (length @{1 2 nil 3}) 1) "nan key table literal")

(assert (= (length (struct 2 1 3 nil)) 1) "nil value struct ctor")
(assert (= (length (table 2 1 3 nil)) 1) "nil value table ctor")
(assert (= (length {1 2 3 nil}) 1) "nil value struct literal")
(assert (= (length @{1 2 3 nil}) 1) "nil value table literal")

# Regression Test
(assert (= 1 (((compile '(fn [] 1) @{})))) "regression test")

# Regression Test #137
(def [a b c] (range 10))
(assert (= a 0) "regression #137 (1)")
(assert (= b 1) "regression #137 (2)")
(assert (= c 2) "regression #137 (3)")

(var [x y z] (range 10))
(assert (= x 0) "regression #137 (4)")
(assert (= y 1) "regression #137 (5)")
(assert (= z 2) "regression #137 (6)")

(assert (= true ;(map truthy? [0 "" true @{} {} [] '()])) "truthy values")
(assert (= false ;(map truthy? [nil false])) "non-truthy values")

(end-suite)

