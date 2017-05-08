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

"Run a simple repl. Does not handle errors and other
such details."
(while 1
    (write stdout ">> ")
    (: line (readline))
    (while line
        (: line (parse-charseq p line))
        (if (parse-hasvalue p)
            (print ((compile (parse-consume p)))))))
