# Generate documentation

(def- prelude
  ```
  <!doctype html>
  <html lang="en">
  <head>
  <meta charset="utf-8">
  <title>Janet Language Documentation</title>
  <meta name="description" content="API Documentation for the janet programming language.">
  </head>
  ```)

(def- postlude
  ```
  </html>
  ```)

(def- escapes
  {10 "<br>"
   09 "&nbsp;&nbsp;&nbsp;&nbsp;"
   38 "&amp;"
   60 "&lt;"
   62 "&gt;"
   34 "&quot;"
   39 "&#39;"
   47 "&#47;"})

(defn- trim-lead
  "Trim leading newlines"
  [str]
  (var i 0)
  (while (= 10 str.i) (++ i))
  (string/slice str i))

(defn- html-escape
  "Escape special characters for HTML encoding."
  [str]
  (def buf @"")
  (loop [byte :in str]
    (if-let [rep escapes.byte]
      (buffer/push-string buf rep)
      (buffer/push-byte buf byte)))
  buf)

(defn- gen-one
  "Generate documentation for a binding. Returns an
  html fragment."
  [key docstring]
  (if-let [index (string/find "\n" docstring)]
    (let [first-line (html-escape (string/slice docstring 0 index))
          rest (html-escape (trim-lead (string/slice docstring (inc index))))]
      (string "<h2>" first-line "</h2>\n"
              "<p>" rest "</p>\n"))
    (string "<h2>" (html-escape key) "</h2>\n"
            "<p>" (html-escape docstring) "</p>\n")))

# Generate parts and print them to stdout
(def parts (seq [[k {:doc d :private p}]
                 :in (sort (pairs (table/getproto _env)))
                 :when (and d (not p))]
                (gen-one k d)))
(print prelude ;parts postlude)
