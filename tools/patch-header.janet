# Patch janet.h

(def defn :macro
   ```
  (defn name & more)

  Define a function. Equivalent to `(def name (fn name [args] ...))`.
  ```
  (fn defn [name & more]
    (def len (length more))
    (def modifiers @[])
    (var docstr "")
    (def fstart
      (fn recur [i]
        (def {i ith} more)
        (def t (type ith))
        (if (= t :tuple)
          i
          (do
            (if (= t :string)
              (set docstr ith)
              (array/push modifiers ith))
            (if (< i len) (recur (+ i 1)))))))
    (def start (fstart 0))
    (def args (in more start))
    # Add function signature to docstring
    (var index 0)
    (def arglen (length args))
    (def buf (buffer "(" name))
    (while (< index arglen)
      (buffer/push-string buf " ")
      (buffer/format buf "%j" (in args index))
      (set index (+ index 1)))
    (array/push modifiers (string buf ")\n\n" docstr))
    # Build return value
    ~(def ,name ,;modifiers (fn ,name ,;(tuple/slice more start)))))

(defn defmacro :macro
   "Define a macro."
  [name & more]
  (setdyn name @{}) # override old macro definitions in the case of a recursive macro
  (apply defn name :macro more))

(defmacro if-not
  "Shorthand for `(if (not condition) else then)`."
  [condition then &opt else]
  ~(if ,condition ,else ,then))

(defn slurp
  ``Read all data from a file with name `path` and then close the file.``
  [path]
  (def f (file/open path :rb))
  (if-not f (error (string "could not open file " path)))
  (def contents (file/read f :all))
  (file/close f)
  contents)

(defn spit
  ``Write `contents` to a file at `path`. Can optionally append to the file.``
  [path contents &opt mode]
  (def mode :wb)
  (def f (file/open path mode))
  (if-not f (error (string "could not open file " path " with mode " mode)))
  (file/write f contents)
  (file/close f)
  nil)

(def [_ _ _ _ janeth janetconf output] boot/args)
(spit output (string/replace `#include "janetconf.h"` (slurp janetconf) (slurp janeth)))
