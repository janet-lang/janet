# Pretty print

# Reindent a function to be more deeply indented
(: reindent (fn [x] x))

# Pretty print an array
(: print-array (fn [a]
    (: parts [])
    (: l (length a))
    (: i 0)
    (while (< i l)
         
        (: i (+ 1 i)))
    (apply strcat "[ " parts)))

(: handler {
    "number" tostring
    "nil" tostring
    "boolean" tostring
    "userdata" tostring
    "cfunction" tostring
    "function" tostring
    "string" tostring # change to unquote string
    "buffer" tostring
    "array" tostring
    "tuple" tostring
    "object" tostring
    "struct" tostring
    "thread" tostring 
 })
