# Copyright (c) 2023 Calvin Rose & contributors
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

# some tests for bigint
# 319575c
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
# 88db9751d
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

(assert (= (:/ (u64 "0xffff_ffff_ffff_ffff") 8 2) (u64 "0xfffffffffffffff"))
        "bigint operations 1")
(assert (let [a (u64 0xff)] (= (:+ a a a a) (:* a 2 2)))
        "bigint operations 2")

# 5ae520a2c
(assert (= (string (i64 -123)) "-123") "i64 prints reasonably")
(assert (= (string (u64 123)) "123") "u64 prints reasonably")

# 1db6d0e0b
(assert-error
  "trap INT64_MIN / -1"
  (:/ (int/s64 "-0x8000_0000_0000_0000") -1))

# int/s64 and int/u64 serialization
# 6aea7c7f7
(assert (deep= (int/to-bytes (u64 0)) @"\x00\x00\x00\x00\x00\x00\x00\x00"))

(assert (deep= (int/to-bytes (i64 1) :le)
               @"\x01\x00\x00\x00\x00\x00\x00\x00"))
(assert (deep= (int/to-bytes (i64 1) :be)
               @"\x00\x00\x00\x00\x00\x00\x00\x01"))
(assert (deep= (int/to-bytes (i64 -1))
               @"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"))
(assert (deep= (int/to-bytes (i64 -5) :be)
               @"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFB"))

(assert (deep= (int/to-bytes (u64 1) :le)
               @"\x01\x00\x00\x00\x00\x00\x00\x00"))
(assert (deep= (int/to-bytes (u64 1) :be)
               @"\x00\x00\x00\x00\x00\x00\x00\x01"))
(assert (deep= (int/to-bytes (u64 300) :be)
               @"\x00\x00\x00\x00\x00\x00\x01\x2C"))

# int/s64 int/u64 to existing buffer
# bbb3e16fd
(let [buf1 @""
      buf2 @"abcd"]
  (assert (deep= (int/to-bytes (i64 1) :le buf1)
                 @"\x01\x00\x00\x00\x00\x00\x00\x00"))
  (assert (deep= buf1 @"\x01\x00\x00\x00\x00\x00\x00\x00"))
  (assert (deep= (int/to-bytes (u64 300) :be buf2)
                 @"abcd\x00\x00\x00\x00\x00\x00\x01\x2C")))

# int/s64 and int/u64 paramater type checking
# 6aea7c7f7
(assert-error
  "bad value passed to int/to-bytes"
  (int/to-bytes 1))

# 6aea7c7f7
(assert-error
  "invalid endianness passed to int/to-bytes"
  (int/to-bytes (u64 0) :little))

# bbb3e16fd
(assert-error
  "invalid buffer passed to int/to-bytes"
  (int/to-bytes (u64 0) :little :buffer))

# Right hand operators
# 4fe005e3c
(assert (= (int/s64 (sum (range 10))) (sum (map int/s64 (range 10))))
        "right hand operators 1")
(assert (= (int/s64
             (product (range 1 10))) (product (map int/s64 (range 1 10))))
        "right hand operators 2")
(assert (= (int/s64 15) (bor 10 (int/s64 5)) (bor (int/s64 10) 5))
        "right hand operators 3")

# Integer type checks
# 11067d7a5
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

# Check for issue #1130
# 7e65c2bda
(var d (int/s64 7))
(mod 0 d)

(var d (int/s64 7))
(def result (seq [n :in (range -21 0)] (mod n d)))
(assert (deep= result
               (map int/s64 @[0 1 2 3 4 5 6 0 1 2 3 4 5 6 0 1 2 3 4 5 6]))
        "issue #1130")

# issue #272 - 81d301a42
(let [MAX_INT_64_STRING "9223372036854775807"
      MAX_UINT_64_STRING "18446744073709551615"
      MAX_INT_IN_DBL_STRING "9007199254740991"
      NAN (math/log -1)
      INF (/ 1 0)
      MINUS_INF (/ -1 0)
      compare-poly-tests
      [[(int/s64 3) (int/u64 3) 0]
       [(int/s64 -3) (int/u64 3) -1]
       [(int/s64 3) (int/u64 2) 1]
       [(int/s64 3) 3 0] [(int/s64 3) 4 -1] [(int/s64 3) -9 1]
       [(int/u64 3) 3 0] [(int/u64 3) 4 -1] [(int/u64 3) -9 1]
       [3 (int/s64 3) 0] [3 (int/s64 4) -1] [3 (int/s64 -5) 1]
       [3 (int/u64 3) 0] [3 (int/u64 4) -1] [3 (int/u64 2) 1]
       [(int/s64 MAX_INT_64_STRING) (int/u64 MAX_UINT_64_STRING) -1]
       [(int/s64 MAX_INT_IN_DBL_STRING)
        (scan-number MAX_INT_IN_DBL_STRING) 0]
       [(int/u64 MAX_INT_IN_DBL_STRING)
        (scan-number MAX_INT_IN_DBL_STRING) 0]
       [(+ 1 (int/u64 MAX_INT_IN_DBL_STRING))
        (scan-number MAX_INT_IN_DBL_STRING) 1]
       [(int/s64 0) INF -1] [(int/u64 0) INF -1]
       [MINUS_INF (int/u64 0) -1] [MINUS_INF (int/s64 0) -1]
       [(int/s64 1) NAN 0] [NAN (int/u64 1) 0]]]
  (each [x y c] compare-poly-tests
    (assert (= c (compare x y))
            (string/format "compare polymorphic %q %q %d" x y c))))

# marshal
(def m1 (u64 3141592654))
(def m2 (unmarshal (marshal m1)))
(assert (= m1 m2) "marshal/unmarshal")

# compare u64/u64
(assert (= (compare (u64 1) (u64 2)) -1) "compare 1")
(assert (= (compare (u64 1) (u64 1))  0) "compare 2")
(assert (= (compare (u64 2) (u64 1)) +1) "compare 3")

# compare i64/i64
(assert (= (compare (i64 -1) (i64 +1)) -1) "compare 4")
(assert (= (compare (i64 +1) (i64 +1))  0) "compare 5")
(assert (= (compare (i64 +1) (i64 -1)) +1) "compare 6")

# compare u64/i64
(assert (= (compare (u64 1) (i64 2)) -1) "compare 7")
(assert (= (compare (u64 1) (i64 -1)) +1) "compare 8")
(assert (= (compare (u64 -1) (i64 -1)) +1) "compare 9")

# compare i64/u64
(assert (= (compare (i64 1) (u64 2)) -1) "compare 10")
(assert (= (compare (i64 -1) (u64 1)) -1) "compare 11")
(assert (= (compare (i64 -1) (u64 -1)) -1) "compare 12")


(end-suite)

