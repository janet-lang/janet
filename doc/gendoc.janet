# Generate documentation

(def- prelude
```
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Janet Language Documentation</title>
<meta name="description" content="API Documentation for the janet programming language.">
<style>
p {
  font-family: monospace;
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

(defn- emit-item
  "Generate documentation for one entry."
  [key env-entry]
  (let [{:macro macro
         :value val
         :ref ref
         :doc docstring} env-entry
        binding-type (cond
                       macro :macro
                       ref (string :var " (" (type ref.0) ")")
                       (type val))]
    (string "<h2>" (html-escape key) "</h2>"
            "<span style=\"color:blue;\">" binding-type "</span>"
            "<p>" (trim-lead (html-escape docstring)) "</p>")))

# Generate parts and print them to stdout
(def parts (seq [[k entry]
                 :in (sort (pairs (table/getproto _env)))
                 :when (and entry:doc (not entry:private))]
                (emit-item k entry)))
(print prelude ;(interpose "<hr>" parts) postlude)
