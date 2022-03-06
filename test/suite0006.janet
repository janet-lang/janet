# Copyright (c) 2021 Calvin Rose & contributors
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
(start-suite 6)

# some tests for bigint

(def i64 int/s64)
(def u64 int/u64)

(assert-no-error
 "create some uint64 bigints"
 (do
   # from number
   (def a (u64 10))
   # max double we can convert to int (2^53)
   (def b (u64 0x1fffffffffffff))
   (def b (u64 (math/pow 2 53)))
   # from string
   (def c (u64 "0xffff_ffff_ffff_ffff"))
   (def c (u64 "32rvv_vv_vv_vv"))
   (def d (u64 "123456789"))))

# Conversion back to an int32
(assert (= (int/to-number (u64 0xFaFa)) 0xFaFa))
(assert (= (int/to-number (i64 0xFaFa)) 0xFaFa))
(assert (= (int/to-number (u64 9007199254740991)) 9007199254740991))
(assert (= (int/to-number (i64 9007199254740991)) 9007199254740991))
(assert (= (int/to-number (i64 -9007199254740991)) -9007199254740991))

(assert-error
  "u64 out of bounds for safe integer"
  (int/to-number (u64 "9007199254740993"))

(assert-error
  "s64 out of bounds for safe integer"
  (int/to-number (i64 "-9007199254740993"))))

(assert-error
  "int/to-number fails on non-abstract types"
  (int/to-number 1))

(assert-no-error
 "create some int64 bigints"
 (do
   # from number
   (def a (i64 -10))
   # max double we can convert to int (2^53)
   (def b (i64 0x1fffffffffffff))
   (def b (i64 (math/pow 2 53)))
   # from string
   (def c (i64 "0x7fff_ffff_ffff_ffff"))
   (def d (i64 "123456789"))))

(assert-error
 "bad initializers"
 (do
   # double to big to be converted to uint64 without truncation (2^53 + 1)
   (def b (u64 (+ 0xffff_ffff_ffff_ff 1)))
   (def b (u64 (+ (math/pow 2 53) 1)))
   # out of range 65 bits
   (def c (u64 "0x1ffffffffffffffff"))
   # just to big
   (def d (u64 "123456789123456789123456789"))))

(assert (= (:/ (u64 "0xffff_ffff_ffff_ffff") 8 2) (u64 "0xfffffffffffffff")) "bigint operations 1")
(assert (let [a (u64 0xff)] (= (:+ a a a a) (:* a 2 2))) "bigint operations 2")

(assert (= (string (i64 -123)) "-123") "i64 prints reasonably")
(assert (= (string (u64 123)) "123") "u64 prints reasonably")

(assert-error
 "trap INT64_MIN / -1"
 (:/ (int/s64 "-0x8000_0000_0000_0000") -1))

# int/s64 and int/u64 serialization
(assert (deep= (int/to-bytes (u64 0)) @"\x00\x00\x00\x00\x00\x00\x00\x00"))

(assert (deep= (int/to-bytes (i64 1) :le) @"\x01\x00\x00\x00\x00\x00\x00\x00"))
(assert (deep= (int/to-bytes (i64 1) :be) @"\x00\x00\x00\x00\x00\x00\x00\x01"))
(assert (deep= (int/to-bytes (i64 -1)) @"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"))
(assert (deep= (int/to-bytes (i64 -5) :be) @"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFB"))

(assert (deep= (int/to-bytes (u64 1) :le) @"\x01\x00\x00\x00\x00\x00\x00\x00"))
(assert (deep= (int/to-bytes (u64 1) :be) @"\x00\x00\x00\x00\x00\x00\x00\x01"))
(assert (deep= (int/to-bytes (u64 300) :be) @"\x00\x00\x00\x00\x00\x00\x01\x2C"))

# int/s64 int/u64 to existing buffer
(let [buf1 @""
      buf2 @"abcd"]
  (assert (deep= (int/to-bytes (i64 1) :le buf1) @"\x01\x00\x00\x00\x00\x00\x00\x00"))
  (assert (deep= buf1 @"\x01\x00\x00\x00\x00\x00\x00\x00"))
  (assert (deep= (int/to-bytes (u64 300) :be buf2) @"abcd\x00\x00\x00\x00\x00\x00\x01\x2C")))

# int/s64 and int/u64 paramater type checking
(assert-error
 "bad value passed to int/to-bytes"
 (int/to-bytes 1))

(assert-error
  "invalid endianness passed to int/to-bytes"
   (int/to-bytes (u64 0) :little))

(assert-error
  "invalid buffer passed to int/to-bytes"
   (int/to-bytes (u64 0) :little :buffer))


# Dynamic bindings
(setdyn :a 10)
(assert (= 40 (with-dyns [:a 25 :b 15] (+ (dyn :a) (dyn :b)))) "dyn usage 1")
(assert (= 10 (dyn :a)) "dyn usage 2")
(assert (= nil (dyn :b)) "dyn usage 3")
(setdyn :a 100)
(assert (= 100 (dyn :a)) "dyn usage 4")

# Keyword arguments
(defn myfn [x y z &keys {:a a :b b :c c}]
  (+ x y z a b c))

(assert (= (+ ;(range 6)) (myfn 0 1 2 :a 3 :b 4 :c 5)) "keyword args 1")
(assert (= (+ ;(range 6)) (myfn 0 1 2 :a 1 :b 6 :c 5 :d 11)) "keyword args 2")

# Comment macro
(comment 1)
(comment 1 2)
(comment 1 2 3)
(comment 1 2 3 4)

# Parser clone
(def p (parser/new))
(assert (= 7 (parser/consume p "(1 2 3 ")) "parser 1")
(def p2 (parser/clone p))
(parser/consume p2 ") 1 ")
(parser/consume p ") 1 ")
(assert (deep= (parser/status p) (parser/status p2)) "parser 2")
(assert (deep= (parser/state p) (parser/state p2)) "parser 3")

# Parser errors
(defn parse-error [input]
  (def p (parser/new))
  (parser/consume p input)
  (parser/error p))

# Invalid utf-8 sequences
(assert (not= nil (parse-error @"\xc3\x28")) "reject invalid utf-8 symbol")
(assert (not= nil (parse-error @":\xc3\x28")) "reject invalid utf-8 keyword")

# Parser line and column numbers
(defn parser-location [input &opt location]
  (def p (parser/new))
  (parser/consume p input)
  (if location
    (parser/where p ;location)
    (parser/where p)))

(assert (= [1 7] (parser-location @"(+ 1 2)")) "parser location 1")
(assert (= [5 7] (parser-location @"(+ 1 2)" [5])) "parser location 2")
(assert (= [10 10] (parser-location @"(+ 1 2)" [10 10])) "parser location 3")

# String check-set
(assert (string/check-set "abc" "a") "string/check-set 1")
(assert (not (string/check-set "abc" "z")) "string/check-set 2")
(assert (string/check-set "abc" "abc") "string/check-set 3")
(assert (string/check-set "abc" "") "string/check-set 4")
(assert (not (string/check-set "" "aabc")) "string/check-set 5")
(assert (not (string/check-set "abc" "abcdefg")) "string/check-set 6")

# Marshal and unmarshal pegs
(def p (-> "abcd" peg/compile marshal unmarshal))
(assert (peg/match p "abcd") "peg marshal 1")
(assert (peg/match p "abcdefg") "peg marshal 2")
(assert (not (peg/match p "zabcdefg")) "peg marshal 3")

# This should be valgrind clean.
(var pegi 3)
(defn marshpeg [p]
  (assert (-> p peg/compile marshal unmarshal) (string "peg marshal " (++ pegi))))
(marshpeg '(* 1 2 (set "abcd") "asdasd" (+ "." 3)))
(marshpeg '(% (* (+ 1 2 3) (* "drop" "bear") '"hi")))
(marshpeg '(> 123 "abcd"))
(marshpeg '{:main (* 1 "hello" :main)})
(marshpeg '(range "AZ"))
(marshpeg '(if-not "abcdf" 123))
(marshpeg '(error ($)))
(marshpeg '(* "abcd" (constant :hi)))
(marshpeg ~(/ "abc" ,identity))
(marshpeg '(if-not "abcdf" 123))
(marshpeg ~(cmt "abcdf" ,identity))
(marshpeg '(group "abc"))

# Module path expansion
(setdyn :current-file "some-dir/some-file")
(defn test-expand [path temp]
  (string (module/expand-path path temp)))

# Right hand operators
(assert (= (int/s64 (sum (range 10))) (sum (map int/s64 (range 10)))) "right hand operators 1")
(assert (= (int/s64 (product (range 1 10))) (product (map int/s64 (range 1 10)))) "right hand operators 2")
(assert (= (int/s64 15) (bor 10 (int/s64 5)) (bor (int/s64 10) 5)) "right hand operators 3")

(assert (= (test-expand "abc" ":cur:/:all:") "some-dir/abc") "module/expand-path 1")
(assert (= (test-expand "./abc" ":cur:/:all:") "some-dir/abc") "module/expand-path 2")
(assert (= (test-expand "abc/def.txt" ":cur:/:name:") "some-dir/def.txt") "module/expand-path 3")
(assert (= (test-expand "abc/def.txt" ":cur:/:dir:/sub/:name:") "some-dir/abc/sub/def.txt") "module/expand-path 4")
(assert (= (test-expand "/abc/../def.txt" ":all:") "/def.txt") "module/expand-path 5")
(assert (= (test-expand "abc/../def.txt" ":all:") "def.txt") "module/expand-path 6")
(assert (= (test-expand "../def.txt" ":all:") "../def.txt") "module/expand-path 7")
(assert (= (test-expand "../././././abcd/../def.txt" ":all:") "../def.txt") "module/expand-path 8")

# Integer type checks
(assert (compare= 0 (- (int/u64 "1000") 1000)) "subtract from int/u64")

(assert (odd? (int/u64 "1001")) "odd? 1")
(assert (not (odd? (int/u64 "1000"))) "odd? 2")
(assert (odd? (int/s64 "1001")) "odd? 3")
(assert (not (odd? (int/s64 "1000"))) "odd? 4")
(assert (odd? (int/s64 "-1001")) "odd? 5")
(assert (not (odd? (int/s64 "-1000"))) "odd? 6")

(assert (even? (int/u64 "1000")) "even? 1")
(assert (not (even? (int/u64 "1001"))) "even? 2")
(assert (even? (int/s64 "1000")) "even? 3")
(assert (not (even? (int/s64 "1001"))) "even? 4")
(assert (even? (int/s64 "-1000")) "even? 5")
(assert (not (even? (int/s64 "-1001"))) "even? 6")

# integer type operations
(defn modcheck [x y]
  (assert (= (string (mod x y)) (string (mod (int/s64 x) y)))
          (string "int/s64 (mod " x " " y ") expected " (mod x y) ", got "
                  (mod (int/s64 x) y)))
  (assert (= (string (% x y)) (string (% (int/s64 x) y)))
          (string "int/s64 (% " x " " y ") expected " (% x y) ", got "
                  (% (int/s64 x) y))))

(modcheck 1 2)
(modcheck 1 3)
(modcheck 4 2)
(modcheck 4 1)
(modcheck 10 3)
(modcheck 10 -3)
(modcheck -10 3)
(modcheck -10 -3)

(end-suite)
