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

# 21bd960
(assert (= (struct 1 2 3 4 5 6 7 8) (struct 7 8 5 6 3 4 1 2))
        "struct order does not matter 1")
# 42a88de
(assert (= (struct
             :apple 1
             6 :bork
             '(1 2 3) 5)
           (struct
             6 :bork
             '(1 2 3) 5
             :apple 1)) "struct order does not matter 2")

# Denormal structs
# 38a7e4faf
(assert (= (length {1 2 nil 3}) 1) "nil key struct literal")
(assert (= (length (struct 1 2 nil 3)) 1) "nil key struct ctor")

(assert (= (length (struct (/ 0 0) 2 1 3)) 1) "nan key struct ctor")
(assert (= (length {1 2 (/ 0 0) 3}) 1) "nan key struct literal")

(assert (= (length (struct 2 1 3 nil)) 1) "nil value struct ctor")
(assert (= (length {1 2 3 nil}) 1) "nil value struct literal")

# Struct duplicate elements
# 8bc2987a7
(assert (= {:a 3 :b 2} {:a 1 :b 2 :a 3}) "struct literal duplicate keys")
(assert (= {:a 3 :b 2} (struct :a 1 :b 2 :a 3))
        "struct constructor duplicate keys")

# Struct prototypes
# 4d983e5
(def x (struct/with-proto {1 2 3 4} 5 6))
(def y (-> x marshal unmarshal))
(def z {1 2 3 4})
(assert (= 2 (get x 1)) "struct get proto value 1")
(assert (= 4 (get x 3)) "struct get proto value 2")
(assert (= 6 (get x 5)) "struct get proto value 3")
(assert (= x y) "struct proto marshal equality 1")
(assert (= (getproto x) (getproto y)) "struct proto marshal equality 2")
(assert (= 0 (cmp x y)) "struct proto comparison 1")
(assert (= 0 (cmp (getproto x) (getproto y))) "struct proto comparison 2")
(assert (not= (cmp x z) 0) "struct proto comparison 3")
(assert (not= (cmp y z) 0) "struct proto comparison 4")
(assert (not= x z) "struct proto comparison 5")
(assert (not= y z) "struct proto comparison 6")
(assert (= (x 5) 6) "struct proto get 1")
(assert (= (y 5) 6) "struct proto get 1")
(assert (deep= x y) "struct proto deep= 1")
(assert (deep-not= x z) "struct proto deep= 2")
(assert (deep-not= y z) "struct proto deep= 3")

# Check missing struct proto bug
# 868ec1a7e, e08394c8
(assert (struct/getproto (struct/with-proto {:a 1} :b 2 :c nil))
        "missing struct proto")

(end-suite)

