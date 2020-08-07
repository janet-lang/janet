# Copyright (c) 2020 Calvin Rose
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
(start-suite 3)

(assert (= (length (range 10)) 10) "(range 10)")
(assert (= (length (range 1 10)) 9) "(range 1 10)")
(assert (deep= @{:a 1 :b 2 :c 3} (zipcoll '[:a :b :c] '[1 2 3])) "zipcoll")

(def- a 100)
(assert (= a 100) "def-")

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

# Edge case should cause old compilers to fail due to
# if statement optimization
(var var-a 1)
(var var-b (if false 2 (string "hello")))

(assert (= var-b "hello") "regression 1")

# Scan number

(assert (= 1 (scan-number "1")) "scan-number 1")
(assert (= -1 (scan-number "-1")) "scan-number -1")
(assert (= 1.3e4 (scan-number "1.3e4")) "scan-number 1.3e4")

# Some macros

(assert (= 2 (if-not 1 3 2)) "if-not 1")
(assert (= 3 (if-not false 3)) "if-not 2")
(assert (= 3 (if-not nil 3 2)) "if-not 3")
(assert (= nil (if-not true 3)) "if-not 4")

(assert (= 4 (unless false (+ 1 2 3) 4)) "unless")

(def res @{})
(loop [[k v] :pairs @{1 2 3 4 5 6}]
  (put res k v))
(assert (and
          (= (get res 1) 2)
          (= (get res 3) 4)
          (= (get res 5) 6)) "loop :pairs")

# Another regression test - no segfaults
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

# Assembly test
# Fibonacci sequence, implemented with naive recursion.
(def fibasm (asm '{
  :arity 1
  :bytecode [
    (ltim 1 0 0x2)      # $1 = $0 < 2
    (jmpif 1 :done)     # if ($1) goto :done
    (lds 1)             # $1 = self
    (addim 0 0 -0x1)    # $0 = $0 - 1
    (push 0)            # push($0), push argument for next function call
    (call 2 1)          # $2 = call($1)
    (addim 0 0 -0x1)    # $0 = $0 - 1
    (push 0)            # push($0)
    (call 0 1)          # $0 = call($1)
    (add 0 0 2)        # $0 = $0 + $2 (integers)
    :done
    (ret 0)             # return $0
  ]
}))

(assert (= 0 (fibasm 0)) "fibasm 1")
(assert (= 1 (fibasm 1)) "fibasm 2")
(assert (= 55 (fibasm 10)) "fibasm 3")
(assert (= 6765 (fibasm 20)) "fibasm 4")

# Calling non functions

(assert (= 1 ({:ok 1} :ok)) "calling struct")
(assert (= 2 (@{:ok 2} :ok)) "calling table")
(assert (= :bad (try ((identity @{:ok 2}) :ok :no) ([err] :bad))) "calling table too many arguments")
(assert (= :bad (try ((identity :ok) @{:ok 2} :no) ([err] :bad))) "calling keyword too many arguments")
(assert (= :oops (try ((+ 2 -1) 1) ([err] :oops))) "calling number fails")

# Method test

(def Dog @{:bark (fn bark [self what] (string (self :name) " says " what "!"))})
(defn make-dog
  [name]
  (table/setproto @{:name name} Dog))

(assert (= "fido" ((make-dog "fido") :name)) "oo 1")
(def spot (make-dog "spot"))
(assert (= "spot says hi!" (:bark spot "hi")) "oo 2")

# Negative tests

(assert-error "+ check types" (+ 1 ()))
(assert-error "- check types" (- 1 ()))
(assert-error "* check types" (* 1 ()))
(assert-error "/ check types" (/ 1 ()))
(assert-error "band check types" (band 1 ()))
(assert-error "bor check types" (bor 1 ()))
(assert-error "bxor check types" (bxor 1 ()))
(assert-error "bnot check types" (bnot ()))

# Buffer blitting

(def b (buffer/new-filled 100))
(buffer/bit-set b 100)
(buffer/bit-clear b 100)
(assert (zero? (sum b)) "buffer bit set and clear")
(buffer/bit-toggle b 101)
(assert (= 32 (sum b)) "buffer bit set and clear")

(def b2 @"hello world")

(buffer/blit b2 "joyto ")
(assert (= (string b2) "joyto world") "buffer/blit 1")

(buffer/blit b2 "joyto" 6)
(assert (= (string b2) "joyto joyto") "buffer/blit 2")

(buffer/blit b2 "abcdefg" 5 6)
(assert (= (string b2) "joytogjoyto") "buffer/blit 3")

# Buffer self blitting, check for use after free
(def buf1 @"1234567890")
(buffer/blit buf1 buf1 -1)
(buffer/blit buf1 buf1 -1)
(buffer/blit buf1 buf1 -1)
(buffer/blit buf1 buf1 -1)
(assert (= (string buf1) (string/repeat "1234567890" 16)) "buffer blit against self")

# Buffer push word

(def b3 @"")
(buffer/push-word b3 0xFF 0x11)
(assert (= 8 (length b3)) "buffer/push-word 1")
(assert (= "\xFF\0\0\0\x11\0\0\0" (string b3)) "buffer/push-word 2")
(buffer/clear b3)
(buffer/push-word b3 0xFFFFFFFF 0x1100)
(assert (= 8 (length b3)) "buffer/push-word 3")
(assert (= "\xFF\xFF\xFF\xFF\0\x11\0\0" (string b3)) "buffer/push-word 4")

# Buffer push string

(def b4 (buffer/new-filled 10 0))
(buffer/push-string b4 b4)
(assert (= "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0" (string b4)) "buffer/push-buffer 1")
(def b5 @"123")
(buffer/push-string b5 "456" @"789")
(assert (= "123456789" (string b5)) "buffer/push-buffer 2")

# Check for bugs with printing self with buffer/format

(def buftemp @"abcd")
(assert (= (string (buffer/format buftemp "---%p---" buftemp)) `abcd---@"abcd"---`) "buffer/format on self 1")
(def buftemp @"abcd")
(assert (= (string (buffer/format buftemp "---%p %p---" buftemp buftemp)) `abcd---@"abcd" @"abcd"---`) "buffer/format on self 2")

# Peg

(defn check-match
  [pat text should-match]
  (def result (peg/match pat text))
  (assert (= (not should-match) (not result)) (string "check-match " text)))

(defn check-deep
  [pat text what]
  (def result (peg/match pat text))
  (assert (deep= result what) (string "check-deep " text)))

# Just numbers

(check-match '(* 4 -1) "abcd" true)
(check-match '(* 4 -1) "abc" false)
(check-match '(* 4 -1) "abcde" false)

# Simple pattern

(check-match '(* (some (range "az" "AZ")) -1) "hello" true)
(check-match '(* (some (range "az" "AZ")) -1) "hello world" false)
(check-match '(* (some (range "az" "AZ")) -1) "1he11o" false)
(check-match '(* (some (range "az" "AZ")) -1) "" false)

# Pre compile

(def pegleg (peg/compile '{:item "abc" :main (* :item "," :item -1)}))

(peg/match pegleg "abc,abc")

# Bad Grammars

(assert-error "peg/compile error 1" (peg/compile nil))
(assert-error "peg/compile error 2" (peg/compile @{}))
(assert-error "peg/compile error 3" (peg/compile '{:a "abc" :b "def"}))
(assert-error "peg/compile error 4" (peg/compile '(blarg "abc")))
(assert-error "peg/compile error 5" (peg/compile '(1 2 3)))

# IP address

(def ip-address
  '{:d (range "09")
    :0-4 (range "04")
    :0-5 (range "05")
    :byte (+
            (* "25" :0-5)
            (* "2" :0-4 :d)
            (* "1" :d :d)
            (between 1 2 :d))
    :main (* :byte "." :byte "." :byte "." :byte)})

(check-match ip-address "10.240.250.250" true)
(check-match ip-address "0.0.0.0" true)
(check-match ip-address "1.2.3.4" true)
(check-match ip-address "256.2.3.4" false)
(check-match ip-address "256.2.3.2514" false)

# Substitution test with peg

(file/flush stderr)
(file/flush stdout)

(def grammar '(accumulate (any (+ (/ "dog" "purple panda") (<- 1)))))
(defn try-grammar [text]
  (assert (= (string/replace-all "dog" "purple panda" text) (0 (peg/match grammar text))) text))

(try-grammar "i have a dog called doug the dog. he is good.")
(try-grammar "i have a dog called doug the dog. he is a good boy.")
(try-grammar "i have a dog called doug the do")
(try-grammar "i have a dog called doug the dog")
(try-grammar "i have a dog called doug the dogg")
(try-grammar "i have a dog called doug the doggg")
(try-grammar "i have a dog called doug the dogggg")

# Peg CSV test

(def csv
  '{:field (+
            (* `"` (% (any (+ (<- (if-not `"` 1)) (* (constant `"`) `""`)))) `"`)
            (<- (any (if-not (set ",\n") 1))))
    :main (* :field (any (* "," :field)) (+ "\n" -1))})

(defn check-csv
  [str res]
  (check-deep csv str res))

(check-csv "1,2,3" @["1" "2" "3"])
(check-csv "1,\"2\",3" @["1" "2" "3"])
(check-csv ``1,"1""",3`` @["1" "1\"" "3"])

# Nested Captures

(def grmr '(capture (* (capture "a") (capture 1) (capture "c"))))
(check-deep grmr "abc" @["a" "b" "c" "abc"])
(check-deep grmr "acc" @["a" "c" "c" "acc"])

# Functions in grammar

(def grmr-triple ~(% (any (/ (<- 1) ,(fn [x] (string x x x))))))
(check-deep grmr-triple "abc" @["aaabbbccc"])
(check-deep grmr-triple "" @[""])
(check-deep grmr-triple " " @["   "])

(def counter ~(/ (group (any (<- 1))) ,length))
(check-deep counter "abcdefg" @[7])

# Capture Backtracking

(check-deep '(+ (* (capture "c") "d") "ce") "ce" @[])

# Matchtime capture

(def scanner (peg/compile ~(cmt (capture (some 1)) ,scan-number)))

(check-deep scanner "123" @[123])
(check-deep scanner "0x86" @[0x86])
(check-deep scanner "-1.3e-7" @[-1.3e-7])
(check-deep scanner "123A" nil)

# Recursive grammars

(def g '{:main (+ (* "a" :main "b") "c")})

(check-match g "c" true)
(check-match g "acb" true)
(check-match g "aacbb" true)
(check-match g "aadbb" false)

# Back reference

(def wrapped-string
  ~{:pad (any "=")
    :open (* "[" (<- :pad :n) "[")
    :close (* "]" (cmt (* (-> :n) (<- :pad)) ,=) "]")
    :main (* :open (any (if-not :close 1)) :close -1)})

(check-match wrapped-string "[[]]" true)
(check-match wrapped-string "[==[a]==]" true)
(check-match wrapped-string "[==[]===]" false)
(check-match wrapped-string "[[blark]]" true)
(check-match wrapped-string "[[bl[ark]]" true)
(check-match wrapped-string "[[bl]rk]]" true)
(check-match wrapped-string "[[bl]rk]] " false)
(check-match wrapped-string "[=[bl]]rk]=] " false)
(check-match wrapped-string "[=[bl]==]rk]=] " false)
(check-match wrapped-string "[===[]==]===]" true)

(def janet-longstring
  ~{:delim (some "`")
    :open (capture :delim :n)
    :close (cmt (* (not (> -1 "`")) (-> :n) (<- :delim)) ,=)
    :main (* :open (any (if-not :close 1)) :close -1)})

(check-match janet-longstring "`john" false)
(check-match janet-longstring "abc" false)
(check-match janet-longstring "` `" true)
(check-match janet-longstring "`  `" true)
(check-match janet-longstring "``  ``" true)
(check-match janet-longstring "``` `` ```" true)
(check-match janet-longstring "``  ```" false)

# Backmatch

(def backmatcher-1 '(* (capture (any "x") :1) "y" (backmatch :1) -1))

(check-match backmatcher-1 "y" true)
(check-match backmatcher-1 "xyx" true)
(check-match backmatcher-1 "xxxxxxxyxxxxxxx" true)
(check-match backmatcher-1 "xyxx" false)
(check-match backmatcher-1 "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxy" false)
(check-match backmatcher-1 (string (string/repeat "x" 10000) "y") false)
(check-match backmatcher-1 (string (string/repeat "x" 10000) "y" (string/repeat "x" 10000)) true)

(def backmatcher-2 '(* '(any "x") "y" (backmatch) -1))

(check-match backmatcher-2 "y" true)
(check-match backmatcher-2 "xyx" true)
(check-match backmatcher-2 "xxxxxxxyxxxxxxx" true)
(check-match backmatcher-2 "xyxx" false)
(check-match backmatcher-2 "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxy" false)
(check-match backmatcher-2 (string (string/repeat "x" 10000) "y") false)
(check-match backmatcher-2 (string (string/repeat "x" 10000) "y" (string/repeat "x" 10000)) true)

(def longstring-2 '(* '(some "`") (some (if-not (backmatch) 1)) (backmatch) -1))

(check-match longstring-2 "`john" false)
(check-match longstring-2 "abc" false)
(check-match longstring-2 "` `" true)
(check-match longstring-2 "`  `" true)
(check-match longstring-2 "``  ``" true)
(check-match longstring-2 "``` `` ```" true)
(check-match longstring-2 "``  ```" false)

# Optional

(check-match '(* (opt "hi") -1) "" true)
(check-match '(* (opt "hi") -1) "hi" true)
(check-match '(* (opt "hi") -1) "no" false)
(check-match '(* (? "hi") -1) "" true)
(check-match '(* (? "hi") -1) "hi" true)
(check-match '(* (? "hi") -1) "no" false)

# Drop

(check-deep '(drop '"hello") "hello" @[])
(check-deep '(drop "hello") "hello" @[])

# Regression #24

(def t (put @{} :hi 1))
(assert (deep= t @{:hi 1}) "regression #24")

# Peg swallowing errors
(assert (try (peg/match ~(/ '1 ,(fn [x] (nil x))) "x") ([err] err))
        "errors should not be swallowed")
(assert (try ((fn [x] (nil x))) ([err] err))
        "errors should not be swallowed 2")

# Tuple types

(assert (= (tuple/type '(1 2 3)) :parens) "normal tuple")
(assert (= (tuple/type [1 2 3]) :parens) "normal tuple 1")
(assert (= (tuple/type '[1 2 3]) :brackets) "bracketed tuple 2")
(assert (= (tuple/type (-> '(1 2 3) marshal unmarshal)) :parens) "normal tuple marshalled/unmarshalled")
(assert (= (tuple/type (-> '[1 2 3] marshal unmarshal)) :brackets) "normal tuple marshalled/unmarshalled")

# Check for bad memoization (+ :a) should mean different things in different contexts.
(def redef-a
  ~{:a "abc"
    :c (+ :a)
    :main (* :c {:a "def" :main (+ :a)} -1)})

(check-match redef-a "abcdef" true)
(check-match redef-a "abcabc" false)
(check-match redef-a "defdef" false)

(def redef-b
  ~{:pork {:pork "beef" :main (+ -1 (* 1 :pork))}
    :main :pork})

(check-match redef-b "abeef" true)
(check-match redef-b "aabeef" false)
(check-match redef-b "aaaaaa" false)

(end-suite)
