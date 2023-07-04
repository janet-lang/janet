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

# Set global variables to prevent some possible compiler optimizations
# that defeat point of the test
# 2771171
(var zero 0)
(var one 1)
(var two 2)
(var three 3)
(var plus +)
(assert (= 22 (plus one (plus 1 2 two) (plus 8 (plus zero 1) 4 three)))
        "nested function calls")

# McCarthy's 91 function
# 2771171
(var f91 nil)
(set f91 (fn [n]
           (if (> n 100)
             (- n 10)
             (f91 (f91 (+ n 11))))))
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
# 23196ff
(def fib
  (do
    (var fib nil)
    (set fib (fn [n]
               (if (< n 2)
                 n
                 (+ (fib (- n 1)) (fib (- n 2))))))))
(def fib2
  (fn fib2 [n]
    (if (< n 2)
      n
      (+ (fib2 (- n 1)) (fib2 (- n 2))))))

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
# 911b0b1
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

# d6967a5
((fn []
   (var accum 1)
   (var counter 0)
   (while (< counter 16)
     (set accum (blshift accum 1))
     (set counter (+ 1 counter)))
   (assert (= accum 65536) "loop in closure")))

(var accum 1)
(var counter 0)
(while (< counter 16)
  (set accum (blshift accum 1))
  (set counter (+ 1 counter)))
(assert (= accum 65536) "loop globally")

# Fiber tests
# 21bd960
(def afiber (fiber/new (fn []
                         (def x (yield))
                         (error (string "hello, " x))) :ye))

(resume afiber) # first resume to prime
(def afiber-result (resume afiber "world!"))

(assert (= afiber-result "hello, world!") "fiber error result")
(assert (= (fiber/status afiber) :error) "fiber error status")

# Var arg tests
# f054586
(def vargf (fn [more] (apply + more)))

(assert (= 0 (vargf @[])) "var arg no arguments")
(assert (= 1 (vargf @[1])) "var arg no packed arguments")
(assert (= 3 (vargf @[1 2])) "var arg tuple size 1")
(assert (= 10 (vargf @[1 2 3 4])) "var arg tuple size 2, 2 normal args")
(assert (= 110 (vargf @[1 2 3 4 10 10 10 10 10 10 10 10 10 10]))
        "var arg large tuple")

# Higher order functions
# d9f24ef
(def compose (fn [f g] (fn [& xs] (f (apply g xs)))))

(def -+ (compose - +))
(def +- (compose + -))

(assert (= (-+ 1 2 3 4) -10) "compose - +")
(assert (= (+- 1 2 3 4) -8) "compose + -")
(assert (= ((compose -+ +-) 1 2 3 4) 8) "compose -+ +-")
(assert (= ((compose +- -+) 1 2 3 4) 10) "compose +- -+")

# UTF-8
# d9f24ef
#🐙🐙🐙🐙

(defn foo [Θa Θb Θc] 0)
(def 🦊 :fox)
(def 🐮 :cow)
(assert (= (string "🐼" 🦊 🐮) "🐼foxcow") "emojis 🙉 :)")
(assert (not= 🦊 "🦊") "utf8 strings are not symbols and vice versa")
(assert (= "\U01F637" "😷") "unicode escape 1")
(assert (= "\u2623" "\U002623" "☣") "unicode escape 2")
(assert (= "\u24c2" "\U0024c2" "Ⓜ") "unicode escape 3")
(assert (= "\u0061" "a") "unicode escape 4")

# Test max triangle program
# c0e373f
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

# Large functions
# 6822400
(def manydefs (seq [i :range [0 300]]
                (tuple 'def (gensym) (string "value_" i))))
(array/push manydefs (tuple * 10000 3 5 7 9))
(def f (compile ['do ;manydefs] (fiber/getenv (fiber/current))))
(assert (= (f) (* 10000 3 5 7 9)) "long function compilation")

# Closure in while loop
# abe7d59
(def closures (seq [i :range [0 5]] (fn [] i)))
(assert (= 0 ((get closures 0))) "closure in loop 0")
(assert (= 1 ((get closures 1))) "closure in loop 1")
(assert (= 2 ((get closures 2))) "closure in loop 2")
(assert (= 3 ((get closures 3))) "closure in loop 3")
(assert (= 4 ((get closures 4))) "closure in loop 4")

# Another regression test - no segfaults
# 6b4824c
(defn afn [x] x)
(var afn-var afn)
(var identity-var identity)
(var map-var map)
(var not-var not)
(assert (= 1 (try (afn-var) ([err] 1))) "bad arity 1")
(assert (= 4 (try ((fn [x y] (+ x y)) 1) ([_] 4))) "bad arity 2")
(assert (= 1 (try (identity-var) ([err] 1))) "bad arity 3")
(assert (= 1 (try (map-var) ([err] 1))) "bad arity 4")
(assert (= 1 (try (not-var) ([err] 1))) "bad arity 5")

# Detaching closure over non resumable fiber
# issue #317 - 7c4ffe9b9
(do
  (defn f1
    [a]
    (defn f1 [] (++ (a 0)))
    (defn f2 [] (++ (a 0)))
    (error [f1 f2]))
  (def [_ [f1 f2]] (protect (f1 @[0])))
  # At time of writing, mark phase can detach closure envs.
  (gccollect)
  (assert (= 1 (f1)) "detach-non-resumable-closure 1")
  (assert (= 2 (f2)) "detach-non-resumable-closure 2"))

# Dynamic defs
# ec65f03
(def staticdef1 0)
(defn staticdef1-inc [] (+ 1 staticdef1))
(assert (= 1 (staticdef1-inc)) "before redefinition without :redef")
(def staticdef1 1)
(assert (= 1 (staticdef1-inc)) "after redefinition without :redef")
(setdyn :redef true)
(def dynamicdef2 0)
(defn dynamicdef2-inc [] (+ 1 dynamicdef2))
(assert (= 1 (dynamicdef2-inc)) "before redefinition with dyn :redef")
(def dynamicdef2 1)
(assert (= 2 (dynamicdef2-inc)) "after redefinition with dyn :redef")
(setdyn :redef nil)

# missing symbols
# issue #914 - 1eb34989d
(defn lookup-symbol [sym] (defglobal sym 10) (dyn sym))

(setdyn :missing-symbol lookup-symbol)

(assert (= (eval-string "(+ a 5)") 15) "lookup missing symbol")

(setdyn :missing-symbol nil)
(setdyn 'a nil)

(assert-error "compile error" (eval-string "(+ a 5)"))

# 88813c4
(assert (deep= (in (disasm (defn a [] (def x 10) x)) :symbolmap)
               @[[0 2 0 'a] [0 2 1 'x]])
        "symbolmap when *debug* is true")

(defn a [arg]
  (def x 10)
  (do
    (def y 20)
    (def z 30)
    (+ x y z)))
(def symbolslots (in (disasm a) :symbolslots))
(def f (asm (disasm a)))
(assert (deep= (in (disasm f) :symbolslots)
               symbolslots)
        "symbolslots survive disasm/asm")

(comment
  (setdyn *debug* true)
  (setdyn :pretty-format "%.40M")
  (def f (fn [x] (fn [y] (+ x y))))
  (assert (deep= (map last (in (disasm (f 10)) :symbolmap))
                 @['x 'y])
          "symbolmap upvalues"))

(assert (deep= (in (disasm (defn a [arg]
                             (def x 10)
                             (do
                               (def y 20)
                               (def z 30)
                               (+ x y z)))) :symbolmap)
               @[[0 6 0 'arg]
                 [0 6 1 'a]
                 [0 6 2 'x]
                 [1 6 3 'y]
                 [2 6 4 'z]])
        "arg & inner symbolmap")

(end-suite)

