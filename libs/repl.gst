(namespace-set! "gst.repl")

"Hold all compile time evaluators"
(export! "evaluators" {})

"Read a line"
(export! "readline" (fn []
    (: b (buffer))
    (read stdin 1 b)
    (while (not (= (get "\n" 0) (get b (- (length b) 1))))
        (read stdin 1 b)
    )
    (string b)
))

"Create a parser"
(export! "p" (parser))

(while 1
    (: line (readline))
    (print "Read line: " line)
)
