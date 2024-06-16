(import dep1)
(import dep2)

(defn myfn
  [x]
  (def y (dep2/function x))
  (dep1/function y))
