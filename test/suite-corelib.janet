# Copyright (c) 2025 Calvin Rose
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

# ac50f62
(assert (= 10 (+ 1 2 3 4)) "addition")
(assert (= -8 (- 1 2 3 4)) "subtraction")
(assert (= 24 (* 1 2 3 4)) "multiplication")
# d6967a5
(assert (= 4 (blshift 1 2)) "left shift")
(assert (= 1 (brshift 4 2)) "right shift")
# unsigned shift
(assert (= 32768 (brushift 0x80000000 16)) "right shift unsigned 1")
(assert-error "right shift unsigned 2" (= -32768 (brshift 0x80000000 16)))
(assert (= -1 (brshift -1 16)) "right shift unsigned 3")
# non-immediate forms
(assert (= 32768 (brushift 0x80000000 (+ 0 16))) "right shift unsigned non-immediate")
(assert-error "right shift non-immediate" (= -32768 (brshift 0x80000000 (+ 0 16))))
(assert (= -1 (brshift -1 (+ 0 16))) "right shift non-immediate 2")
(assert (= 32768 (blshift 1 (+ 0 15))) "left shift non-immediate")
# 7e46ead
(assert (< 1 2 3 4 5 6) "less than integers")
(assert (< 1.0 2.0 3.0 4.0 5.0 6.0) "less than reals")
(assert (> 6 5 4 3 2 1) "greater than integers")
(assert (> 6.0 5.0 4.0 3.0 2.0 1.0) "greater than reals")
(assert (<= 1 2 3 3 4 5 6) "less than or equal to integers")
(assert (<= 1.0 2.0 3.0 3.0 4.0 5.0 6.0) "less than or equal to reals")
(assert (>= 6 5 4 4 3 2 1) "greater than or equal to integers")
(assert (>= 6.0 5.0 4.0 4.0 3.0 2.0 1.0) "greater than or equal to reals")

(assert (= 7 (% 20 13)) "rem 1")
(assert (= -7 (% -20 13)) "rem 2")
(assert (= 7 (% 20 -13)) "rem 3")
(assert (= -7 (% -20 -13)) "rem 4")
(assert (nan? (% 20 0)) "rem 5")

(assert (= 7 (mod 20 13)) "mod 1")
(assert (= 6 (mod -20 13)) "mod 2")
(assert (= -6 (mod 20 -13)) "mod 3")
(assert (= -7 (mod -20 -13)) "mod 4")
(assert (= 20 (mod 20 0)) "mod 5")

(assert (= 1 (div 20 13)) "div 1")
(assert (= -2 (div -20 13)) "div 2")
(assert (= -2 (div 20 -13)) "div 3")
(assert (= 1 (div -20 -13)) "div 4")
(assert (= math/inf (div 20 0)) "div 5")

(assert (all = (seq [n :range [0 10]] (mod n 5 3))
               (seq [n :range [0 10]] (% n 5 3))
               [0 1 2 0 1 0 1 2 0 1]) "variadic mod")

# linspace range
(assert (deep= @[0 1 2 3] (range 4)) "range 1")
(assert (deep= @[0 1 2 3] (range 3.01)) "range 2")
(assert (deep= @[0 1 2 3] (range 3.999)) "range 3")
(assert (deep= @[0.8 1.8 2.8 3.8] (range 0.8 3.999)) "range 4")
(assert (deep= @[0.8 1.8 2.8 3.8] (range 0.8 3.999)) "range 5")

(assert (< 1.0 nil false true
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

# b305a7c9b
(assert (= (string (buffer "123" "456")) (string @"123456")) "buffer literal")
# 277117165
(assert (= (get {} 1) nil) "get nil from empty struct")
(assert (= (get @{} 1) nil) "get nil from empty table")
(assert (= (get {:boop :bap} :boop) :bap) "get non nil from struct")
(assert (= (get @{:boop :bap} :boop) :bap) "get non nil from table")
(assert (= (get @"\0" 0) 0) "get non nil from buffer")
(assert (= (get @"\0" 1) nil) "get nil from buffer oob")
(assert (put @{} :boop :bap) "can add to empty table")
(assert (put @{1 3} :boop :bap) "can add to non-empty table")
# 7e46ead
(assert (= 7 (bor 3 4)) "bit or")
(assert (= 0 (band 3 4)) "bit and")
# f41dab8
(assert (= 0xFF (bxor 0x0F 0xF0)) "bit xor")
(assert (= 0xF0 (bxor 0xFF 0x0F)) "bit xor 2")

# Some testing for not=
# 08f6c642d
(assert (not= 1 1 0) "not= 1")
(assert (not= 0 1 1) "not= 2")

# Check if abstract test works
# d791077e2
(assert (abstract? stdout) "abstract? stdout")
(assert (abstract? stdin) "abstract? stdin")
(assert (abstract? stderr) "abstract? stderr")
(assert (not (abstract? nil)) "not abstract? nil")
(assert (not (abstract? 1)) "not abstract? 1")
(assert (not (abstract? 3)) "not abstract? 3")
(assert (not (abstract? 5)) "not abstract? 5")

# Module path expansion
# ff3bb6627
(setdyn :current-file "some-dir/some-file")
(defn test-expand [path temp]
  (string (module/expand-path path temp)))

(assert (= (test-expand "abc" ":cur:/:all:") "some-dir/abc")
        "module/expand-path 1")
(assert (= (test-expand "./abc" ":cur:/:all:") "some-dir/abc")
        "module/expand-path 2")
(assert (= (test-expand "abc/def.txt" ":cur:/:name:") "some-dir/def.txt")
        "module/expand-path 3")
(assert (= (test-expand "abc/def.txt" ":cur:/:dir:/sub/:name:")
           "some-dir/abc/sub/def.txt") "module/expand-path 4")
# fc46030e7
(assert (= (test-expand "/abc/../def.txt" ":all:") "/def.txt")
        "module/expand-path 5")
(assert (= (test-expand "abc/../def.txt" ":all:") "def.txt")
        "module/expand-path 6")
(assert (= (test-expand "../def.txt" ":all:") "../def.txt")
        "module/expand-path 7")
(assert (= (test-expand "../././././abcd/../def.txt" ":all:") "../def.txt")
        "module/expand-path 8")

# module/expand-path regression
# issue #143 - e0fe8476a
(with-dyns [:syspath ".janet/.janet"]
  (assert (= (string (module/expand-path "hello" ":sys:/:all:.janet"))
             ".janet/.janet/hello.janet") "module/expand-path 1"))

# int?
(assert (int? 1) "int? 1")
(assert (int? -1) "int? -1")
(assert (not (int? true)) "int? true")
(assert (not (int? 3.14)) "int? 3.14")
(assert (not (int? 8589934592)) "int? 8589934592")

# memcmp
(assert (= (memcmp "123helloabcd" "1234helloabc" 5 3 4) 0) "memcmp 1")
(assert (< (memcmp "123hellaabcd" "1234helloabc" 5 3 4) 0) "memcmp 2")
(assert (> (memcmp "123helloabcd" "1234hellaabc" 5 3 4) 0) "memcmp 3")
(assert-error "invalid offset-a: 1" (memcmp "a" "b" 1 1 0))
(assert-error "invalid offset-b: 1" (memcmp "a" "b" 1 0 1))

# Range
# a982f351d
(assert (deep= (range 10) @[0 1 2 3 4 5 6 7 8 9]) "(range 10)")
(assert (deep= (range 5 10) @[5 6 7 8 9]) "(range 5 10)")
(assert (deep= (range 0 16 4) @[0 4 8 12]) "(range 0 16 4)")
(assert (deep= (range 0 17 4) @[0 4 8 12 16]) "(range 0 17 4)")
(assert (deep= (range 16 0 -4) @[16 12 8 4]) "(range 16 0 -4)")
(assert (deep= (range 17 0 -4) @[17 13 9 5 1]) "(range 17 0 -4)")
(assert-error "large range" (range 0xFFFFFFFFFF))

(assert (= (length (range 10)) 10) "(range 10)")
(assert (= (length (range -10)) 0) "(range -10)")
(assert (= (length (range 1 10)) 9) "(range 1 10)")

# iterating over generator
(assert-no-error "iterate over coro 1" (values (generate [x :range [0 10]] x)))
(assert-no-error "iterate over coro 2" (keys (generate [x :range [0 10]] x)))
(assert-no-error "iterate over coro 3" (pairs (generate [x :range [0 10]] x)))

(end-suite)

