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

# Denormal tables
# 38a7e4faf
(assert (= (length @{1 2 nil 3}) 1) "nil key table literal")
(assert (= (length (table 1 2 nil 3)) 1) "nil key table ctor")

(assert (= (length (table (/ 0 0) 2 1 3)) 1) "nan key table ctor")
(assert (= (length @{1 2 (/ 0 0) 3}) 1) "nan key table literal")

(assert (= (length (table 2 1 3 nil)) 1) "nil value table ctor")
(assert (= (length @{1 2 3 nil}) 1) "nil value table literal")

# Table duplicate elements
(assert (deep= @{:a 3 :b 2} @{:a 1 :b 2 :a 3}) "table literal duplicate keys")
(assert (deep= @{:a 3 :b 2} (table :a 1 :b 2 :a 3))
        "table constructor duplicate keys")

## Table prototypes
# 027b2a81c
(def roottab @{
 :parentprop 123
})

(def childtab @{
 :childprop 456
})

(table/setproto childtab roottab)

(assert (= 123 (get roottab :parentprop)) "table get 1")
(assert (= 123 (get childtab :parentprop)) "table get proto")
(assert (= nil (get roottab :childprop)) "table get 2")
(assert (= 456 (get childtab :childprop)) "proto no effect")

# b3aed1356
(assert-error
  "table rawget regression"
  (table/new -1))

# table/clone
# 392813667
(defn check-table-clone [x msg]
  (assert (= (table/to-struct x) (table/to-struct (table/clone x))) msg))

(check-table-clone @{:a 123 :b 34 :c :hello : 945 0 1 2 3 4 5}
                   "table/clone 1")
(check-table-clone @{} "table/clone 2")

(end-suite)

