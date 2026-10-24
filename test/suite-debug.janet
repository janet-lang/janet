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

# Simple function break
# a8afc5b81
(debug/fbreak map 1)
(def f (fiber/new (fn [] (map inc [1 2 3])) :a))
(resume f)
(assert (= :debug (fiber/status f)) "debug/fbreak")
(debug/unfbreak map 1)
(map inc [1 2 3])

# Reading locals of an unmarshalled closure that captures an on-stack
# environment must validate the env first. The on-stack variant stores a
# negative, unvalidated offset after unmarshal; without validation debug/stack
# read the fiber pointer as a value array (out-of-bounds / type confusion).
(def outer
  (fiber/new
    (fn []
      (def secret 0xBEEF)
      (def capt (fn [] (yield :paused) secret))
      (yield capt)
      :done)
    :y))
(def capt (resume outer))
(def inner (fiber/new capt :y))
(resume inner)
(def image (marshal inner))
# Disable collection so the unmarshalled env keeps its unvalidated offset until
# debug/stack touches it.
(def old-interval (gcinterval))
(gcsetinterval 0)
(def inner2 (unmarshal image))
(var locals nil)
(each fr (debug/stack inner2)
  (when (fr :locals) (set locals (fr :locals))))
(gcsetinterval old-interval)
(assert (= 0xBEEF (get locals 'secret)) "unmarshalled on-stack env validated in debug/stack")

(end-suite)

