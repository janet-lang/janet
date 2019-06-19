# Copyright (c) 2019 Calvin Rose
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
(start-suite 4)
# some tests for string/format and buffer/format

(assert (= (string (buffer/format @"" "pi = %6.3f" math/pi)) "pi =  3.142") "%6.3f")
(assert (= (string (buffer/format @"" "pi = %+6.3f" math/pi)) "pi = +3.142") "%6.3f")
(assert (= (string (buffer/format @"" "pi = %40.20g" math/pi)) "pi =                     3.141592653589793116") "%6.3f")

(assert (= (string (buffer/format @"" "🐼 = %6.3f" math/pi)) "🐼 =  3.142") "UTF-8")
(assert (= (string (buffer/format @"" "π = %.8g" math/pi)) "π = 3.1415927") "π")
(assert (= (string (buffer/format @"" "\xCF\x80 = %.8g" math/pi)) "\xCF\x80 = 3.1415927") "\xCF\x80")

(assert (= (string/format "pi = %6.3f" math/pi) "pi =  3.142") "%6.3f")
(assert (= (string/format "pi = %+6.3f" math/pi) "pi = +3.142") "%6.3f")
(assert (= (string/format "pi = %40.20g" math/pi) "pi =                     3.141592653589793116") "%6.3f")

(assert (= (string/format "🐼 = %6.3f" math/pi) "🐼 =  3.142") "UTF-8")
(assert (= (string/format "π = %.8g" math/pi) "π = 3.1415927") "π")
(assert (= (string/format "\xCF\x80 = %.8g" math/pi) "\xCF\x80 = 3.1415927") "\xCF\x80")

# Range
(assert (deep= (range 10) @[0 1 2 3 4 5 6 7 8 9]) "range 1 argument")
(assert (deep= (range 5 10) @[5 6 7 8 9]) "range 2 arguments")
(assert (deep= (range 5 10 2) @[5 7 9]) "range 3 arguments")

# More marshalling code

(defn check-image
  "Run a marshaling test using the make-image and load-image functions."
  [x msg]
  (assert-no-error msg (load-image (make-image x))))

(check-image (fn [] (fn [] 1)) "marshal nested functions")
(check-image (fiber/new (fn [] (fn [] 1))) "marshal nested functions in fiber")
(check-image (fiber/new (fn [] (fiber/new (fn [] 1)))) "marshal nested fibers")

(def issue-53-x 
  (fiber/new 
    (fn [] 
      (var y (fiber/new (fn [] (print "1") (yield) (print "2")))))))

(check-image issue-53-x "issue 53 regression")

# Bracket tuple issue

(def do 3)
(assert (= [3 1 2 3] [do 1 2 3]) "bracket tuples are never special forms")
(assert (= ~(,defn 1 2 3) [defn 1 2 3]) "bracket tuples are never macros")
(assert (= ~(,+ 1 2 3) [+ 1 2 3]) "bracket tuples are never function calls")

(end-suite)

