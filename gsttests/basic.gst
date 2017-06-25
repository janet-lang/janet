(print "Running basic tests...")
(def assert (fn [x e] (if x (do (write stdout ".") x) (do (print e) (exit 1)))))

(assert (= 10 (+ 1 2 3 4)) "addition")
(assert (= -8 (- 1 2 3 4)) "subtraction")
(assert (= 24 (* 1 2 3 4)) "multiplication")
(assert (= 4 (blshift 1 2)) "left shift")
(assert (= 1 (brshift 4 2)) "right shift")

(var accum 1)
(var count 0)
(while (< count 16)
	(varset accum (blshift accum 1))
	(varset count (+ 1 count)))

(assert (= accum 65536) "loop")

(print)
(exit 0)