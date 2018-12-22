# Helper to generate core library mappings for janet

(def allsyms (all-symbols))

(def- escapes
  {(get "|" 0) `\|`
   (get "-" 0) `\-`
   (get "+" 0) `\+`
   (get "*" 0) `\*`
   (get "^" 0) `\^`
   (get "$" 0) `\$`
   (get "?" 0) `\?`
   38 "&amp;"
   60 "&lt;"
   62 "&gt;"
   34 "&quot;"
   39 "&#39;"
   47 "&#47;"})

(defn- escape
  "Escape special characters for HTML and regex encoding."
  [str]
  (def buf @"")
  (loop [byte :in str]
    (if-let [rep escapes.byte]
      (buffer/push-string buf rep)
      (buffer/push-byte buf byte)))
  buf)

(print (string/join (map escape allsyms) "|"))
