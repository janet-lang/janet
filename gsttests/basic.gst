# Define assert
(: assert (fn [x e] (if x x (do (print e) (exit 1)))))

# Basic Math
(assert (= 10 (+ 1 2 3 4), "addition")
(assert (= -8 (- 1 2 3 4), "subtraction")
(assert (= 24 (* 1 2 3 4), "multiplication")
(assert (= 0.1 (/ 1 10), "division")

# All good
(exit 0)
