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
(def p (-> p make-image load-image))

(assert (peg/match p "abc") "complex peg grammar 1")
(assert (peg/match p "[1 2 3 4]") "complex peg grammar 2")

#
# fn compilation special
#
(defn myfn1 [[x y z] & more]
  more)
(defn myfn2 [head & more]
  more)
(assert (= (myfn1 [1 2 3] 4 5 6) (myfn2 [:a :b :c] 4 5 6)) "destructuring and varargs")

#
# Test propagation of signals via fibers
#

(def f (fiber/new (fn [] (error :abc) 1) :ei))
(def res (resume f))
(assert-error :abc (propagate res f) "propagate 1")

# table/clone

(defn check-table-clone [x msg]
  (assert (= (table/to-struct x) (table/to-struct (table/clone x))) msg))

(check-table-clone @{:a 123 :b 34 :c :hello : 945 0 1 2 3 4 5} "table/clone 1")
(check-table-clone @{} "table/clone 1")

# Issue #142

(def buffer (tarray/buffer 8))
(def buffer-float64-view (tarray/new :float64 1 1 0 buffer))
(def buffer-uint32-view (tarray/new :uint32 2 1 0 buffer))

(set (buffer-uint32-view 1) 0xfffe9234)
(set (buffer-uint32-view 0) 0x56789abc)

(assert (buffer-float64-view 0) "issue #142 nanbox hijack 1")
(assert (= (type (buffer-float64-view 0)) :number) "issue #142 nanbox hijack 2")
(assert (= (type (unmarshal @"\xC8\xbc\x9axV4\x92\xfe\xff")) :number) "issue #142 nanbox hijack 3")

# Make sure Carriage Returns don't end up in doc strings.

(assert (not (string/find "\r" (get ((fiber/getenv (fiber/current)) 'cond) :doc ""))) "no \\r in doc strings")

# module/expand-path regression
(with-dyns [:syspath ".janet/.janet"]
  (assert (= (string (module/expand-path "hello" ":sys:/:all:.janet"))
             ".janet/.janet/hello.janet") "module/expand-path 1"))

# comp should be variadic
(assert (= 10 ((comp +) 1 2 3 4)) "variadic comp 1")
(assert (= 11 ((comp inc +) 1 2 3 4)) "variadic comp 2")
(assert (= 12 ((comp inc inc +) 1 2 3 4)) "variadic comp 3")
(assert (= 13 ((comp inc inc inc +) 1 2 3 4)) "variadic comp 4")
(assert (= 14 ((comp inc inc inc inc +) 1 2 3 4)) "variadic comp 5")
(assert (= 15 ((comp inc inc inc inc inc +) 1 2 3 4)) "variadic comp 6")
(assert (= 16 ((comp inc inc inc inc inc inc +) 1 2 3 4)) "variadic comp 7")

# Function shorthand
(assert (= (|(+ 1 2 3)) 6) "function shorthand 1")
(assert (= (|(+ 1 2 3 $) 4) 10) "function shorthand 2")
(assert (= (|(+ 1 2 3 $0) 4) 10) "function shorthand 3")
(assert (= (|(+ $0 $0 $0 $0) 4) 16) "function shorthand 4")
(assert (= (|(+ $ $ $ $) 4) 16) "function shorthand 5")
(assert (= (|4) 4) "function shorthand 6")
(assert (= (((|||4))) 4) "function shorthand 7")
(assert (= (|(+ $1 $1 $1 $1) 2 4) 16) "function shorthand 8")
(assert (= (|(+ $0 $1 $3 $2 $6) 0 1 2 3 4 5 6) 12) "function shorthand 9")
(assert (= (|(+ $0 $99) ;(range 100)) 99) "function shorthand 10")

# Simple function break
(debug/fbreak map 1)
(def f (fiber/new (fn [] (map inc [1 2 3])) :a))
(resume f)
(assert (= :debug (fiber/status f)) "debug/fbreak")
(debug/unfbreak map 1)
(map inc [1 2 3])

(defn idx= [x y] (= (tuple/slice x) (tuple/slice y)))

# Simple take, drop, etc. tests.
(assert (idx= (take 10 (range 100)) (range 10)) "take 10")
(assert (idx= (drop 10 (range 100)) (range 10 100)) "drop 10")

# Printing to buffers
(def out-buf @"")
(def err-buf @"")
(with-dyns [:out out-buf :err err-buf]
  (print "Hello")
  (prin "hi")
  (eprint "Sup")
  (eprin "not much."))

(assert (= (string out-buf) "Hello\nhi") "print and prin to buffer 1")
(assert (= (string err-buf) "Sup\nnot much.") "eprint and eprin to buffer 1")

(assert (= (string '()) (string [])) "empty bracket tuple literal")

# with-vars
(var abc 123)
(assert (= 356 (with-vars [abc 456] (- abc 100))) "with-vars 1")
(assert-error "with-vars 2" (with-vars [abc 456] (error :oops)))
(assert (= abc 123) "with-vars 3")

# Trim empty string
(assert (= "" (string/trim " ")) "string/trim regression")

# RNGs

(defn test-rng
  [rng]
  (assert (all identity (seq [i :range [0 1000]]
                             (<= (math/rng-int rng i) i))) "math/rng-int test")
  (assert (all identity (seq [i :range [0 1000]]
    (def x (math/rng-uniform rng))
    (and (>= x 0) (< x 1))))
          "math/rng-uniform test"))

(def seedrng (math/rng 123))
(for i 0 75
  (test-rng (math/rng (:int seedrng))))

(assert (deep-not= (-> 123 math/rng (:buffer 16))
                   (-> 456 math/rng (:buffer 16))) "math/rng-buffer 1")

(assert-no-error "math/rng-buffer 2" (math/seedrandom "abcdefg"))

# OS Date test

(assert (deep= {:year-day 0
                :minutes 30
                :month 0
                :dst false
                :seconds 0
                :year 2014
                :month-day 0
                :hours 20 
                :week-day 3}
               (os/date 1388608200)) "os/date")

# Appending buffer to self

(with-dyns [:out @""]
  (prin "abcd")
  (prin (dyn :out))
  (prin (dyn :out))
  (assert (deep= (dyn :out) @"abcdabcdabcdabcd") "print buffer to self"))

(os/setenv "TESTENV1" "v1")
(os/setenv "TESTENV2" "v2")
(assert (= (os/getenv "TESTENV1") "v1") "getenv works")
(def environ (os/environ))
(assert (= [(environ "TESTENV1") (environ "TESTENV2")] ["v1" "v2"]) "environ works")

# Issue #183 - just parse it :)
1e-4000000000000000000000

# Ensure randomness puts n of pred into our buffer eventually
(defn cryptorand-check
  [n pred]
  (def max-attempts 10000)
  (var attempts 0)
  (while (not= attempts max-attempts)
    (def cryptobuf (os/cryptorand 10))
    (when (= n (count pred cryptobuf))
      (break))
    (++ attempts))
  (not= attempts max-attempts))

(def v (math/rng-int (math/rng (os/time)) 100))
(assert (cryptorand-check 0 |(= $ v)) "cryptorand skips value sometimes")
(assert (cryptorand-check 1 |(= $ v)) "cryptorand has value sometimes")

(do 
  (def buf (buffer/new-filled 1))
  (os/cryptorand 1 buf)
  (assert (= (in buf 0) 0) "cryptorand doesn't overwrite buffer")
  (assert (= (length buf) 2) "cryptorand appends to buffer"))

# Nested quasiquotation

(def nested ~(a ~(b ,(+ 1 2) ,(foo ,(+ 1 3) d) e) f))
(assert (deep= nested '(a ~(b ,(+ 1 2) ,(foo 4 d) e) f)) "nested quasiquote")

# Top level unquote
(defn constantly
  []
  (comptime (math/random)))

(assert (= (constantly) (constantly)) "comptime 1")

(end-suite)
