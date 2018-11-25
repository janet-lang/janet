# Copyright (c) 2018 Calvin Rose
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

(import test.helper :prefix "" :exit true)
(start-suite 3)

# Class stuff
(defclass Car)

(defnew Car
  "Make a new car."
  [color]
  (put self :color color))

(defm Car:honk
  "Honk the horn."
  []
  (string "Honk! from a " (get self :color) " car!"))

(def redcar (Car:new :red))
(def greencar (Car:new :green))

(assert (= (call redcar:honk) ($ redcar:honk)) "$ alias for call 1")
(assert (= (call greencar:honk) ($ greencar:honk)) "$ alias for call 2")

(assert (= (call redcar:honk) "Honk! from a :red car!") "method call 1")
(assert (= (call greencar:honk) "Honk! from a :green car!") "method call 2")

(def wrapper (wrap-call redcar:honk))
(assert (= (call redcar:honk) (wrapper)) "wrap-call")

(end-suite)

