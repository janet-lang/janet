(var *sourcefile* stdin)

(var *compile* (fn [x]
	(def ret (compile x))
	(if (= :function (type ret))
		ret
		(error (string "compile error: " ret)))))

(var *read* (fn []
	(def b (buffer))
	(def p (parser))
	(while (not (parse-hasvalue p))
		(read *sourcefile* 1 b)
		(if (= (length b) 0)
			(error "unexpected end of source"))
		(parse-charseq p b)
		(clear b))
	(parse-consume p)))

(def eval (fn [x]
	(apply (*compile* x) 123)))

(def t (thread (fn []
	(while true
		(eval (*read*))))))
(print (tran t))
