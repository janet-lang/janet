# Copyright (c) 2019 Calvin Rose & contributors
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

(import test/helper :prefix "" :exit true)
(start-suite 6)

# some tests for bigint 

(assert-no-error
 "create some uint64 bigints"
 (do
   # from number
   (def a (bigint/uint64 10))
   # max double we can convert to int (2^53)
   (def b (bigint/uint64 0x1fffffffffffff))
   (def b (bigint/uint64 (math/pow 2 53)))
   # from string 
   (def c (bigint/uint64 "0xffffffffffffffff"))
   (def d (bigint/uint64 "123456789"))))

(assert-no-error
 "create some int64 bigints"
 (do
   # from number
   (def a (bigint/int64 -10))
   # max double we can convert to int (2^53)
   (def b (bigint/int64 0x1fffffffffffff))
   (def b (bigint/int64 (math/pow 2 53)))
   # from string 
   (def c (bigint/int64 "0x7fffffffffffffff"))
   (def d (bigint/int64 "123456789"))))

(assert-error
 "bad initializers"
 (do
   # double to big to be converted to uint64 without truncation (2^53 + 1)
   (def b (bigint/uint64 (+ 0xffff_ffff_ffff_ff 1)))
   (def b (bigint/uint64 (+ (math/pow 2 53) 1)))
   # out of range 65 bits
   (def c (bigint/uint64 "0x1ffffffffffffffff"))
   # just to big     
   (def d (bigint/uint64 "123456789123456789123456789"))))

(assert (:== (:/ (bigint/uint64 "0xffffffffffffffff") 8 2) "0xfffffffffffffff") "bigint operations")
(assert (let [a (bigint/uint64 0xff)] (:== (:+ a a a a) (:* a 2 2))) "bigint operations")
        


(end-suite)
