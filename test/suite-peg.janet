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

# Peg

# 83f4a11bf
(defn check-match
  [pat text should-match]
  (def result (peg/match pat text))
  (assert (= (not should-match) (not result))
          (string "check-match " text)))

# 798c88b4c
(defn check-deep
  [pat text what]
  (def result (peg/match pat text))
  (assert (deep= result what) (string "check-deep " text)))

# Just numbers
# 83f4a11bf
(check-match '(* 4 -1) "abcd" true)
(check-match '(* 4 -1) "abc" false)
(check-match '(* 4 -1) "abcde" false)

# Simple pattern
# 83f4a11bf
(check-match '(* (some (range "az" "AZ")) -1) "hello" true)
(check-match '(* (some (range "az" "AZ")) -1) "hello world" false)
(check-match '(* (some (range "az" "AZ")) -1) "1he11o" false)
(check-match '(* (some (range "az" "AZ")) -1) "" false)

# Pre compile
# ff0d3a008
(def pegleg (peg/compile '{:item "abc" :main (* :item "," :item -1)}))

(peg/match pegleg "abc,abc")

# Bad Grammars
# 192705113
(assert-error "peg/compile error 1" (peg/compile nil))
(assert-error "peg/compile error 2" (peg/compile @{}))
(assert-error "peg/compile error 3" (peg/compile '{:a "abc" :b "def"}))
(assert-error "peg/compile error 4" (peg/compile '(blarg "abc")))
(assert-error "peg/compile error 5" (peg/compile '(1 2 3)))

# IP address
# 40845b5c1
(def ip-address
  '{:d (range "09")
    :0-4 (range "04")
    :0-5 (range "05")
    :byte (+
            (* "25" :0-5)
            (* "2" :0-4 :d)
            (* "1" :d :d)
            (between 1 2 :d))
    :main (* :byte "." :byte "." :byte "." :byte)})

(check-match ip-address "10.240.250.250" true)
(check-match ip-address "0.0.0.0" true)
(check-match ip-address "1.2.3.4" true)
(check-match ip-address "256.2.3.4" false)
(check-match ip-address "256.2.3.2514" false)

# Substitution test with peg
# d7626f8c5
(def grammar '(accumulate (any (+ (/ "dog" "purple panda") (<- 1)))))
(defn try-grammar [text]
  (assert (= (string/replace-all "dog" "purple panda" text)
             (0 (peg/match grammar text))) text))

(try-grammar "i have a dog called doug the dog. he is good.")
(try-grammar "i have a dog called doug the dog. he is a good boy.")
(try-grammar "i have a dog called doug the do")
(try-grammar "i have a dog called doug the dog")
(try-grammar "i have a dog called doug the dogg")
(try-grammar "i have a dog called doug the doggg")
(try-grammar "i have a dog called doug the dogggg")

# Peg CSV test
# 798c88b4c
(def csv
  '{:field (+
            (* `"` (% (any (+ (<- (if-not `"` 1))
                              (* (constant `"`) `""`)))) `"`)
            (<- (any (if-not (set ",\n") 1))))
    :main (* :field (any (* "," :field)) (+ "\n" -1))})

(defn check-csv
  [str res]
  (check-deep csv str res))

(check-csv "1,2,3" @["1" "2" "3"])
(check-csv "1,\"2\",3" @["1" "2" "3"])
(check-csv ``1,"1""",3`` @["1" "1\"" "3"])

# Nested Captures
# 798c88b4c
(def grmr '(capture (* (capture "a") (capture 1) (capture "c"))))
(check-deep grmr "abc" @["a" "b" "c" "abc"])
(check-deep grmr "acc" @["a" "c" "c" "acc"])

# Functions in grammar
# 798c88b4c
(def grmr-triple ~(% (any (/ (<- 1) ,(fn [x] (string x x x))))))
(check-deep grmr-triple "abc" @["aaabbbccc"])
(check-deep grmr-triple "" @[""])
(check-deep grmr-triple " " @["   "])

(def counter ~(/ (group (any (<- 1))) ,length))
(check-deep counter "abcdefg" @[7])

# Capture Backtracking
# ff0d3a008
(check-deep '(+ (* (capture "c") "d") "ce") "ce" @[])

# Matchtime capture
# 192705113
(def scanner (peg/compile ~(cmt (capture (some 1)) ,scan-number)))

(check-deep scanner "123" @[123])
(check-deep scanner "0x86" @[0x86])
(check-deep scanner "-1.3e-7" @[-1.3e-7])
(check-deep scanner "123A" nil)

# Recursive grammars
# 170e785b7
(def g '{:main (+ (* "a" :main "b") "c")})

(check-match g "c" true)
(check-match g "acb" true)
(check-match g "aacbb" true)
(check-match g "aadbb" false)

# Back reference
# d0ec89c7c
(def wrapped-string
  ~{:pad (any "=")
    :open (* "[" (<- :pad :n) "[")
    :close (* "]" (cmt (* (-> :n) (<- :pad)) ,=) "]")
    :main (* :open (any (if-not :close 1)) :close -1)})

(check-match wrapped-string "[[]]" true)
(check-match wrapped-string "[==[a]==]" true)
(check-match wrapped-string "[==[]===]" false)
(check-match wrapped-string "[[blark]]" true)
(check-match wrapped-string "[[bl[ark]]" true)
(check-match wrapped-string "[[bl]rk]]" true)
(check-match wrapped-string "[[bl]rk]] " false)
(check-match wrapped-string "[=[bl]]rk]=] " false)
(check-match wrapped-string "[=[bl]==]rk]=] " false)
(check-match wrapped-string "[===[]==]===]" true)

(def janet-longstring
  ~{:delim (some "`")
    :open (capture :delim :n)
    :close (cmt (* (not (> -1 "`")) (-> :n) (<- (backmatch :n))) ,=)
    :main (* :open (any (if-not :close 1)) :close -1)})

(check-match janet-longstring "`john" false)
(check-match janet-longstring "abc" false)
(check-match janet-longstring "` `" true)
(check-match janet-longstring "`  `" true)
(check-match janet-longstring "``  ``" true)
(check-match janet-longstring "``` `` ```" true)
(check-match janet-longstring "``  ```" false)
(check-match janet-longstring "`a``b`" false)

# Line and column capture
# 776ce586b
(def line-col (peg/compile '(any (* (line) (column) 1))))
(check-deep line-col "abcd" @[1 1 1 2 1 3 1 4])
(check-deep line-col "" @[])
(check-deep line-col "abcd\n" @[1 1 1 2 1 3 1 4 1 5])
(check-deep line-col "abcd\nz" @[1 1 1 2 1 3 1 4 1 5 2 1])

# Backmatch
# 711fe64a5
(def backmatcher-1 '(* (capture (any "x") :1) "y" (backmatch :1) -1))

(check-match backmatcher-1 "y" true)
(check-match backmatcher-1 "xyx" true)
(check-match backmatcher-1 "xxxxxxxyxxxxxxx" true)
(check-match backmatcher-1 "xyxx" false)
(check-match backmatcher-1 (string (string/repeat "x" 73) "y") false)
(check-match backmatcher-1 (string (string/repeat "x" 10000) "y") false)
(check-match backmatcher-1 (string (string/repeat "x" 10000) "y"
                                   (string/repeat "x" 10000)) true)

(def backmatcher-2 '(* '(any "x") "y" (backmatch) -1))

(check-match backmatcher-2 "y" true)
(check-match backmatcher-2 "xyx" true)
(check-match backmatcher-2 "xxxxxxxyxxxxxxx" true)
(check-match backmatcher-2 "xyxx" false)
(check-match backmatcher-2 (string (string/repeat "x" 73) "y") false)
(check-match backmatcher-2 (string (string/repeat "x" 10000) "y") false)
(check-match backmatcher-2 (string (string/repeat "x" 10000) "y"
                                   (string/repeat "x" 10000)) true)

(def longstring-2 '(* '(some "`")
                      (some (if-not (backmatch) 1))
                      (backmatch) -1))

(check-match longstring-2 "`john" false)
(check-match longstring-2 "abc" false)
(check-match longstring-2 "` `" true)
(check-match longstring-2 "`  `" true)
(check-match longstring-2 "``  ``" true)
(check-match longstring-2 "``` `` ```" true)
(check-match longstring-2 "``  ```" false)

# Optional
# 4eeadd746
(check-match '(* (opt "hi") -1) "" true)
(check-match '(* (opt "hi") -1) "hi" true)
(check-match '(* (opt "hi") -1) "no" false)
(check-match '(* (? "hi") -1) "" true)
(check-match '(* (? "hi") -1) "hi" true)
(check-match '(* (? "hi") -1) "no" false)

# Drop
# b4934cedd
(check-deep '(drop '"hello") "hello" @[])
(check-deep '(drop "hello") "hello" @[])

# Add bytecode verification for peg unmarshaling
# e88a9af2f
# This should be valgrind clean.
(var pegi 3)
(defn marshpeg [p]
  (assert (-> p peg/compile marshal unmarshal)
          (string "peg marshal " (++ pegi))))
(marshpeg '(* 1 2 (set "abcd") "asdasd" (+ "." 3)))
(marshpeg '(% (* (+ 1 2 3) (* "drop" "bear") '"hi")))
(marshpeg '(> 123 "abcd"))
(marshpeg '{:main (* 1 "hello" :main)})
(marshpeg '(range "AZ"))
(marshpeg '(if-not "abcdf" 123))
(marshpeg '(error ($)))
(marshpeg '(* "abcd" (constant :hi)))
(marshpeg ~(/ "abc" ,identity))
(marshpeg '(if-not "abcdf" 123))
(marshpeg ~(cmt "abcdf" ,identity))
(marshpeg '(group "abc"))
(marshpeg '(sub "abcdf" "abc"))
(marshpeg '(* (sub 1 1)))
(marshpeg '(split "," (+ "a" "b" "c")))

# Peg swallowing errors
# 159651117
(assert (try (peg/match ~(/ '1 ,(fn [x] (nil x))) "x") ([err] err))
        "errors should not be swallowed")
(assert (try ((fn [x] (nil x))) ([err] err))
        "errors should not be swallowed 2")

# Check for bad memoization (+ :a) should mean different things in
# different contexts
# 8bc8709d0
(def redef-a
  ~{:a "abc"
    :c (+ :a)
    :main (* :c {:a "def" :main (+ :a)} -1)})

(check-match redef-a "abcdef" true)
(check-match redef-a "abcabc" false)
(check-match redef-a "defdef" false)

# 54a04b589
(def redef-b
  ~{:pork {:pork "beef" :main (+ -1 (* 1 :pork))}
    :main :pork})

(check-match redef-b "abeef" true)
(check-match redef-b "aabeef" false)
(check-match redef-b "aaaaaa" false)

# Integer parsing
# 45feb5548
(check-deep '(int 1) "a" @[(chr "a")])
(check-deep '(uint 1) "a" @[(chr "a")])
(check-deep '(int-be 1) "a" @[(chr "a")])
(check-deep '(uint-be 1) "a" @[(chr "a")])
(check-deep '(int 1) "\xFF" @[-1])
(check-deep '(uint 1) "\xFF" @[255])
(check-deep '(int-be 1) "\xFF" @[-1])
(check-deep '(uint-be 1) "\xFF" @[255])
(check-deep '(int 2) "\xFF\x7f" @[0x7fff])
(check-deep '(int-be 2) "\x7f\xff" @[0x7fff])
(check-deep '(uint 2) "\xff\x7f" @[0x7fff])
(check-deep '(uint-be 2) "\x7f\xff" @[0x7fff])
(check-deep '(uint-be 2) "\x7f\xff" @[0x7fff])
(when-let [u64 int/u64
           i64 int/s64]
  (check-deep '(uint 8) "\xff\x7f\x00\x00\x00\x00\x00\x00" @[(u64 0x7fff)])
  (check-deep '(int 8) "\xff\x7f\x00\x00\x00\x00\x00\x00" @[(i64 0x7fff)])
  (check-deep '(uint 7) "\xff\x7f\x00\x00\x00\x00\x00" @[(u64 0x7fff)])
  (check-deep '(int 7) "\xff\x7f\x00\x00\x00\x00\x00" @[(i64 0x7fff)]))

(check-deep '(* (int 2) -1) "123" nil)

# to/thru bug
# issue #640 - 742469a8b
(check-deep '(to -1) "aaaa" @[])
(check-deep '(thru -1) "aaaa" @[])
(check-deep ''(to -1) "aaaa" @["aaaa"])
(check-deep ''(thru -1) "aaaa" @["aaaa"])
(check-deep '(to "b") "aaaa" nil)
(check-deep '(thru "b") "aaaa" nil)

# unref
# 96513665d
(def grammar
  (peg/compile
    ~{:main (* :tagged -1)
      :tagged (unref (replace (* :open-tag :value :close-tag) ,struct))
      :open-tag (* (constant :tag) "<" (capture :w+ :tag-name) ">")
      :value (* (constant :value) (group (any (+ :tagged :untagged))))
      :close-tag (* "</" (backmatch :tag-name) ">")
      :untagged (capture (any (if-not "<" 1)))}))
(check-deep grammar "<p><em>foobar</em></p>"
            @[{:tag "p" :value @[{:tag "em" :value @["foobar"]}]}])
(check-deep grammar "<p>foobar</p>" @[{:tag "p" :value @["foobar"]}])

# Using a large test grammar
# cf05ff610
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
  [(if (or (root-env sym) (specials sym)) :coresym :symbol) text])

(def grammar
  ~{:ws (set " \v\t\r\f\n\0")
    :readermac (set "';~,")
    :symchars (+ (range "09" "AZ" "az" "\x80\xFF")
                 (set "!$%&*+-./:<?=>@^_|"))
    :token (some :symchars)
    :hex (range "09" "af" "AF")
    :escape (* "\\" (+ (set `"'0?\abefnrtvz`)
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
                 :close (cmt (* (not (> -1 "`")) (-> :n) '(backmatch :n))
                             ,=)
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

###
### Compiling brainfuck to Janet.
###
# 20d5d560f
(def- bf-peg
  "Peg for compiling brainfuck into a Janet source ast."
  (peg/compile
    ~{:+ (/ '(some "+") ,(fn [x] ~(+= (DATA POS) ,(length x))))
      :- (/ '(some "-") ,(fn [x] ~(-= (DATA POS) ,(length x))))
      :> (/ '(some ">") ,(fn [x] ~(+= POS ,(length x))))
      :< (/ '(some "<") ,(fn [x] ~(-= POS ,(length x))))
      :. (* "." (constant (prinf "%c" (get DATA POS))))
      :loop (/ (* "[" :main "]") ,(fn [& captures]
                                    ~(while (not= (get DATA POS) 0)
                                       ,;captures)))
      :main (any (+ :s :loop :+ :- :> :< :.))}))

(defn bf
  "Run brainfuck."
  [text]
  (eval
    ~(let [DATA (array/new-filled 100 0)]
       (var POS 50)
       ,;(peg/match bf-peg text))))

(defn test-bf
  "Test some bf for expected output."
  [input output]
  (def b @"")
  (with-dyns [:out b]
    (bf input))
  (assert (= (string output) (string b))
          (string "bf input '"
                  input
                  "' failed, expected "
                  (describe output)
                  ", got "
                  (describe (string b))
                  ".")))

(test-bf (string "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]"
                 ">>.>---.+++++++..+++.>>.<-.<.+++.------.--------"
                 ".>>+.>++.") "Hello World!\n")

(test-bf (string ">++++++++"
                 "[-<+++++++++>]<.>>+>-[+]++>++>+++[>[->+++<<+++>]<<]"
                 ">-----.>->+++..+++.>-.<<+[>[+>+]>>]<--------------"
                 ".>>.+++.------.--------.>+.>+.")
         "Hello World!\n")

(test-bf (string "+[+[<<<+>>>>]+<-<-<<<+<++]<<.<++.<++..+++.<<++.<---"
                 ".>>.>.+++.------.>-.>>--.")
         "Hello, World!")

# Regression test
# issue #300 - 714bd61d5
# Just don't segfault
(assert (peg/match '{:main (replace "S" {"S" :spade})} "S7")
        "regression #300")

# Lenprefix rule
# 8b5bcaee3
(def peg (peg/compile ~(* (lenprefix (/ (* '(any (if-not ":" 1)) ":")
                                        ,scan-number) 1) -1)))

(assert (peg/match peg "5:abcde") "lenprefix 1")
(assert (not (peg/match peg "5:abcdef")) "lenprefix 2")
(assert (not (peg/match peg "5:abcd")) "lenprefix 3")

# Packet capture
# 8b5bcaee3
(def peg2
  (peg/compile
    ~{# capture packet length in tag :header-len
      :packet-header (* (/ ':d+ ,scan-number :header-len) ":")

      # capture n bytes from a backref :header-len
      :packet-body '(lenprefix (-> :header-len) 1)

      # header, followed by body, and drop the :header-len capture
      :packet (/ (* :packet-header :packet-body) ,|$1)

      # any exact sequence of packets (no extra characters)
      :main (* (any :packet) -1)}))

(assert (deep= @["a" "bb" "ccc"] (peg/match peg2 "1:a2:bb3:ccc"))
        "lenprefix 4")
(assert (deep= @["a" "bb" "cccccc"] (peg/match peg2 "1:a2:bb6:cccccc"))
        "lenprefix 5")
(assert (= nil (peg/match peg2 "1:a2:bb:5:cccccc")) "lenprefix 6")
(assert (= nil (peg/match peg2 "1:a2:bb:7:cccccc")) "lenprefix 7")

# Issue #412
# 677737d34
(assert (peg/match '(* "a" (> -1 "a") "b") "abc")
        "lookhead does not move cursor")

# 6d096551f
(def peg3
  ~{:main (* "(" (thru ")"))})

(def peg4 (peg/compile ~(* (thru "(") '(to ")"))))

(assert (peg/match peg3 "(12345)") "peg thru 1")
(assert (not (peg/match peg3 " (12345)")) "peg thru 2")
(assert (not (peg/match peg3 "(12345")) "peg thru 3")

(assert (= "abc" (0 (peg/match peg4 "123(abc)"))) "peg thru/to 1")
(assert (= "abc" (0 (peg/match peg4 "(abc)"))) "peg thru/to 2")
(assert (not (peg/match peg4 "123(abc")) "peg thru/to 3")

# 86e12369b
(def peg5 (peg/compile [3 "abc"]))

(assert (:match peg5 "abcabcabc") "repeat alias 1")
(assert (:match peg5 "abcabcabcac") "repeat alias 2")
(assert (not (:match peg5 "abcabc")) "repeat alias 3")

# Peg find and find-all
# c26f57362
(def p "/usr/local/bin/janet")
(assert (= (peg/find '"n/" p) 13) "peg find 1")
(assert (not (peg/find '"t/" p)) "peg find 2")
(assert (deep= (peg/find-all '"/" p) @[0 4 10 14]) "peg find-all")

# Peg replace and replace-all
# e548e1f6e
(defn check-replacer
  [x y z]
  (assert (= (string/replace x y z) (string (peg/replace x y z)))
          "replacer test replace")
  (assert (= (string/replace-all x y z) (string (peg/replace-all x y z)))
          "replacer test replace-all"))
(check-replacer "abc" "Z" "abcabcabcabasciabsabc")
(check-replacer "abc" "Z" "")
(check-replacer "aba" "ZZZZZZ" "ababababababa")
(check-replacer "aba" "" "ababababababa")

# 485099fd6
(check-replacer "aba" string/ascii-upper "ababababababa")
(check-replacer "aba" 123 "ababababababa")
(assert (= (string (peg/replace-all ~(set "ab") string/ascii-upper "abcaa"))
           "ABcAA")
        "peg/replace-all cfunction")
(assert (= (string (peg/replace-all ~(set "ab") |$ "abcaa"))
           "abcaa")
        "peg/replace-all function")

# 9dc7e8ed3
(defn peg-test [name f peg subst text expected]
  (assert (= (string (f peg subst text)) expected) name))

(peg-test "peg/replace has access to captures"
  peg/replace
  ~(sequence "." (capture (set "ab")))
  (fn [str char] (string/format "%s -> %s, " str (string/ascii-upper char)))
  ".a.b.c"
  ".a -> A, .b.c")

(peg-test "peg/replace-all has access to captures"
  peg/replace-all
  ~(sequence "." (capture (set "ab")))
  (fn [str char] (string/format "%s -> %s, " str (string/ascii-upper char)))
  ".a.b.c"
  ".a -> A, .b -> B, .c")

# Peg bug
# eab5f67c5
(assert (deep= @[] (peg/match '(any 1) @"")) "peg empty pattern 1")
(assert (deep= @[] (peg/match '(any 1) (buffer))) "peg empty pattern 2")
(assert (deep= @[] (peg/match '(any 1) "")) "peg empty pattern 3")
(assert (deep= @[] (peg/match '(any 1) (string))) "peg empty pattern 4")
(assert (deep= @[] (peg/match '(* "test" (any 1)) @"test"))
        "peg empty pattern 5")
(assert (deep= @[] (peg/match '(* "test" (any 1)) (buffer "test")))
        "peg empty pattern 6")

# number pattern
# cccbdc164
(assert (deep= @[111] (peg/match '(number :d+) "111"))
        "simple number capture 1")
(assert (deep= @[255] (peg/match '(number :w+) "0xff"))
        "simple number capture 2")

# Marshal and unmarshal pegs
# 446ab037b
(def p (-> "abcd" peg/compile marshal unmarshal))
(assert (peg/match p "abcd") "peg marshal 1")
(assert (peg/match p "abcdefg") "peg marshal 2")
(assert (not (peg/match p "zabcdefg")) "peg marshal 3")

# to/thru bug
# issue #971 - a895219d2
(def pattern
  (peg/compile
    '{:dd (sequence :d :d)
      :sep (set "/-")
      :date (sequence :dd :sep :dd)
      :wsep (some (set " \t"))
      :entry (group (sequence (capture :date) :wsep (capture :date)))
      :main (some (thru :entry))}))

(def alt-pattern
  (peg/compile
    '{:dd (sequence :d :d)
      :sep (set "/-")
      :date (sequence :dd :sep :dd)
      :wsep (some (set " \t"))
      :entry (group (sequence (capture :date) :wsep (capture :date)))
      :main (some (choice :entry 1))}))

(def text "1800-10-818-9-818 16/12\n17/12 19/12\n20/12 11/01")
(assert (deep= (peg/match pattern text) (peg/match alt-pattern text))
        "to/thru bug #971")

# 14657a7
(def- sym-prefix-peg
  (peg/compile
    ~{:symchar (+ (range "\x80\xff" "AZ" "az" "09")
                  (set "!$%&*+-./:<?=>@^_"))
      :anchor (drop (cmt ($) ,|(= $ 0)))
      :cap (* (+ (> -1 (not :symchar)) :anchor) (* ($) '(some :symchar)))
      :recur (+ :cap (> -1 :recur))
      :main (> -1 :recur)}))

(assert (deep= (peg/match sym-prefix-peg @"123" 3) @[0 "123"])
        "peg lookback")
(assert (deep= (peg/match sym-prefix-peg @"1234" 4) @[0 "1234"])
        "peg lookback 2")

# issue #1027 - 356b39c6f
(assert (deep= (peg/replace-all '(* (<- 1) 1 (backmatch))
                                "xxx" "aba cdc efa")
               @"xxx xxx efa")
        "peg replace-all 1")

# issue #1026 - 9341081a4
(assert (deep=
  (peg/match '(not (* (constant 7) "a")) "hello")
  @[]) "peg not")

(assert (deep=
  (peg/match '(if-not (* (constant 7) "a") "hello") "hello")
  @[]) "peg if-not")

(assert (deep=
  (peg/match '(if-not (drop (* (constant 7) "a")) "hello") "hello")
  @[]) "peg if-not drop")

(assert (deep=
  (peg/match '(if (not (* (constant 7) "a")) "hello") "hello")
  @[]) "peg if not")

(defn test [name peg input expected]
  (assert-no-error "compile peg" (peg/compile peg))
  (assert-no-error "marshal/unmarshal peg" (-> peg marshal unmarshal))
  (assert (deep= (peg/match peg input) expected) name))

(test "sub: matches the same input twice"
  ~(sub "abcd" "abc")
  "abcdef"
  @[])

(test "sub: second pattern cannot match more than the first pattern"
  ~(sub "abcd" "abcde")
  "abcdef"
  nil)

(test "sub: fails if first pattern fails"
  ~(sub "x" "abc")
  "abcdef"
  nil)

(test "sub: fails if second pattern fails"
  ~(sub "abc" "x")
  "abcdef"
  nil)

(test "sub: keeps captures from both patterns"
  ~(sub '"abcd" '"abc")
  "abcdef"
  @["abcd" "abc"])

(test "sub: second pattern can reference captures from first"
  ~(* (constant 5 :tag) (sub (capture "abc" :tag) (backref :tag)))
  "abcdef"
  @[5 "abc" "abc"])

(test "sub: second pattern can't see past what the first pattern matches"
  ~(sub "abc" (* "abc" -1))
  "abcdef"
  @[])

(test "sub: positions inside second match are still relative to the entire input"
  ~(* "one\ntw" (sub "o" (* ($) (line) (column))))
  "one\ntwo\nthree\n"
  @[6 2 3])

(test "sub: advances to the end of the first pattern's match"
  ~(* (sub "abc" "ab") "d")
  "abcdef"
  @[])

(test "split: basic functionality"
  ~(split "," '1)
  "a,b,c"
  @["a" "b" "c"])

(test "split: drops captures from separator pattern"
  ~(split '"," '1)
  "a,b,c"
  @["a" "b" "c"])

(test "split: can match empty subpatterns"
  ~(split "," ':w*)
  ",a,,bar,,,c,,"
  @["" "a" "" "bar" "" "" "c" "" ""])

(test "split: subpattern is limited to only text before the separator"
  ~(split "," '(to -1))
  "a,,bar,c"
  @["a" "" "bar" "c"])

(test "split: fails if any subpattern fails"
  ~(split "," '"a")
  "a,a,b"
  nil)

(test "split: separator does not have to match anything"
  ~(split "x" '(to -1))
  "a,a,b"
  @["a,a,b"])

(test "split: always consumes entire input"
  ~(split 1 '"")
  "abc"
  @["" "" "" ""])

(test "split: separator can be an arbitrary PEG"
  ~(split :s+ '(to -1))
  "a   b      c"
  @["a" "b" "c"])

(test "split: does not advance past the end of the input"
  ~(* (split "," ':w+) 0)
  "a,b,c"
  @["a" "b" "c"])

(test "nth 1"
  ~{:prefix (number :d+ nil :n)
    :word '(lenprefix (-> :n) :w)
    :main (some (nth 1 (* :prefix ":" :word)))}
  "5:apple6:banana6:cherry"
  @["apple" "banana" "cherry"])

(test "only-tags 1"
  ~{:prefix (number :d+ nil :n)
    :word (capture (lenprefix (-> :n) :w) :W)
    :main (some (* (only-tags (* :prefix ":" :word)) (-> :W)))}
  "5:apple6:banana6:cherry"
  @["apple" "banana" "cherry"])

# Issue #1539 - make sure split with "" doesn't infinite loop/oom
(test "issue 1539"
      ~(split "" (capture (to -1)))
      "hello there friends"
      nil)

(test "issue 1539 pt. 2"
  ~(split "," (capture 0))
  "abc123,,,,"
  @["" "" "" "" ""])

(end-suite)

