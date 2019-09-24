#' Copyright (c) 2019 Calvin Rose
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
(start-suite 2)

# Buffer stuff
(defn buffer=
  [a b]
  (= (string a) (string b)))

(assert (buffer= @"abcd" @"abcd") "buffer equal 1")
(assert (buffer= @"abcd" (buffer "ab" "cd")) "buffer equal 2")
(assert (not= @"" @"") "buffer not equal 1")
(assert (not= @"abcd" @"abcd") "buffer not equal 2")

(defn buffer-factory
  []
  @"im am a buffer")

(assert (not= (buffer-factory) (buffer-factory)) "buffer instantiation")

(assert (= (length @"abcdef") 6) "buffer length")

# Looping idea
(def xs
  (seq [x :in '[-1 0 1] y :in '[-1 0 1] :when (not= x y 0)] (tuple x y)))
(def txs (apply tuple xs))

(assert (= txs '[[-1 -1] [-1 0] [-1 1] [0 -1] [0 1] [1 -1] [1 0] [1 1]]) "nested seq")

# Generators
(def gen (generate [x :range [0 100] :when (pos? (% x 4))] x))
(var gencount 0)
(loop [x :generate gen]
  (++ gencount)
  (assert (pos? (% x 4)) "generate in loop"))
(assert (= gencount 75) "generate loop count")

# Check x:digits: works as symbol and not a hex number
(def x1 100)
(assert (= x1 100) "x1 as symbol")
(def X1 100)
(assert (= X1 100) "X1 as symbol")

# String functions
(assert (= 3 (string/find "abc" "   abcdefghijklmnop")) "string/find 1")
(assert (= 0 (string/find "A" "A")) "string/find 2")
(assert (string/has-prefix? "" "foo") "string/has-prefix? 1")
(assert (string/has-prefix? "fo" "foo") "string/has-prefix? 2")
(assert (not (string/has-prefix? "o" "foo")) "string/has-prefix? 3")
(assert (string/has-suffix? "" "foo") "string/has-suffix? 1")
(assert (string/has-suffix? "oo" "foo") "string/has-suffix? 2")
(assert (not (string/has-suffix? "f" "foo")) "string/has-suffix? 3")
(assert (= (string/replace "X" "." "XXX...XXX...XXX")  ".XX...XXX...XXX") "string/replace 1")
(assert (= (string/replace-all "X" "." "XXX...XXX...XXX") "...............") "string/replace-all 1")
(assert (= (string/replace-all "XX" "." "XXX...XXX...XXX") ".X....X....X") "string/replace-all 2")
(assert (= (string/ascii-lower "ABCabc&^%!@:;.") "abcabc&^%!@:;.") "string/ascii-lower")
(assert (= (string/ascii-upper "ABCabc&^%!@:;.") "ABCABC&^%!@:;.") "string/ascii-lower")
(assert (= (string/reverse "") "") "string/reverse 1")
(assert (= (string/reverse "a") "a") "string/reverse 2")
(assert (= (string/reverse "abc") "cba") "string/reverse 3")
(assert (= (string/reverse "abcd") "dcba") "string/reverse 4")
(assert (= (string/join @["one" "two" "three"] ",") "one,two,three") "string/join 1")
(assert (= (string/join @["one" "two" "three"] ", ") "one, two, three") "string/join 2")
(assert (= (string/join @["one" "two" "three"]) "onetwothree") "string/join 3")
(assert (= (string/join @[] "hi") "") "string/join 4")
(assert (= (string/trim " abcd ") "abcd") "string/trim 1")
(assert (= (string/trim "abcd \t\t\r\f") "abcd") "string/trim 2")
(assert (= (string/trim "\n\n\t abcd") "abcd") "string/trim 3")
(assert (= (string/trim "") "") "string/trim 4")
(assert (= (string/triml " abcd ") "abcd ") "string/triml 1")
(assert (= (string/triml "\tabcd \t\t\r\f") "abcd \t\t\r\f") "string/triml 2")
(assert (= (string/triml "abcd ") "abcd ") "string/triml 3")
(assert (= (string/trimr " abcd ") " abcd") "string/trimr 1")
(assert (= (string/trimr "\tabcd \t\t\r\f") "\tabcd") "string/trimr 2")
(assert (= (string/trimr " abcd") " abcd") "string/trimr 3")
(assert (deep= (string/split "," "one,two,three") @["one" "two" "three"]) "string/split 1")
(assert (deep= (string/split "," "onetwothree") @["onetwothree"]) "string/split 2")
(assert (deep= (string/find-all "e" "onetwothree") @[2 9 10]) "string/find-all 1")
(assert (deep= (string/find-all "," "onetwothree") @[]) "string/find-all 2")

(assert-error "string/find error 1" (string/find "" "abcd"))
(assert-error "string/split error 1" (string/split "" "abcd"))
(assert-error "string/replace error 1" (string/replace "" "." "abcd"))
(assert-error "string/replace-all error 1" (string/replace-all "" "." "abcdabcd"))
(assert-error "string/find-all error 1" (string/find-all "" "abcd"))

# Check if abstract test works
(assert (abstract? stdout) "abstract? stdout")
(assert (abstract? stdin) "abstract? stdin")
(assert (abstract? stderr) "abstract? stderr")
(assert (not (abstract? nil)) "not abstract? nil")
(assert (not (abstract? 1)) "not abstract? 1")
(assert (not (abstract? 3)) "not abstract? 3")
(assert (not (abstract? 5)) "not abstract? 5")

(end-suite)

