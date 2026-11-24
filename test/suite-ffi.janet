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

(var has-ffi (dyn 'ffi/native))
(def has-full-ffi
  (and has-ffi
       (when-let [entry (dyn 'ffi/calling-conventions)]
         (def fficc (entry :value))
         (> (length (fficc)) 1)))) # all arches support :none

# FFI check
# d80356158
(compwhen has-ffi
  (ffi/context))

(compwhen has-ffi
  (ffi/defbind memcpy :ptr [dest :ptr src :ptr n :size]))
(compwhen has-full-ffi
  (def buffer1 @"aaaa")
  (def buffer2 @"bbbb")
  (memcpy buffer1 buffer2 4)
  (assert (= (string buffer1) "bbbb") "ffi 1 - memcpy"))

# cfaae47ce
(compwhen has-ffi
  (assert (= 8 (ffi/size [:int :char])) "size unpacked struct 1")
  (assert (= 5 (ffi/size [:pack :int :char])) "size packed struct 1")
  (assert (= 5 (ffi/size [:int :pack-all :char])) "size packed struct 2")
  (assert (= 4 (ffi/align [:int :char])) "align 1")
  (assert (= 1 (ffi/align [:pack :int :char])) "align 2")
  (assert (= 1 (ffi/align [:int :char :pack-all])) "align 3")
  (assert (= 26 (ffi/size [:char :pack :int @[:char 21]]))
          "array struct size"))

(compwhen has-ffi
  (assert-error "bad struct issue #1512" (ffi/struct :void)))

(compwhen has-ffi
  (def buf @"")
  (ffi/write :u8 10 buf)
  (assert (= 1 (length buf)))
  (ffi/write :u8 10 buf)
  (assert (= 2 (length buf))))

# ffi/signature used to lay argument types out in a fixed array of 32
# entries on the C stack, so a 33rd argument type was written past the end
# of it. Signatures are now sized by their argument count.
(compwhen has-ffi
  (assert (ffi/signature :default :void ;(array/new-filled 33 :int))
          "signature with 33 arguments")
  # Enough of both integer and float arguments to spill well past the
  # registers onto the stack.
  (assert (ffi/signature :default :double
                         ;(array/new-filled 100 :int)
                         ;(array/new-filled 100 :double))
          "signature with 200 arguments"))

# ffi type sizes must stay within what a buffer can hold
(compwhen has-ffi
  # 2147483647 * 4 wraps a uint32_t size to 4294967292, which ffi/write used
  # as a memset length over a 10 byte buffer
  (assert-error "array type too large" (ffi/size @[:int32 2147483647]))
  (assert-error "array type too large in ffi/write"
                (ffi/write @[:int32 2147483647] 0 @"" 0))
  # two INT32_MAX fields add up to 4294967294
  (assert-error "struct type too large"
                (ffi/size (ffi/struct @[:int8 2147483647] @[:int8 2147483647])))
  # three of them wrap back into range, reporting 2147483645
  (assert-error "struct type size wraparound"
                (ffi/size (ffi/struct @[:int8 2147483647]
                                      @[:int8 2147483647]
                                      @[:int8 2147483647])))
  (assert (= 2147483647 (ffi/size @[:int8 2147483647]))
          "largest allowed array type"))

(end-suite)
