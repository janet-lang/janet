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
(start-suite 8)

###
### Compiling brainfuck to Janet.
###

(def- bf-peg
  "Peg for compiling brainfuck into a Janet source ast."
  (peg/compile
    ~{:+ (/ '(some "+") ,(fn [x] ~(+= (DATA POS) ,(length x))))
      :- (/ '(some "-") ,(fn [x] ~(-= (DATA POS) ,(length x))))
      :> (/ '(some ">") ,(fn [x] ~(+= POS ,(length x))))
      :< (/ '(some "<") ,(fn [x] ~(-= POS ,(length x))))
      :. (* "." (constant (prinf "%c" (get DATA POS))))
      :loop (/ (* "[" :main "]") ,(fn [& captures]
                                    ~(while (not= (get DATA POS) 0)
                                       ,;captures)))
      :main (any (+ :s :loop :+ :- :> :< :.)) }))

(defn bf
  "Run brainfuck."
  [text]
  (eval
    ~(let [DATA (array/new-filled 100 0)]
       (var POS 50)
       ,;(peg/match bf-peg text))))

(defn test-bf
  "Test some bf for expected output."
  [input output]
  (def b @"")
  (with-dyns [:out b]
    (bf input))
  (assert (= (string output) (string b))
          (string "bf input '"
                  input
                  "' failed, expected "
                  (describe output)
                  ", got "
                  (describe (string b))
                  ".")))

(test-bf "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++." "Hello World!\n")

(test-bf ">++++++++[-<+++++++++>]<.>>+>-[+]++>++>+++[>[->+++<<+++>]<<]>-----.>->
+++..+++.>-.<<+[>[+>+]>>]<--------------.>>.+++.------.--------.>+.>+."
         "Hello World!\n")

(test-bf "+[+[<<<+>>>>]+<-<-<<<+<++]<<.<++.<++..+++.<<++.<---.>>.>.+++.------.>-.>>--."
         "Hello, World!")

# Prompts and Labels

(assert (= 10 (label a (for i 0 10 (if (= i 5) (return a 10))))) "label 1")

(defn recur
  [lab x y]
  (when (= x y) (return lab :done))
  (def res (label newlab (recur (or lab newlab) (+ x 1) y)))
  (if lab :oops res))
(assert (= :done (recur nil 0 10)) "label 2")

(assert (= 10 (prompt :a (for i 0 10 (if (= i 5) (return :a 10))))) "prompt 1")

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

# Match checks

(assert (= :hi (match nil nil :hi)) "match 1")
(assert (= :hi (match {:a :hi} {:a a} a)) "match 2")
(assert (= nil (match {:a :hi} {:a a :b b} a)) "match 3")
(assert (= nil (match [1 2] [a b c] a)) "match 4")
(assert (= 2 (match [1 2] [a b] b)) "match 5")

# And/or checks

(assert (= false (and false false)) "and 1")
(assert (= false (or false false)) "or 1")

# #300 Regression test

# Just don't segfault
(assert (peg/match '{:main (replace "S" {"S" :spade})} "S7") "regression #300")

# Test cases for #293
(assert (= :yes (match [1 2 3] [_ a _] :yes :no)) "match wildcard 1")
(assert (= :no (match [1 2 3] [__ a __] :yes :no)) "match wildcard 2")
(assert (= :yes (match [1 2 [1 2 3]] [_ a [_ _ _]] :yes :no)) "match wildcard 3")
(assert (= :yes (match [1 2 3] (_ (even? 2)) :yes :no)) "match wildcard 4")
(assert (= :yes (match {:a 1} {:a _} :yes :no)) "match wildcard 5")
(assert (= false (match {:a 1 :b 2 :c 3} {:a a :b _ :c _ :d _} :no {:a _ :b _ :c _} false :no)) "match wildcard 6")
(assert (= nil (match {:a 1 :b 2 :c 3} {:a a :b _ :c _ :d _} :no {:a _ :b _ :c _} nil :no)) "match wildcard 7")

# Regression #301
(def b (buffer/new-filled 128 0x78))
(assert (= 38 (length (buffer/blit @"" b -1 90))) "buffer/blit 1")

(def a @"abcdefghijklm")
(assert (deep= @"abcde" (buffer/blit @"" a -1 0 5)) "buffer/blit 2")
(assert (deep= @"bcde" (buffer/blit @"" a -1 1 5)) "buffer/blit 3")
(assert (deep= @"cde" (buffer/blit @"" a -1 2 5)) "buffer/blit 4")
(assert (deep= @"de" (buffer/blit @"" a -1 3 5)) "buffer/blit 5")

# chr
(assert (= (chr "a") 97) "chr 1")

# Detaching closure over non resumable fiber.
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

# Marshal closure over non resumable fiber.
(do
  (defn f1
    [a]
    (defn f1 [] (++ (a 0)))
    (defn f2 [] (++ (a 0)))
    (error [f1 f2]))
  (def [_ tup] (protect (f1 @[0])))
  (def [f1 f2] (unmarshal (marshal tup make-image-dict) load-image-dict))
  (assert (= 1 (f1)) "marshal-non-resumable-closure 1")
  (assert (= 2 (f2)) "marshal-non-resumable-closure 2"))

# Marshal closure over currently alive fiber.
(do
  (defn f1
    [a]
    (defn f1 [] (++ (a 0)))
    (defn f2 [] (++ (a 0)))
    (marshal [f1 f2] make-image-dict))
  (def [f1 f2] (unmarshal (f1 @[0]) load-image-dict))
  (assert (= 1 (f1)) "marshal-live-closure 1")
  (assert (= 2 (f2)) "marshal-live-closure 2"))

(do
  (var a 1)
  (defn b [x] (+ a x))
  (def c (unmarshal (marshal b)))
  (assert (= 2 (c 1)) "marshal-on-stack-closure 1"))

# Reduce2

(assert (= (reduce + 0 (range 1 10)) (reduce2 + (range 10))) "reduce2 1")
(assert (= (reduce * 1 (range 2 10)) (reduce2 * (range 1 10))) "reduce2 2")
(assert (= nil (reduce2 * [])) "reduce2 3")

# Accumulate

(assert (deep= (accumulate + 0 (range 5)) @[0 1 3 6 10]) "accumulate 1")
(assert (deep= (accumulate2 + (range 5)) @[0 1 3 6 10]) "accumulate2 1")
(assert (deep= @[] (accumulate2 + [])) "accumulate2 2")
(assert (deep= @[] (accumulate 0 + [])) "accumulate 2")

# Perm strings

(assert (= (os/perm-int "rwxrwxrwx") 8r777) "perm 1")
(assert (= (os/perm-int "rwxr-xr-x") 8r755) "perm 2")
(assert (= (os/perm-int "rw-r--r--") 8r644) "perm 3")

(assert (= (band (os/perm-int "rwxrwxrwx") 8r077) 8r077) "perm 4")
(assert (= (band (os/perm-int "rwxr-xr-x") 8r077) 8r055) "perm 5")
(assert (= (band (os/perm-int "rw-r--r--") 8r077) 8r044) "perm 6")

(assert (= (os/perm-string 8r777) "rwxrwxrwx") "perm 7")
(assert (= (os/perm-string 8r755) "rwxr-xr-x") "perm 8")
(assert (= (os/perm-string 8r644) "rw-r--r--") "perm 9")

# Issue #336 cases - don't segfault

(assert-error "unmarshal errors 1" (unmarshal @"\xd6\xb9\xb9"))
(assert-error "unmarshal errors 2" (unmarshal @"\xd7bc"))
(assert-error "unmarshal errors 3" (unmarshal "\xd3\x01\xd9\x01\x62\xcf\x03\x78\x79\x7a" load-image-dict))
(assert-error "unmarshal errors 4"
              (unmarshal
                @"\xD7\xCD\0e/p\x98\0\0\x03\x01\x01\x01\x02\0\0\x04\0\xCEe/p../tools
\0\0\0/afl\0\0\x01\0erate\xDE\xDE\xDE\xDE\xDE\xDE\xDE\xDE\xDE\xDE
\xA8\xDE\xDE\xDE\xDE\xDE\xDE\0\0\0\xDE\xDE_unmarshal_testcase3.ja
neldb\0\0\0\xD8\x05printG\x01\0\xDE\xDE\xDE'\x03\0marshal_tes/\x02
\0\0\0\0\0*\xFE\x01\04\x02\0\0'\x03\0\r\0\r\0\r\0\r" load-image-dict))

# No segfault, valgrind clean.
(def x @"\xCC\xCD.nd\x80\0\r\x1C\xCDg!\0\x07\xCC\xCD\r\x1Ce\x10\0\r;\xCDb\x04\xFF9\xFF\x80\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04uu\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\0\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04}\x04\x04\x04\x04\x04\x04\x04\x04#\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\0\x01\0\0\x03\x04\x04\x04\xE2\x03\x04\x04\x04\x04\x04\x04\x04\x04\x04\x14\x1A\x04\x04\x04\x04\x04\x18\x04\x04!\x04\xE2\x03\x04\x04\x04\x04\x04\x04$\x04\x04\x04\x04\x04\x04\x04\x04\x04\x80\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04\x04A\0\0\0\x03\0\0!\xBF\xFF")
(unmarshal x load-image-dict)
(gccollect)
(marshal x make-image-dict)

(end-suite)
