(print "\nRunning basic tests...\n")

(var num-tests-passed 0)
(var num-tests-run 0)
(def assert (fn [x e]
	(varset! num-tests-run (+ 1 num-tests-run))
	(if x
		(do
			(print "  \e[32m✔\e[0m" e)
			(varset! num-tests-passed (+ 1 num-tests-passed))
			x)
		(do
			(print "  \e[31m✘\e[0m" e)
			x))))

(assert (= 10 (+ 1 2 3 4)) "addition")
(assert (= -8 (- 1 2 3 4)) "subtraction")
(assert (= 24 (* 1 2 3 4)) "multiplication")
(assert (= 4 (blshift 1 2)) "left shift")
(assert (= 1 (brshift 4 2)) "right shift")
(assert (< 1 2 3 4 5 6) "less than integers")
(assert (< 1.0 2.0 3.0 4.0 5.0 6.0) "less than reals")
(assert (> 6 5 4 3 2 1) "greater than integers")
(assert (> 6.0 5.0 4.0 3.0 2.0 1.0) "greater than reals")
(assert (<= 1 2 3 3 4 5 6) "less than or equal to integers")
(assert (<= 1.0 2.0 3.0 3.0 4.0 5.0 6.0) "less than or equal to reals")
(assert (>= 6 5 4 4 3 2 1) "greater than or equal to integers")
(assert (>= 6.0 5.0 4.0 4.0 3.0 2.0 1.0) "greater than or equal to reals")

(assert (< nil 1.0 1 false true "hi"
	(array 1 2 3)
	(tuple 1 2 3)
	(table "a" "b" "c" false)
	(struct 1 2)
	(thread (fn [x] x))
	(buffer "hi")
	(fn [x] (+ x x))
	+) "type ordering")

(assert (not false) "false literal")
(assert true "true literal")
(assert (not nil) "nil literal")
(assert (= 7 (bor 3 4)) "bit or")
(assert (= 0 (band 3 4)) "bit and")

(var accum 1)
(var count 0)
(while (< count 16)
	(varset! accum (blshift accum 1))
	(varset! count (+ 1 count)))

(assert (= accum 65536) "loop")

(assert (= (struct 1 2 3 4 5 6 7 8) (struct 7 8 5 6 3 4 1 2)) "struct order does not matter")

# Serialization tests

(def scheck (fn [x]
    (def dat (serialize x))
    (def deser (deserialize dat))
    (assert (= x deser) (string "serialize " (description x)))
))

(scheck 1)
(scheck true)
(scheck false)
(scheck nil)
(scheck "asdasdasd")
(scheck (struct 1 2 3 4))
(scheck (tuple 1 2 3))
(scheck 123412.12)
(scheck (struct (struct 1 2 3 "a") (struct 1 2 3 "a") false 1 "asdasd" (tuple "a" "b")))
(scheck "qwertyuiopasdfghjklzxcvbnm123456789")
(scheck "qwertyuiopasdfghjklzxcvbnm1234567890!@#$%^&*()")

(def athread (thread (fn [x]
	(error (string "hello, " x)))))

(def athread-result (tran athread "world!"))

(assert (= athread-result "hello, world!") "thread error result")
(assert (= (status athread) "error") "thread error status")

# yield tests

(def t (thread (fn [] (tran nil 1) (tran nil 2) 3)))

(assert (= 1 (tran t)) "initial transfer to new thread")
(assert (= 2 (tran t)) "second transfer to thread")
(assert (= 3 (tran t)) "return from thread")
(assert (= (status t) "dead") "finished thread is dead")

# report

(print num-tests-passed "of" num-tests-run "tests passed")