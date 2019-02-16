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

(import test/helper :prefix "" :exit true)
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

(end-suite)

