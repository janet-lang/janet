# Pretty print

# Declare pretty print
(: pp nil)

# Pretty print an array or tuple
(: print-seq (fn [start end a]
    (: parts [])
    (: len (length a))
    (: i 0)
    (while (< i len)
        (push parts (pp (rawget a i)))
        (push parts " ")
        (: i (+ 1 i)))
    (if (> len 0) (pop parts))
    (push parts end)
    (apply strcat start parts)))

# Pretty print an object or struct
(: print-struct (fn [start end s]
    (: parts [])
    (: key (next s))
    (while (not (= key nil))
        (push parts (pp key))
        (push parts " ")
        (push parts (pp (rawget s key)))
        (push parts " ")
        (: key (next s key)))
    (if (> (length parts) 0) (pop parts))
    (push parts end)
    (apply strcat start parts)))

# Pretty 

(: handlers {
    "real" tostring
    "integer" tostring
    "nil" tostring
    "boolean" tostring
    "userdata" tostring
    "cfunction" tostring
    "function" tostring
    "string" tostring
    "buffer" tostring
    "array" (fn [a] (print-seq "[" "]" a))
    "tuple" (fn [a] (print-seq "(" ")" a))
    "object" (fn [s] (print-struct "{" "}" s))
    "struct" (fn [s] (print-struct "#{" "}" s))
    "thread" tostring 
 })

# Define pretty print
(: pp (fn [x]
    (: h (rawget handlers (type x)))
    (h x)))

(print (pp [1 {4 5 6 7} 2 3]))

(print "DONE!")
