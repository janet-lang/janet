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

# 8a346ec
(assert (= (string/join @["one" "two" "three"]) "onetwothree")
        "string/join 1 argument")
(assert (= (string/join @["one" "two" "three"] ", ") "one, two, three")
        "string/join 2 arguments")
(assert (= (string/join @[] ", ") "") "string/join empty array")

(assert (= (string/find "123" "abc123def") 3) "string/find positive")
(assert (= (string/find "1234" "abc123def") nil) "string/find negative")

# String functions
# f41dab8f6
(assert (= 3 (string/find "abc" "   abcdefghijklmnop")) "string/find 1")
(assert (= 0 (string/find "A" "A")) "string/find 2")
(assert (string/has-prefix? "" "foo") "string/has-prefix? 1")
(assert (string/has-prefix? "fo" "foo") "string/has-prefix? 2")
(assert (not (string/has-prefix? "o" "foo")) "string/has-prefix? 3")
(assert (string/has-suffix? "" "foo") "string/has-suffix? 1")
(assert (string/has-suffix? "oo" "foo") "string/has-suffix? 2")
(assert (not (string/has-suffix? "f" "foo")) "string/has-suffix? 3")
(assert (= (string/replace "X" "." "XXX...XXX...XXX")  ".XX...XXX...XXX")
        "string/replace 1")
(assert (= (string/replace-all "X" "." "XXX...XXX...XXX") "...............")
        "string/replace-all 1")
(assert (= (string/replace-all "XX" "." "XXX...XXX...XXX") ".X....X....X")
        "string/replace-all 2")
(assert (= (string/replace "xx" string/ascii-upper "xxyxyxyxxxy")
           "XXyxyxyxxxy") "string/replace function")
(assert (= (string/replace-all "xx" string/ascii-upper "xxyxyxyxxxy")
           "XXyxyxyXXxy") "string/replace-all function")
(assert (= (string/replace "x" 12 "xyx") "12yx")
        "string/replace stringable")
(assert (= (string/replace-all "x" 12 "xyx") "12y12")
        "string/replace-all stringable")
(assert (= (string/ascii-lower "ABCabc&^%!@:;.") "abcabc&^%!@:;.")
        "string/ascii-lower")
(assert (= (string/ascii-upper "ABCabc&^%!@:;.") "ABCABC&^%!@:;.")
        "string/ascii-lower")
(assert (= (string/reverse "") "") "string/reverse 1")
(assert (= (string/reverse "a") "a") "string/reverse 2")
(assert (= (string/reverse "abc") "cba") "string/reverse 3")
(assert (= (string/reverse "abcd") "dcba") "string/reverse 4")
(assert (= (string/join @["one" "two" "three"] ",") "one,two,three")
        "string/join 1")
(assert (= (string/join @["one" "two" "three"] ", ") "one, two, three")
        "string/join 2")
(assert (= (string/join @["one" "two" "three"]) "onetwothree")
        "string/join 3")
(assert (= (string/join @[] "hi") "") "string/join 4")
(assert (= (string/trim " abcd ") "abcd") "string/trim 1")
(assert (= (string/trim "abcd \t\t\r\f") "abcd") "string/trim 2")
(assert (= (string/trim "\n\n\t abcd") "abcd") "string/trim 3")
(assert (= (string/trim "") "") "string/trim 4")
(assert (= (string/triml " abcd ") "abcd ") "string/triml 1")
(assert (= (string/triml "\tabcd \t\t\r\f") "abcd \t\t\r\f")
        "string/triml 2")
(assert (= (string/triml "abcd ") "abcd ") "string/triml 3")
(assert (= (string/trimr " abcd ") " abcd") "string/trimr 1")
(assert (= (string/trimr "\tabcd \t\t\r\f") "\tabcd") "string/trimr 2")
(assert (= (string/trimr " abcd") " abcd") "string/trimr 3")
(assert (deep= (string/split "," "one,two,three") @["one" "two" "three"])
        "string/split 1")
(assert (deep= (string/split "," "onetwothree") @["onetwothree"])
        "string/split 2")
(assert (deep= (string/find-all "e" "onetwothree") @[2 9 10])
        "string/find-all 1")
(assert (deep= (string/find-all "," "onetwothree") @[])
        "string/find-all 2")

# b26a7bb22
(assert-error "string/find error 1" (string/find "" "abcd"))
(assert-error "string/split error 1" (string/split "" "abcd"))
(assert-error "string/replace error 1" (string/replace "" "." "abcd"))
(assert-error "string/replace-all error 1"
              (string/replace-all "" "." "abcdabcd"))
(assert-error "string/find-all error 1" (string/find-all "" "abcd"))

# String bugs
# bcba0c027
(assert (deep= (string/find-all "qq" "qqq") @[0 1]) "string/find-all 1")
(assert (deep= (string/find-all "q" "qqq") @[0 1 2]) "string/find-all 2")
(assert (deep= (string/split "qq" "1qqqqz") @["1" "" "z"]) "string/split 1")
(assert (deep= (string/split "aa" "aaa") @["" "a"]) "string/split 2")

# some tests for string/format
# 0f0c415
(assert (= (string/format "pi = %6.3f" math/pi) "pi =  3.142") "%6.3f")
(assert (= (string/format "pi = %+6.3f" math/pi) "pi = +3.142") "%6.3f")
(assert (= (string/format "pi = %40.20g" math/pi)
           "pi =                     3.141592653589793116") "%6.3f")

(assert (= (string/format "🐼 = %6.3f" math/pi) "🐼 =  3.142") "UTF-8")
(assert (= (string/format "π = %.8g" math/pi) "π = 3.1415927") "π")
(assert (= (string/format "\xCF\x80 = %.8g" math/pi) "\xCF\x80 = 3.1415927")
        "\xCF\x80")

# String check-set
# b4e25e559
(assert (string/check-set "abc" "a") "string/check-set 1")
(assert (not (string/check-set "abc" "z")) "string/check-set 2")
(assert (string/check-set "abc" "abc") "string/check-set 3")
(assert (string/check-set "abc" "") "string/check-set 4")
(assert (not (string/check-set "" "aabc")) "string/check-set 5")
(assert (not (string/check-set "abc" "abcdefg")) "string/check-set 6")

# Trim empty string
# issue #174 - 9b605b27b
(assert (= "" (string/trim " ")) "string/trim regression")

# Keyword and Symbol slice
# e9911fee4
(assert (= :keyword (keyword/slice "some_keyword_slice" 5 12))
        "keyword slice")
(assert (= 'symbol (symbol/slice "some_symbol_slice" 5 11)) "symbol slice")

(end-suite)

