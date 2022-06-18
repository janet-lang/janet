# Copyright (c) 2022 Calvin Rose & contributors
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
(start-suite 12)

(var counter 0)
(def thunk (delay (++ counter)))
(assert (= (thunk) 1) "delay 1")
(assert (= counter 1) "delay 2")
(assert (= (thunk) 1) "delay 3")
(assert (= counter 1) "delay 4")

# FFI check
(compwhen (dyn 'ffi/native)
  (ffi/context))
(compwhen (dyn 'ffi/native)
  (ffi/defbind memcpy :ptr [dest :ptr src :ptr n :size]))
(compwhen (dyn 'ffi/native)
  (def buffer1 @"aaaa")
  (def buffer2 @"bbbb")
  (memcpy buffer1 buffer2 4)
  (assert (= (string buffer1) "bbbb") "ffi 1 - memcpy"))

(end-suite)

