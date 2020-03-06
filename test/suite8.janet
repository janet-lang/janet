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

(end-suite)
