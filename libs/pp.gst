# Pretty print

# Declare pretty print
(: pp nil)

# Pretty print an array or tuple
(: print-seq (fn [start end a seen]
    (: seen (if seen seen {}))
    (if (get seen a) (get seen a)
        (do
            (: parts [])
            (: len (length a))
            (: i 0)
            (while (< i len)
                (push! parts (pp (get a i) seen))
                (push! parts " ")
                (: i (+ 1 i)))
            (if (> len 0) (pop! parts))
            (push! parts end)
            (: ret (apply string start parts))
            (set! seen a ret)
            ret))))

# Pretty print an object or struct
(: print-struct (fn [start end s seen]
    (: seen (if seen seen {}))
    (if (get seen s) (get seen s)
        (do
            (: parts [])
            (: key (next s))
            (while (not (= key nil))
                    (push! parts (pp key seen))
                    (push! parts " ")
                    (push! parts (pp (get s key) seen))
                    (push! parts " ")
                    (: key (next s key)))
            (if (> (length parts) 0) (pop! parts))
            (push! parts end)
            (: ret (apply string start parts))
            (set! seen s ret)
            ret))))

# Type handlers
(: handlers {
    "array" (fn [a seen] (print-seq "[" "]" a seen))
    "tuple" (fn [a seen] (print-seq "(" ")" a seen))
    "table" (fn [s seen] (print-struct "{" "}" s seen))
    "struct" (fn [s seen] (print-struct "#{" "}" s seen))
 })

# Define pretty print
(: pp (fn [x seen]
    (: h (get handlers (type x)))
    ((if h h tostring) x seen)))

# (print (pp [1 {4 5 6 7} 2 3]))
