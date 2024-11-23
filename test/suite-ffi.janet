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

(def has-ffi (dyn 'ffi/native))
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

(end-suite)
