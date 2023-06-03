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

# Assembly test
# Fibonacci sequence, implemented with naive recursion.
# a679f60
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

# dacbe29
(def f (asm (disasm (fn [x] (fn [y] (+ x y))))))
(assert (= ((f 10) 37) 47) "asm environment tables")

(end-suite)

