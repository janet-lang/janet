# A flexible templater for janet. Compiles
# templates to janet functions that produce buffers.

(defn template
  "Compile a template string into a function"
  [source]

  # State for compilation machine
  (def p (parser/new))
  (def forms @[])

  (defn parse-chunk
    "Parse a string and push produced values to forms."
    [chunk]
    (parser/consume p chunk)
    (while (parser/has-more p)
      (array/push forms (parser/produce p)))
    (if (= :error (parser/status p))
      (error (parser/error p))))

  (defn code-chunk
    "Parse all the forms in str and return them
    in a tuple prefixed with 'do."
    [str]
    (parse-chunk str)
    true)

  (defn string-chunk
    "Insert string chunk into parser"
    [str]
    (parser/insert p str)
    (parse-chunk "")
    true)

  # Run peg
  (def grammar 
    ~{:code-chunk (* "{%" (drop (cmt '(any (if-not "%}" 1)) ,code-chunk)) "%}")
      :main-chunk (drop (cmt '(any (if-not "{%" 1)) ,string-chunk))
      :main (any (+ :code-chunk :main-chunk (error "")))})
  (def parts (peg/match grammar source))

  # Check errors in template and parser
  (unless parts (error "invalid template syntax"))
  (parse-chunk "\n")
  (case (parser/status p)
    :pending (error (string "unfinished parser state " (parser/state p)))
    :error (error (parser/error p)))

  # Make ast from forms
  (def ast ~(fn [params &] (default params @{}) (,buffer ;forms)))

  (def ctor (compile ast *env* source))
  (if-not (function? ctor)
    (error (string "could not compile template")))
  (ctor))
