# Copyright (c) 2023 Calvin Rose & contributors
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
(start-suite 10)

# index-of
(assert (= nil (index-of 10 [])) "index-of 1")
(assert (= nil (index-of 10 [1 2 3])) "index-of 2")
(assert (= 1 (index-of 2 [1 2 3])) "index-of 3")
(assert (= 0 (index-of :a [:a :b :c])) "index-of 4")
(assert (= nil (index-of :a {})) "index-of 5")
(assert (= :a (index-of :A {:a :A :b :B})) "index-of 6")
(assert (= :a (index-of :A @{:a :A :b :B})) "index-of 7")
(assert (= 0 (index-of (chr "a") "abc")) "index-of 8")
(assert (= nil (index-of (chr "a") "")) "index-of 9")
(assert (= nil (index-of 10 @[])) "index-of 10")
(assert (= nil (index-of 10 @[1 2 3])) "index-of 11")

# Regression
(assert (= {:x 10} (|(let [x $] ~{:x ,x}) 10)) "issue 463")

# macex testing
(assert (deep= (macex1 '~{1 2 3 4}) '~{1 2 3 4}) "macex1 qq struct")
(assert (deep= (macex1 '~@{1 2 3 4}) '~@{1 2 3 4}) "macex1 qq table")
(assert (deep= (macex1 '~(1 2 3 4)) '~[1 2 3 4]) "macex1 qq tuple")
(assert (= :brackets (tuple/type (1 (macex1 '~[1 2 3 4])))) "macex1 qq bracket tuple")
(assert (deep= (macex1 '~@[1 2 3 4 ,blah]) '~@[1 2 3 4 ,blah]) "macex1 qq array")

# Sourcemaps in threading macros
(defn check-threading [macro expansion]
  (def expanded (macex1 (tuple macro 0 '(x) '(y))))
  (assert (= expanded expansion) (string macro " expansion value"))
  (def smap-x (tuple/sourcemap (get expanded 1)))
  (def smap-y (tuple/sourcemap expanded))
  (def line first)
  (defn column [t] (t 1))
  (assert (not= smap-x [-1 -1]) (string macro " x sourcemap existence"))
  (assert (not= smap-y [-1 -1]) (string macro " y sourcemap existence"))
  (assert (or (< (line smap-x) (line smap-y))
              (and (= (line smap-x) (line smap-y))
                   (< (column smap-x) (column smap-y))))
          (string macro " relation between x and y sourcemap")))

(check-threading '-> '(y (x 0)))
(check-threading '->> '(y (x 0)))

# keep-syntax
(let [brak '[1 2 3]
      par '(1 2 3)]

  (tuple/setmap brak 2 1)

  (assert (deep= (keep-syntax brak @[1 2 3]) @[1 2 3]) "keep-syntax brackets ignore array")
  (assert (= (keep-syntax! brak @[1 2 3]) '[1 2 3]) "keep-syntax! brackets replace array")

  (assert (= (keep-syntax! par (map inc @[1 2 3])) '(2 3 4)) "keep-syntax! parens coerce array")
  (assert (not= (keep-syntax! brak @[1 2 3]) '(1 2 3)) "keep-syntax! brackets not parens")
  (assert (not= (keep-syntax! par @[1 2 3]) '[1 2 3]) "keep-syntax! parens not brackets")
  (assert (= (tuple/sourcemap brak)
             (tuple/sourcemap (keep-syntax! brak @[1 2 3]))) "keep-syntax! brackets source map")

  (keep-syntax par brak)
  (assert (not= (tuple/sourcemap brak) (tuple/sourcemap par)) "keep-syntax no mutate")
  (assert (= (keep-syntax 1 brak) brak) "keep-syntax brackets ignore type"))

# Cancel test
(def f (fiber/new (fn [&] (yield 1) (yield 2) (yield 3) 4) :yti))
(assert (= 1 (resume f)) "cancel resume 1")
(assert (= 2 (resume f)) "cancel resume 2")
(assert (= :hi (cancel f :hi)) "cancel resume 3")
(assert (= :error (fiber/status f)) "cancel resume 4")

# Curenv
(assert (= (curenv) (curenv 0)) "curenv 1")
(assert (= (table/getproto (curenv)) (curenv 1)) "curenv 2")
(assert (= nil (curenv 1000000)) "curenv 3")
(assert (= root-env (curenv 1)) "curenv 4")

# Import macro test
(assert-no-error "import macro 1" (macex '(import a :as b :fresh maybe)))
(assert (deep= ~(,import* "a" :as "b" :fresh maybe) (macex '(import a :as b :fresh maybe))) "import macro 2")

# #477 walk preserving bracket type
(assert (= :brackets (tuple/type (postwalk identity '[]))) "walk square brackets 1")
(assert (= :brackets (tuple/type (walk identity '[]))) "walk square brackets 2")

# # off by 1 error in inttypes
(assert (= (int/s64 "-0x8000_0000_0000_0000") (+ (int/s64 "0x7FFF_FFFF_FFFF_FFFF") 1)) "int types wrap around")

#
# Longstring indentation
#

(defn reindent
  "Reindent a the contents of a longstring as the Janet parser would.
  This include removing leading and trailing newlines."
  [text indent]

  # Detect minimum indent
  (var rewrite true)
  (each index (string/find-all "\n" text)
    (for i (+ index 1) (+ index indent 1)
      (case (get text i)
        nil (break)
        (chr "\n") (break)
        (chr " ") nil
        (set rewrite false))))

  # Only re-indent if no dedented characters.
  (def str
    (if rewrite
      (peg/replace-all ~(* "\n" (between 0 ,indent " ")) "\n" text)
      text))

  (def first-nl (= (chr "\n") (first str)))
  (def last-nl (= (chr "\n") (last str)))
  (string/slice str (if first-nl 1 0) (if last-nl -2)))

(defn reindent-reference
  "Same as reindent but use parser functionality. Useful for validating conformance."
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
    (assert (= a b) (string "indent " indent-counter " (indent=" indent ")"))))

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

# String bugs
(assert (deep= (string/find-all "qq" "qqq") @[0 1]) "string/find-all 1")
(assert (deep= (string/find-all "q" "qqq") @[0 1 2]) "string/find-all 2")
(assert (deep= (string/split "qq" "1qqqqz") @["1" "" "z"]) "string/split 1")
(assert (deep= (string/split "aa" "aaa") @["" "a"]) "string/split 2")

# Comparisons
(assert (> 1e23 100) "less than immediate 1")
(assert (> 1e23 1000) "less than immediate 2")
(assert (< 100 1e23) "greater than immediate 1")
(assert (< 1000 1e23) "greater than immediate 2")

# os/execute with environment variables
(assert (= 0 (os/execute [(dyn :executable) "-e" "(+ 1 2 3)"] :pe (merge (os/environ) {"HELLO" "WORLD"}))) "os/execute with env")

# Regression #638
(compwhen
  (dyn 'ev/go)
  (assert
    (= [true :caught]
       (protect
         (try
           (do
             (ev/sleep 0)
             (with-dyns []
               (ev/sleep 0)
               (error "oops")))
           ([err] :caught))))
    "regression #638"))


# Struct prototypes
(def x (struct/with-proto {1 2 3 4} 5 6))
(def y (-> x marshal unmarshal))
(def z {1 2 3 4})
(assert (= 2 (get x 1)) "struct get proto value 1")
(assert (= 4 (get x 3)) "struct get proto value 2")
(assert (= 6 (get x 5)) "struct get proto value 3")
(assert (= x y) "struct proto marshal equality 1")
(assert (= (getproto x) (getproto y)) "struct proto marshal equality 2")
(assert (= 0 (cmp x y)) "struct proto comparison 1")
(assert (= 0 (cmp (getproto x) (getproto y))) "struct proto comparison 2")
(assert (not= (cmp x z) 0) "struct proto comparison 3")
(assert (not= (cmp y z) 0) "struct proto comparison 4")
(assert (not= x z) "struct proto comparison 5")
(assert (not= y z) "struct proto comparison 6")
(assert (= (x 5) 6) "struct proto get 1")
(assert (= (y 5) 6) "struct proto get 1")
(assert (deep= x y) "struct proto deep= 1")
(assert (deep-not= x z) "struct proto deep= 2")
(assert (deep-not= y z) "struct proto deep= 3")

# Issue #751
(def t {:side false})
(assert (nil? (get-in t [:side :note])) "get-in with false value")
(assert (= (get-in t [:side :note] "dflt") "dflt")
        "get-in with false value and default")

(assert (= (math/gcd 462 1071) 21) "math/gcd 1")
(assert (= (math/lcm 462 1071) 23562) "math/lcm 1")

# Evaluate stream with `dofile`
(def [r w] (os/pipe))
(:write w "(setdyn :x 10)")
(:close w)
(def stream-env (dofile r))
(assert (= (stream-env :x) 10) "dofile stream 1")

# Issue #861 - should be valgrind clean
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

# Check missing struct proto bug.
(assert (struct/getproto (struct/with-proto {:a 1} :b 2 :c nil)) "missing struct proto")

(end-suite)
