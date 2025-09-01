# Copyright (c) 2025 Calvin Rose & contributors
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

# Issue #1629
(def thread-channel (ev/thread-chan 100))
(def super (ev/thread-chan 10))
(defn worker []
  (while true
    (def item (ev/take thread-channel))
    (when (= item :deadline)
      (ev/deadline 0.1 nil (fiber/current) true))))
(ev/thread worker nil :n super)
(ev/give thread-channel :item)
(ev/sleep 0.05)
(ev/give thread-channel :item)
(ev/sleep 0.05)
(ev/give thread-channel :deadline)
(ev/sleep 0.05)
(ev/give thread-channel :item)
(ev/sleep 0.05)
(ev/give thread-channel :item)
(ev/sleep 0.15)
(assert (deep= '(:error "deadline expired" nil) (ev/take super)) "deadline expirataion")

# Another variant
(def thread-channel (ev/thread-chan 100))
(def super (ev/thread-chan 10))
(defn worker []
  (while true
    (def item (ev/take thread-channel))
    (when (= item :deadline)
      (ev/deadline 0.1))))
(ev/thread worker nil :n super)
(ev/give thread-channel :deadline)
(ev/sleep 0.2)
(assert (deep= '(:error "deadline expired" nil) (ev/take super)) "deadline expirataion")

(end-suite)
