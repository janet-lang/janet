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

# Scan number
# 798c88b4c
(assert (= 1 (scan-number "1")) "scan-number 1")
(assert (= -1 (scan-number "-1")) "scan-number -1")
(assert (= 1.3e4 (scan-number "1.3e4")) "scan-number 1.3e4")

# Issue #183 - just parse it :)
# 688d297a1
1e-4000000000000000000000

# For undefined behavior sanitizer
# c876e63
0xf&1fffFFFF

# off by 1 error in inttypes
# a3e812b86
(assert (= (int/s64 "-0x8000_0000_0000_0000")
           (+ (int/s64 "0x7FFF_FFFF_FFFF_FFFF") 1)) "int types wrap around")

(end-suite)

