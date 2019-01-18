# Copyright (C) Calvin Rose 2019
#
# Takes in a janet string and colorizes for multiple
# output formats.

# Constants for checking if symbols should be
# highlighted.
(def- core-env (table/getproto *env*))
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

(defn check-number [text] (and (scan-number text) text))

(defn- make-grammar
  "Creates the grammar based on the paint function, which
  colorizes fragments of text."
  [paint]

  (defn <-c
    "Peg rule for capturing and coloring a rule."
    [color what]
    ~(/ (<- ,what) ,(partial paint color)))

  (defn color-symbol
    "Color a symbol only if it is a core library binding or special."
    [text]
    (def sym (symbol text))
    (def should-color (or (specials sym) (core-env sym)))
    (paint (if should-color :coresym :symbol) text))

  ~{:ws (set " \t\r\f\n\0")
    :readermac (set "';~,")
    :symchars (+ (range "09" "AZ" "az" "\x80\xFF") (set "$%&*+-./:<=>?@^_|"))
    :token (some :symchars)
    :hex (range "09" "af" "AF")
    :escape (* "\\" (+ (set "ntrzf0\"\\e") 
                       (* "x" :hex :hex) 
                       (error (constant "bad hex escape"))))

    :comment ,(<-c :comment ~(* "#" (any (if-not (+ "\n" -1) 1))))

    :symbol (/ ':token ,color-symbol)
    :keyword ,(<-c :keyword ~(* ":" (any :symchars)))
    :constant ,(<-c :constant ~(+ "true" "false" "nil"))
    :bytes (* "\"" (any (+ :escape (if-not "\"" 1))) "\"")
    :string ,(<-c :string :bytes)
    :buffer ,(<-c :string ~(* "@" :bytes))
    :long-bytes {:delim (some "`")
                 :open (capture :delim :n)
                 :close (cmt (* (not (> -1 "`")) (-> :n) ':delim) ,=)
                 :main (drop (* :open (any (if-not :close 1)) :close))}
    :long-string ,(<-c :string :long-bytes)
    :long-buffer ,(<-c :string ~(* "@" :long-bytes))
    :number (/ (cmt ':token ,check-number) ,(partial paint :number))

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

    :main (+ (% :root) (error ""))})

# Terminal syntax highlighting

(def- terminal-colors
  {:number 32
   :keyword 33
   :string 35
   :coresym 31
   :constant 34
   :comment 36})

(defn- terminal-paint
  "Paint colors for ansi terminals"
  [what str]
  (def code (get terminal-colors what))
  (if code (string "\e[" code "m" str "\e[0m") str))

# HTML syntax highlighting

(def- html-colors
  {:number "j-number"
   :keyword "j-keyword"
   :string "j-string"
   :coresym "j-coresym"
   :constant "j-constant"
   :comment "j-comment"
   :line "j-line"})

(def- escapes
  {38 "&amp;"
   60 "&lt;"
   62 "&gt;"
   34 "&quot;"
   39 "&#39;"
   47 "&#47;"})

(def html-style
  "Style tag to add to a page to highlight janet code"
```
<style type="text/css">
.j-main { color: white; background: #111; font-size: 1.4em; }
.j-number { color: #89dc76; }
.j-keyword { color: #ffd866; }
.j-string { color: #ab90f2; }
.j-coresym { color: #ff6188; }
.j-constant { color: #fc9867; }
.j-comment { color: darkgray; }
.j-line { color: gray; }
</style>
```)

(defn- html-escape
  "Escape special characters for HTML encoding."
  [str]
  (def buf @"")
  (loop [byte :in str]
    (if-let [rep (get escapes byte)]
      (buffer/push-string buf rep)
      (buffer/push-byte buf byte)))
  buf)

(defn- html-paint
  "Paint colors for HTML"
  [what str]
  (def color (get html-colors what))
  (def escaped (html-escape str))
  (if color
    (string "<span class=\"" color "\">" escaped "</span>")
    escaped))

# Create Pegs

(def- terminal-grammar (peg/compile (make-grammar terminal-paint)))
(def- html-grammar (peg/compile (make-grammar html-paint)))

# API

(defn ansi
  "Highlight janet source code ANSI Termianl escape colors."
  [source]
  (0 (peg/match terminal-grammar source)))

(defn html
  "Highlight janet source code and output HTML."
  [source]
  (string "<pre class=\"j-main\">"
          (0 (peg/match html-grammar source))
          "</pre>"))

(defn html-file
  "Highlight a janet file and print out a highlighted HTML version
  of the file. Must provide a default title when creating the file."
  [in-path out-path title &]
  (default title in-path)
  (def f (file/open in-path :r))
  (def source (file/read f :all))
  (file/close f)
  (def markup (0 (peg/match html-grammar source)))
  (def out (file/open out-path :w))
  (file/write out
              "<!doctype html><html><head><meta charset=\"UTF-8\">"
              html-style
              "<title>"
              title
              "</title></head>"
              "<body class=\"j-main\"><pre>"
              markup
              "</pre></body></html>")
  (file/close out))

(defn ansi-file
  "Highlight a janet file and print the highlighted output to stdout."
  [in-path]
  (def f (file/open in-path :r))
  (def source (file/read f :all))
  (file/close f)
  (def markup (0 (peg/match terminal-grammar source)))
  (print markup))
