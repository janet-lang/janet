# Copyright (c) 2026 Calvin Rose
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
(setdyn *lint-warn* :none)
(def issue-53-x
  (fiber/new
    (fn []
      (var y (fiber/new (fn [] (print "1") (yield) (print "2")))))))
(setdyn *lint-warn* nil)

(check-image issue-53-x "issue 53 regression")

# Marshal closure over non resumable fiber
# issue #317 - 7c4ffe9b9
(do
  (defn f1
    [a]
    (defn f1 :shadow [] (++ (a 0)))
    (defn f2 [] (++ (a 0)))
    (error [f1 f2]))
  (def [_ tup] (protect (f1 @[0])))
  (def [f1 f2] :shadow (unmarshal (marshal tup make-image-dict) load-image-dict))
  (assert (= 1 (f1)) "marshal-non-resumable-closure 1")
  (assert (= 2 (f2)) "marshal-non-resumable-closure 2"))

# Marshal closure over currently alive fiber
# issue #317 - 7c4ffe9b9
(do
  (defn f1
    [a]
    (defn f1 :shadow [] (++ (a 0)))
    (defn f2 :shadow [] (++ (a 0)))
    (marshal [f1 f2] make-image-dict))
  (def [f1 f2] :shadow (unmarshal (f1 @[0]) load-image-dict))
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
# Reject peg bytecode with a negative (argument n) index - the index is read as
# a signed int32 at match time and only bounds-checked on the high side, so a
# negative value would read before the argument array.
(do
  (def b (buffer (marshal (peg/compile ~(argument 0)))))
  (var idx nil)
  (for i 0 (- (length b) 2)
    (when (and (= (b i) 0x0f) (= (b (+ i 1)) 0x00) (= (b (+ i 2)) 0x00))
      (set idx i)))
  (assert idx "peg argument opcode not found")
  (def tampered
    (buffer/push (buffer (buffer/slice b 0 (+ idx 1)))
                 "\xCD\xFA\x0A\x1F\x00"
                 (buffer/slice b (+ idx 2))))
  (assert-error "unmarshal peg negative argument index"
                (peg/match (unmarshal tampered) "x" 0 :arg)))

# Reject a peg bytecode length that overflows the allocation size
(do
  (def b (buffer (marshal (peg/compile 1))))
  # byte 11 is the bytecode length; replace it with an encoded UINT64_MAX
  (assert (= (b 11) 2) "unexpected peg marshal layout")
  (def tampered
    (buffer/push (buffer (buffer/slice b 0 11))
                 "\xF8\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
                 (buffer/slice b 12)))
  (assert-error "unmarshal peg bytecode length overflow"
                (unmarshal tampered)))

# Reject a function whose captured-env count disagrees with its funcdef. The
# LB_FUNCTION record carries the number of env slots allocated for the closure,
# but the env count the GC and upvalue opcodes use comes from the funcdef. If the
# record claims fewer envs than the funcdef references, the envs[] array is
# under-allocated and marking/upvalue access reads past it.
(do
  (def clo ((fn [x] (fn [] x)) 42))
  (def b (buffer (marshal clo)))
  # byte 0 is LB_FUNCTION (0xD7), byte 1 is the env count (1 here)
  (assert (and (= (b 0) 0xD7) (= (b 1) 1)) "unexpected function marshal layout")
  (set (b 1) 0)
  (assert-error "unmarshal function env count mismatch"
                (do (def c (unmarshal b)) (gccollect) (c))))

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
(def t :shadow (table/weak-keys 1))
(put t @"" keep-value)
(put t :key @"")
(assert (= 2 (length t)) "table/weak-keys marsh 1")
(def tclone :shadow (-> t marshal unmarshal))
(assert (= 2 (length tclone)) "table/weak-keys marsh 2")
(gccollect)
(assert (= 1 (length tclone)) "table/weak-keys marsh 3")
(assert (= 1 (length t)) "table/weak-keys marsh 4")
(assert (deep= t tclone) "table/weak-keys marsh 5")

# table weak values
(def t :shadow (table/weak-values 1))
(put t @"" keep-value)
(put t :key @"")
(assert (= 2 (length t)) "table/weak-values marsh 1")
(def tclone :shadow (-> t marshal unmarshal))
(assert (= 2 (length tclone)) "table/weak-values marsh 2")
(gccollect)
(assert (= 1 (length t)) "table/weak-value marsh 3")
(assert (deep= (freeze t) (freeze tclone)) "table/weak-values marsh 4")

# tables with prototypes
(def t :shadow (table/weak-values 1))
(table/setproto t @{:abc 123})
(put t @"" keep-value)
(put t :key @"")
(assert (= 2 (length t)) "marsh weak tables with prototypes 1")
(def tclone :shadow (-> t marshal unmarshal))
(assert (= 2 (length tclone)) "marsh weak tables with prototypes 2")
(gccollect)
(assert (= 1 (length t)) "marsh weak tables with prototypes 3")
(assert (deep= (freeze t) (freeze tclone)) "marsh weak tables with prototypes 4")
(assert (deep= (getproto t) (getproto tclone)) "marsh weak tables with prototypes 5")

# Reject an unmarshalled fiber that has no stack frames but a resumable status.
# A finished fiber marshals with frame == 0; forcing its status to :pending
# leaves the VM running with frame == 0, reading the stack frame header before
# fiber->data.
(do
  (def dead (fiber/new (fn [] 1)))
  (resume dead)
  (def b (buffer (marshal dead)))
  # byte 0 is LB_FIBER, byte 1 is the fiber flags (status :dead here), byte 2
  # is the frame (0 for a finished fiber)
  (assert (and (= (b 0) 0xcc) (= (b 1) 8) (= (b 2) 0)) "unexpected fiber marshal layout")
  # splice in flags encoding status :pending (3 << 16) with the same low bits
  (def tampered
    (buffer/push (buffer (buffer/slice b 0 1))
                 "\xCD\x00\x03\x00\x08"
                 (buffer/slice b 2)))
  (assert-error "unmarshal frameless resumable fiber"
                (resume (unmarshal tampered))))

# A malformed channel image must be rejected before the channel is allocated.
# Previously the abstract was allocated first, so a panic while reading the
# header left an uninitialized channel in the heap and the next collection ran
# its finalizer on garbage queue pointers and a garbage mutex.
(do
  (def b (buffer (marshal (ev/chan 2))))
  # the image ends with is_threaded, closed, limit and count, one byte each
  (def n (length b))
  (assert (and (= (b (- n 2)) 2) (= (b (- n 1)) 0)) "unexpected channel marshal layout")
  # claim more queued items than the limit allows
  (put b (- n 1) 3)
  (assert-error "unmarshal channel count over limit" (unmarshal b))
  # cut the image off right after the is_threaded byte
  (assert-error "unmarshal truncated channel" (unmarshal (buffer/slice b 0 (- n 3))))
  (gccollect))

(end-suite)
