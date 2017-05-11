(namespace-set! "gst.repl")

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

"Run a simple repl."
(while 1
    (write stdout ">> ")
    (: t (thread (fn [line]
        (: ret 1)
        (while line
            (: line (parse-charseq p line))
            (if (parse-hasvalue p)
                (: ret ((compile (parse-consume p))))))
        ret)))
    (: res (tran t (readline)))
    (if (= (status t) "dead") (print (debugp res)) (print "Error: " (debugp res))))
