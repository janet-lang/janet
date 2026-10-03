# Copyright (c) 2026 Calvin Rose & contributors
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

# New parser
(assert (= 123:u (int/u64 "123")) "int/u64 parsing 1")
(assert (= 0:u (int/u64 "0")) "int/u64 parsing 2")
(assert (= 0xFFFF_FFFF_FFFF_FFFF:u (int/u64 "0xFFFF_FFFF_FFFF_FFFF")) "int/u64 parsing 3")

(assert (= 123:s (int/s64 "123")) "int/s64 parsing 1")
(assert (= -123:s (int/s64 "-123")) "int/s64 parsing 2")
(assert (= 0:s (int/s64 "0")) "int/s64 parsing 3")

# some tests for inttypes
# 319575c
(assert-no-error
  "int/u64 creation"
  (do
    # from number
    (def _a 10:u)
    (def _b 0x1f_ffff_ffff_ffff:u)
    # max double we can convert to int (2^53)
    (def _c (int/u64 (math/pow 2 53)))
    # from string
    (def _d (int/u64 "0xffff_ffff_ffff_ffff"))
    (def _e (int/u64 "32rvv_vv_vv_vv"))
    (def _f (int/u64 "123456789"))))

# Conversion back to an int32
# 88db9751d
(assert (= (int/to-number 0xFaFa:u) 0xFaFa) "int/to-number 1")
(assert (= (int/to-number 0xFaFa:s) 0xFaFa) "int/to-number 2")
(assert (= (int/to-number 9007199254740991:u) 9007199254740991) "int/to-number 3")
(assert (= (int/to-number 9007199254740991:s) 9007199254740991) "int/to-number 4")
(assert (= (int/to-number -9007199254740991:s) -9007199254740991) "int/to-number 5")

(assert-error
  "int/u64 out of bounds for safe integer"
  (int/to-number 9007199254740993:u))

(assert-error
  "int/s64 out of bounds for safe integer"
  (int/to-number -9007199254740993:s))

(assert-error
  "int/to-number fails on non-abstract types"
  (int/to-number 1))

(assert-no-error
  "int/s64 creation"
  (do
    # from number
    (def _a -10:s)
    (def _b 0x1f_ffff_ffff_ffff:s)
    # max double we can convert to int (2^53)
    (def _c (int/s64 (math/pow 2 53)))
    # from string
    (def _d (int/s64 "0x7fff_ffff_ffff_ffff"))
    (def _e (int/s64 "123456789"))))

(assert-error
  "bad initializers"
  (do
    # double too big (> 2^53) to be converted to uint64 without truncation
    (def _a (int/u64 (+ 0xffff_ffff_ffff_ff 1)))
    (def _b (int/u64 (+ (math/pow 2 53) 1)))
    # out of range 65 bits
    (def _c (int/u64 "0x1ffffffffffffffff"))
    # just to big
    (def _d (int/u64 "123456789123456789123456789"))))

(assert (= (:/ 0xffff_ffff_ffff_ffff:u 8 2) 0xfff_ffff_ffff_ffff:u)
        "inttype operations 1")
(assert (let [a 0xff:u] (= (:+ a a a a) (:* a 2 2)))
        "inttype operations 2")

# 5ae520a2c
(assert (= (string -123:s) "-123") "int/s64 prints reasonably")
(assert (= (string 123:u) "123") "int/u64 prints reasonably")

# 1db6d0e0b
(assert-error
  "trap INT64_MIN / -1"
  (:/ -0x8000_0000_0000_0000:s -1))

# int/s64 and int/u64 serialization
# 6aea7c7f7
(assert (deep= (int/to-bytes 0:u) @"\x00\x00\x00\x00\x00\x00\x00\x00")
        "int/to-bytes 1")
(assert (deep= (int/to-bytes 1:s :le) @"\x01\x00\x00\x00\x00\x00\x00\x00")
        "int/to-bytes 2")
(assert (deep= (int/to-bytes 1:s :be) @"\x00\x00\x00\x00\x00\x00\x00\x01")
        "int/to-bytes 3")
(assert (deep= (int/to-bytes -1:s) @"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF")
        "int/to-bytes 4")
(assert (deep= (int/to-bytes -5:s :be) @"\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFB")
        "int/to-bytes 5")
(assert (deep= (int/to-bytes 1:u :le) @"\x01\x00\x00\x00\x00\x00\x00\x00")
        "int/to-bytes 6")
(assert (deep= (int/to-bytes 1:u :be) @"\x00\x00\x00\x00\x00\x00\x00\x01")
        "int/to-bytes 7")
(assert (deep= (int/to-bytes 300:u :be) @"\x00\x00\x00\x00\x00\x00\x01\x2C")
        "int/to-bytes 8")

# int/s64 int/u64 to existing buffer
# bbb3e16fd
(let [buf1 @""
      buf2 @"abcd"]
  (assert (deep= (int/to-bytes 1:s :le buf1) @"\x01\x00\x00\x00\x00\x00\x00\x00")
          "int/to-bytes to buf1")
  (assert (deep= buf1 @"\x01\x00\x00\x00\x00\x00\x00\x00")
          "buf1 result")

  (assert (deep= (int/to-bytes 300:u :be buf2) @"abcd\x00\x00\x00\x00\x00\x00\x01\x2C")
          "int/to-bytes to buf2")
  (assert (deep= buf2 @"abcd\x00\x00\x00\x00\x00\x00\x01\x2C")
          "buf2 result"))

# int/s64 and int/u64 parameter type checking
# 6aea7c7f7
(assert-error
  "bad value passed to int/to-bytes"
  (int/to-bytes 1))

# 6aea7c7f7
(assert-error
  "invalid endianness passed to int/to-bytes"
  (int/to-bytes 0:u :little))

# bbb3e16fd
(assert-error
  "invalid buffer passed to int/to-bytes"
  (int/to-bytes 0:u :le :buffer))

# Right hand operators
# 4fe005e3c
(assert (= (int/s64 (sum (range 10))) (sum (map int/s64 (range 10))))
        "right hand operators 1")
(assert (= (int/s64 (product (range 1 10))) (product (map int/s64 (range 1 10))))
        "right hand operators 2")
(assert (= 15:s (bor 10 5:s) (bor 10:s 5))
        "right hand operators 3")

# Integer type checks
# 11067d7a5
(assert (compare= 0 (- 1000:u 1000)) "subtract from int/u64")

(assert (odd? 1001:u) "odd? 1")
(assert (not (odd? 1000:u)) "odd? 2")
(assert (odd? 1001:s) "odd? 3")
(assert (not (odd? 1000:s)) "odd? 4")
(assert (odd? -1001:s) "odd? 5")
(assert (not (odd? -1000:s)) "odd? 6")

(assert (even? 1000:u) "even? 1")
(assert (not (even? 1001:u)) "even? 2")
(assert (even? 1000:s) "even? 3")
(assert (not (even? 1001:s)) "even? 4")
(assert (even? -1000:s) "even? 5")
(assert (not (even? -1001:s)) "even? 6")

# integer type operations
(defn opcheck [int x y]
  (each op [mod % div]
    (assert (compare= (op x y) (op (int x) y))
            (string int " (" op " " x " " y ") expected " (op x y)
                    ", got " (op (int x) y)))
    (assert (compare= (op x y) (op x (int y)))
            (string int " (" op " " x " " y ") expected " (op x y)
                    ", got " (op x (int y))))
    (assert (compare= (op x y) (op (int x) (int y)))
            (string int " (" op " " x " " y ") expected " (op x y)
                    ", got " (op (int x) (int y))))))

(loop [x :in [-5 -3 0 3 5]
       y :in [-4 -3 3 4]]
  (opcheck int/s64 x y)
  (if (and (>= x 0) (>= y 0))
    (opcheck int/u64 x y)))

(each int [int/s64 int/u64]
  (each op [% / div]
    (assert-error "division by zero" (op (int 7) 0))
    (assert-error "division by zero" (op 7 (int 0)))
    (assert-error "division by zero" (op (int 7) (int 0)))))

(each int [int/s64 int/u64]
  (loop [x :in [-5 -3 0 3 5] :when (or (pos? x) (= int int/s64))]
    # skip check when comparing negative values with unsigned integers.
    (assert (= (int x) (mod (int x) 0)) (string int " mod 0"))
    (assert (= (int x) (mod x (int 0))) (string int " mod 0"))
    (assert (= (int x) (mod (int x) (int 0))) (string int " mod 0"))))

(loop [x :in [-5 -3 0 3 5]]
  (assert (compare= (bnot x) (bnot (int/s64 x))) "int/s64 bnot"))

(loop [x :range [0 10]]
  (assert (= 0xFFFF_FFFF_FFFF_FFFF:u
          (bxor (int/u64 x) (bnot (int/u64 x))))
          "int/u64 bnot"))

# Check for issue #1130
# 7e65c2bda
(var d 7:s)
(mod 0 d)

(var d 7:s)
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
      [[3:s 3:u 0]
       [-3:s 3:u -1]
       [3:s 2:u 1]
       [3:s 3 0] [3:s 4 -1] [3:s -9 1]
       [3:u 3 0] [3:u 4 -1] [3:u -9 1]
       [3 3:s 0] [3 4:s -1] [3 -5:s 1]
       [3 3:u 0] [3 4:u -1] [3 2:u 1]
       [(int/s64 MAX_INT_64_STRING) (int/u64 MAX_UINT_64_STRING) -1]
       [(int/s64 MAX_INT_IN_DBL_STRING)
        (scan-number MAX_INT_IN_DBL_STRING) 0]
       [(int/u64 MAX_INT_IN_DBL_STRING)
        (scan-number MAX_INT_IN_DBL_STRING) 0]
       [(+ 1 (int/u64 MAX_INT_IN_DBL_STRING))
        (scan-number MAX_INT_IN_DBL_STRING) 1]
       [0:s INF -1] [0:u INF -1]
       [MINUS_INF 0:u -1] [MINUS_INF 0:s -1]
       [1:s NAN 0] [NAN 1:u 0]]]
  (each [x y c] compare-poly-tests
    (assert (= c (compare x y))
            (string/format "compare polymorphic %q %q %d" x y c))))

# marshal
(def m1 3141592654:u)
(def m2 (unmarshal (marshal m1)))
(assert (= m1 m2) "marshal/unmarshal")

# compare int/u64 int/u64
(assert (= (compare 1:u 2:u) -1) "compare 1")
(assert (= (compare 1:u 1:u)  0) "compare 2")
(assert (= (compare 2:u 1:u) +1) "compare 3")

# compare int/s64 int/s64
(assert (= (compare -1:s +1:s) -1) "compare 4")
(assert (= (compare +1:s +1:s)  0) "compare 5")
(assert (= (compare +1:s -1:s) +1) "compare 6")

# compare int/u64 int/s64
(assert (= (compare 1:u 2:s) -1) "compare 7")
(assert (= (compare 1:u -1:s) +1) "compare 8")
(assert (= (compare 0:u -1:s) +1) "compare 9")

# compare int/s64 int/u64
(assert (= (compare 1:s 2:u) -1) "compare 10")
(assert (= (compare -1:s 1:u) -1) "compare 11")
(assert (= (compare -1:s 0:u) -1) "compare 12")

# off by 1 error in inttypes
# a3e812b86
(assert (= -0x8000_0000_0000_0000:s
           (+ 0x7FFF_FFFF_FFFF_FFFF:s 1)) "int types wrap around")
(assert (= 0x7FFF_FFFF_FFFF_FFFF:s
           (- -0x8000_0000_0000_0000:s 1)) "int types wrap around")

# Issue #1217
(assert (= (- 0xFFFF_FFFF:u 1) 0xFFFF_FFFE:u) "int/u64 subtract")

(end-suite)
