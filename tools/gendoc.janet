# Generate documentation

# TODO - make tool reusable

(def- prelude
```
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Janet Language Documentation</title>
<meta name="description" content="API Documentation for the janet programming language.">
<style>
.docstring {
  font-family: monospace;
}
.binding-type {
  color: blue;
}
.source-map {
  color: steelblue;
  font-size: 0.8em;
}
</style>
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

(defn- make-title
  "Generate title"
  []
  (string "<h1>Janet Core API</h1>"
          "<p>Version " janet/version  "-" janet/build "</p>"
          "<p>Generated "
          (string/number (os/time) :f 0 20)
          " seconds after epoch</p>"
          "<hr>"))

(defn- emit-item
  "Generate documentation for one entry."
  [key env-entry]
  (let [{:macro macro
         :value val
         :ref ref
         :source-map sm
         :doc docstring} env-entry
        binding-type (cond
                       macro :macro
                       ref (string :var " (" (type (get ref 0)) ")")
                       (type val))
        source-ref (if-let [[path start end] sm]
                     (string "<span class=\"source-map\">" path " (" start ":" end ")</span>")
                     "")]
    (string "<h2 class=\"binding\">" (html-escape key) "</h2>\n"
            "<span class=\"binding-type\">" binding-type "</span>\n"
            "<p class=\"docstring\">" (trim-lead (html-escape docstring)) "</p>\n"
            source-ref)))

# Generate parts and print them to stdout
(def parts (seq [[k entry]
                 :in (sort (pairs (table/getproto _env)))
                 :when (and entry:doc (not entry:private))]
                (emit-item k entry)))
(print
  prelude
  (make-title)
  ;(interpose "<hr>\n" parts)
  postlude)
