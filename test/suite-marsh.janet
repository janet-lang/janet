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

# Marshal

# 98f2c6f
(def um-lookup (env-lookup (fiber/getenv (fiber/current))))
(def m-lookup (invert um-lookup))

# 0cf10946b
(defn testmarsh [x msg]
  (def marshx (marshal x m-lookup))
  (def out (marshal (unmarshal marshx um-lookup) m-lookup))
  (assert (= (string marshx) (string out)) msg))

(testmarsh nil "marshal nil")
(testmarsh false "marshal false")
(testmarsh true "marshal true")
(testmarsh 1 "marshal small integers")
(testmarsh -1 "marshal integers (-1)")
(testmarsh 199 "marshal small integers (199)")
(testmarsh 5000 "marshal medium integers (5000)")
(testmarsh -5000 "marshal small integers (-5000)")
(testmarsh 10000 "marshal large integers (10000)")
(testmarsh -10000 "marshal large integers (-10000)")
(testmarsh 1.0 "marshal double")
(testmarsh "doctordolittle" "marshal string")
(testmarsh :chickenshwarma "marshal symbol")
(testmarsh @"oldmcdonald" "marshal buffer")
(testmarsh @[1 2 3 4 5] "marshal array")
(testmarsh [tuple 1 2 3 4 5] "marshal tuple")
(testmarsh @{1 2 3 4}  "marshal table")
(testmarsh {1 2 3 4}  "marshal struct")
(testmarsh (fn [x] x) "marshal function 0")
(testmarsh (fn name [x] x) "marshal function 1")
(testmarsh (fn [x] (+ 10 x 2)) "marshal function 2")
(testmarsh (fn thing [x] (+ 11 x x 30)) "marshal function 3")
(testmarsh map "marshal function 4")
(testmarsh reduce "marshal function 5")
(testmarsh (fiber/new (fn [] (yield 1) 2)) "marshal simple fiber 1")
(testmarsh (fiber/new (fn [&] (yield 1) 2)) "marshal simple fiber 2")

# issue #53 - 1147482e6
(def strct {:a @[nil]})
(put (strct :a) 0 strct)
(testmarsh strct "cyclic struct")

# More marshalling code
# issue #53 - 1147482e6
(defn check-image
  "Run a marshaling test using the make-image and load-image functions."
  [x msg]
  (def im (make-image x))
  # (printf "\nimage-hash: %d" (-> im string hash))
  (assert-no-error msg (load-image im)))

(check-image (fn [] (fn [] 1)) "marshal nested functions")
(check-image (fiber/new (fn [] (fn [] 1)))
             "marshal nested functions in fiber")
(check-image (fiber/new (fn [] (fiber/new (fn [] 1))))
             "marshal nested fibers")

# issue #53 - f4908ebc4
(def issue-53-x
  (fiber/new
    (fn []
      (var y (fiber/new (fn [] (print "1") (yield) (print "2")))))))

(check-image issue-53-x "issue 53 regression")

# Marshal closure over non resumable fiber
# issue #317 - 7c4ffe9b9
(do
  (defn f1
    [a]
    (defn f1 [] (++ (a 0)))
    (defn f2 [] (++ (a 0)))
    (error [f1 f2]))
  (def [_ tup] (protect (f1 @[0])))
  (def [f1 f2] (unmarshal (marshal tup make-image-dict) load-image-dict))
  (assert (= 1 (f1)) "marshal-non-resumable-closure 1")
  (assert (= 2 (f2)) "marshal-non-resumable-closure 2"))

# Marshal closure over currently alive fiber
# issue #317 - 7c4ffe9b9
(do
  (defn f1
    [a]
    (defn f1 [] (++ (a 0)))
    (defn f2 [] (++ (a 0)))
    (marshal [f1 f2] make-image-dict))
  (def [f1 f2] (unmarshal (f1 @[0]) load-image-dict))
  (assert (= 1 (f1)) "marshal-live-closure 1")
  (assert (= 2 (f2)) "marshal-live-closure 2"))

(do
  (var a 1)
  (defn b [x] (+ a x))
  (def c (unmarshal (marshal b)))
  (assert (= 2 (c 1)) "marshal-on-stack-closure 1"))

# Issue #336 cases - don't segfault
# b145d4786
(assert-error "unmarshal errors 1" (unmarshal @"\xd6\xb9\xb9"))
(assert-error "unmarshal errors 2" (unmarshal @"\xd7bc"))
# 5bbd50785
(assert-error "unmarshal errors 3"
              (unmarshal "\xd3\x01\xd9\x01\x62\xcf\x03\x78\x79\x7a"
                         load-image-dict))
# fcc610f53
(assert-error "unmarshal errors 4"
              (unmarshal
                @"\xD7\xCD\0e/p\x98\0\0\x03\x01\x01\x01\x02\0\0\x04\0\xCEe/p../tools
\0\0\0/afl\0\0\x01\0erate\xDE\xDE\xDE\xDE\xDE\xDE\xDE\xDE\xDE\xDE
\xA8\xDE\xDE\xDE\xDE\xDE\xDE\0\0\0\xDE\xDE_unmarshal_testcase3.ja
neldb\0\0\0\xD8\x05printG\x01\0\xDE\xDE\xDE'\x03\0marshal_tes/\x02
\0\0\0\0\0*\xFE\x01\04\x02\0\0'\x03\0\r\0\r\0\r\0\r" load-image-dict))
# XXX: still needed? see 72beeeea
(gccollect)

# ev/chan marshalling
(compwhen (dyn 'ev/chan)
  (def chan (ev/chan 10))
  (ev/give chan chan)
  (def newchan (unmarshal (marshal chan)))
  (def item (ev/take newchan))
  (assert (= item newchan) "ev/chan marshalling"))

# Issue #1488 - marshalling weak values
(testmarsh (array/weak 10) "marsh array/weak")
(testmarsh (table/weak-keys 10) "marsh table/weak-keys")
(testmarsh (table/weak-values 10) "marsh table/weak-values")
(testmarsh (table/weak 10) "marsh table/weak")

# Now check that gc works with weak containers after marshalling

# Turn off automatic GC for testing weak references
(gcsetinterval 0x7FFFFFFF)

# array
(def a (array/weak 1))
(array/push a @"")
(assert (= 1 (length a)) "array/weak marsh 1")
(def aclone (-> a marshal unmarshal))
(assert (= 1 (length aclone)) "array/weak marsh 2")
(gccollect)
(assert (= 1 (length aclone)) "array/weak marsh 3")
(assert (= 1 (length a)) "array/weak marsh 4")
(assert (= nil (get a 0)) "array/weak marsh 5")
(assert (= nil (get aclone 0)) "array/weak marsh 6")
(assert (deep= a aclone) "array/weak marsh 7")

# table weak keys and values
(def t (table/weak 1))
(def keep-key :key)
(def keep-value :value)
(put t :abc @"")
(put t :key :value)
(assert (= 2 (length t)) "table/weak marsh 1")
(def tclone (-> t marshal unmarshal))
(assert (= 2 (length tclone)) "table/weak marsh 2")
(gccollect)
(assert (= 1 (length tclone)) "table/weak marsh 3")
(assert (= 1 (length t)) "table/weak marsh 4")
(assert (= keep-value (get t keep-key)) "table/weak marsh 5")
(assert (= keep-value (get tclone keep-key)) "table/weak marsh 6")
(assert (deep= t tclone) "table/weak marsh 7")

# table weak keys
(def t (table/weak-keys 1))
(put t @"" keep-value)
(put t :key @"")
(assert (= 2 (length t)) "table/weak-keys marsh 1")
(def tclone (-> t marshal unmarshal))
(assert (= 2 (length tclone)) "table/weak-keys marsh 2")
(gccollect)
(assert (= 1 (length tclone)) "table/weak-keys marsh 3")
(assert (= 1 (length t)) "table/weak-keys marsh 4")
(assert (deep= t tclone) "table/weak-keys marsh 5")

# table weak values
(def t (table/weak-values 1))
(put t @"" keep-value)
(put t :key @"")
(assert (= 2 (length t)) "table/weak-values marsh 1")
(def tclone (-> t marshal unmarshal))
(assert (= 2 (length tclone)) "table/weak-values marsh 2")
(gccollect)
(assert (= 1 (length t)) "table/weak-value marsh 3")
(assert (deep= (freeze t) (freeze tclone)) "table/weak-values marsh 4")

# tables with prototypes
(def t (table/weak-values 1))
(table/setproto t @{:abc 123})
(put t @"" keep-value)
(put t :key @"")
(assert (= 2 (length t)) "marsh weak tables with prototypes 1")
(def tclone (-> t marshal unmarshal))
(assert (= 2 (length tclone)) "marsh weak tables with prototypes 2")
(gccollect)
(assert (= 1 (length t)) "marsh weak tables with prototypes 3")
(assert (deep= (freeze t) (freeze tclone)) "marsh weak tables with prototypes 4")
(assert (deep= (getproto t) (getproto tclone)) "marsh weak tables with prototypes 5")

(end-suite)
