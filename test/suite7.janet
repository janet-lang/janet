# Copyright (c) 2019 Calvin Rose & contributors
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
(start-suite 7)

# Using a large test grammar

(def- core-env (table/getproto (fiber/getenv (fiber/current))))
(def- specials {'fn true
               'var true
               'do true
               'while true
               'def true
               'splice true
               'set true
               'unquote true
               'quasiquote true
               'quote true
               'if true})

(defn- check-number [text] (and (scan-number text) text))

(defn capture-sym
  [text]
  (def sym (symbol text))
  [(if (or (core-env sym) (specials sym)) :coresym :symbol) text])

(def grammar
  ~{:ws (set " \v\t\r\f\n\0")
    :readermac (set "';~,")
    :symchars (+ (range "09" "AZ" "az" "\x80\xFF") (set "!$%&*+-./:<?=>@^_|"))
    :token (some :symchars)
    :hex (range "09" "af" "AF")
    :escape (* "\\" (+ (set "ntrvzf0e\"\\")
                       (* "x" :hex :hex)
                       (error (constant "bad hex escape"))))
    :comment (/ '(* "#" (any (if-not (+ "\n" -1) 1))) (constant :comment))
    :symbol (/ ':token ,capture-sym)
    :keyword (/ '(* ":" (any :symchars)) (constant :keyword))
    :constant (/ '(+ "true" "false" "nil") (constant :constant))
    :bytes (* "\"" (any (+ :escape (if-not "\"" 1))) "\"")
    :string (/ ':bytes (constant :string))
    :buffer (/ '(* "@" :bytes) (constant :string))
    :long-bytes {:delim (some "`")
                 :open (capture :delim :n)
                 :close (cmt (* (not (> -1 "`")) (-> :n) ':delim) ,=)
                 :main (drop (* :open (any (if-not :close 1)) :close))}
    :long-string (/ ':long-bytes (constant :string))
    :long-buffer (/ '(* "@" :long-bytes) (constant :string))
    :number (/ (cmt ':token ,check-number) (constant :number))
    :raw-value (+ :comment :constant :number :keyword
                  :string :buffer :long-string :long-buffer
                  :parray :barray :ptuple :btuple :struct :dict :symbol)
    :value (* (? '(some (+ :ws :readermac))) :raw-value '(any :ws))
    :root (any :value)
    :root2 (any (* :value :value))
    :ptuple (* '"(" :root (+ '")" (error "")))
    :btuple (* '"[" :root (+ '"]" (error "")))
    :struct (* '"{" :root2 (+ '"}" (error "")))
    :parray (* '"@" :ptuple)
    :barray (* '"@" :btuple)
    :dict (* '"@"  :struct)
    :main (+ :root (error ""))})

(def p (peg/compile grammar))

# Just make sure is valgrind clean.
(-> p make-image load-image)

(assert (peg/match p "abc") "complex peg grammar 1")
(assert (peg/match p "[1 2 3 4]") "complex peg grammar 2")

(end-suite)
