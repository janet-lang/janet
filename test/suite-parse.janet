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

# 7e46ead2f
(assert (not false) "false literal")
(assert true "true literal")
(assert (not nil) "nil literal")

(assert (= '(1 2 3) (quote (1 2 3)) (tuple 1 2 3)) "quote shorthand")

# String literals
# 45f8db0
(assert (= "abcd" "\x61\x62\x63\x64") "hex escapes")
(assert (= "\e" "\x1B") "escape character")
(assert (= "\x09" "\t") "tab character")

# Long strings
# 7e6342720
(assert (= "hello, world" `hello, world`) "simple long string")
(assert (= "hello, \"world\"" `hello, "world"`)
        "long string with embedded quotes")
(assert (= "hello, \\\\\\ \"world\"" `hello, \\\ "world"`)
        "long string with embedded quotes and backslashes")

#
# Longstring indentation
#
# 7aa4241
(defn reindent
  "Reindent the contents of a longstring as the Janet parser would.
  This include removing leading and trailing newlines."
  [text indent]

  # Detect minimum indent
  (var rewrite true)
  (each index (string/find-all "\n" text)
    (for i (+ index 1) (+ index indent 1)
      (case (get text i)
        nil (break)
        (chr "\r") (if-not (= (chr "\n") (get text (inc i)))
                     (set rewrite false))
        (chr "\n") (break)
        (chr " ") nil
        (set rewrite false))))

  # Only re-indent if no dedented characters.
  (def str
    (if rewrite
      (peg/replace-all ~(* '(* (? "\r") "\n") (between 0 ,indent " "))
                      (fn [mtch eol] eol) text)
      text))

  (def first-eol (cond
                   (string/has-prefix? "\r\n" str) :crlf
                   (string/has-prefix? "\n" str) :lf))
  (def last-eol (cond
                  (string/has-suffix? "\r\n" str) :crlf
                  (string/has-suffix? "\n" str) :lf))
  (string/slice str (case first-eol :crlf 2 :lf 1 0) (case last-eol :crlf -3 :lf -2)))

(defn reindent-reference
  "Same as reindent but use parser functionality. Useful for
  validating conformance."
  [text indent]
  (if (empty? text) (break text))
  (def source-code
    (string (string/repeat " " indent) "``````"
            text
            "``````"))
  (parse source-code))

(var indent-counter 0)
(defn check-indent
  [text indent]
  (++ indent-counter)
  (let [a (reindent text indent)
        b (reindent-reference text indent)]
    (assert (= a b)
            (string/format "reindent: %q, parse: %q (indent-test #%d with indent of %d)" a b indent-counter indent)
            )))

# Unix EOLs
(check-indent "" 0)
(check-indent "\n" 0)
(check-indent "\n" 1)
(check-indent "\n\n" 0)
(check-indent "\n\n" 1)
(check-indent "\nHello, world!" 0)
(check-indent "\nHello, world!" 1)
(check-indent "Hello, world!" 0)
(check-indent "Hello, world!" 1)
(check-indent "\n    Hello, world!" 4)
(check-indent "\n    Hello, world!\n" 4)
(check-indent "\n    Hello, world!\n   " 4)
(check-indent "\n    Hello, world!\n    " 4)
(check-indent "\n    Hello, world!\n   dedented text\n    " 4)
(check-indent "\n    Hello, world!\n    indented text\n    " 4)
# Windows EOLs
(check-indent "\r\n" 0)
(check-indent "\r\n" 1)
(check-indent "\r\n\r\n" 0)
(check-indent "\r\n\r\n" 1)
(check-indent "\r\nHello, world!" 0)
(check-indent "\r\nHello, world!" 1)
(check-indent "\r\n    Hello, world!\r\n   " 4)
(check-indent "\r\n    Hello, world!\r\n    " 4)
(check-indent "\r\n    Hello, world!\r\n   dedented text\r\n    " 4)
(check-indent "\r\n    Hello, world!\r\n    indented text\r\n    " 4)

# Symbols with @ character
# d68eae9
(def @ 1)
(assert (= @ 1) "@ symbol")
(def @-- 2)
(assert (= @-- 2) "@-- symbol")
(def @hey 3)
(assert (= @hey 3) "@hey symbol")

# Parser clone
# 43520ac67
(def p (parser/new))
(assert (= 7 (parser/consume p "(1 2 3 ")) "parser 1")
(def p2 (parser/clone p))
(parser/consume p2 ") 1 ")
(parser/consume p ") 1 ")
(assert (deep= (parser/status p) (parser/status p2)) "parser 2")
(assert (deep= (parser/state p) (parser/state p2)) "parser 3")

# Parser errors
# 976dfc719
(defn parse-error [input]
  (def p (parser/new))
  (parser/consume p input)
  (parser/error p))

# Invalid utf-8 sequences
(assert (not= nil (parse-error @"\xc3\x28")) "reject invalid utf-8 symbol")
(assert (not= nil (parse-error @":\xc3\x28")) "reject invalid utf-8 keyword")

# Parser line and column numbers
# 77b79e989
(defn parser-location [input &opt location]
  (def p (parser/new))
  (parser/consume p input)
  (if location
    (parser/where p ;location)
    (parser/where p)))

(assert (= [1 7] (parser-location @"(+ 1 2)")) "parser location 1")
(assert (= [5 7] (parser-location @"(+ 1 2)" [5])) "parser location 2")
(assert (= [10 10] (parser-location @"(+ 1 2)" [10 10])) "parser location 3")

# Issue #861 - should be valgrind clean
# 39c6be7cb
(def step1 "(a b c d)\n")
(def step2 "(a b)\n")
(def p1 (parser/new))
(parser/state p1)
(parser/consume p1 step1)
(loop [v :iterate (parser/produce p1)])
(parser/state p1)
(def p2 (parser/clone p1))
(parser/state p2)
(parser/consume p2 step2)
(loop [v :iterate (parser/produce p2)])
(parser/state p2)

# parser delimiter errors
(defn test-error [delim fmt]
  (def p (parser/new))
  (parser/consume p delim)
  (parser/eof p)
  (def msg (string/format fmt delim))
  (assert (= (parser/error p) msg) "delimiter error"))
(each c [ "(" "{" "[" "\"" "``" ]
  (test-error c "unexpected end of source, %s opened at line 1, column 1"))

# parser/insert
(def p (parser/new))
(parser/consume p "(")
(parser/insert p "hello")
(parser/consume p ")")
(assert (= (parser/produce p) ["hello"]))

(def p (parser/new))
(parser/consume p `("hel`)
(parser/insert p `lo`)
(parser/consume p `")`)
(assert (= (parser/produce p) ["hello"]))

(end-suite)

