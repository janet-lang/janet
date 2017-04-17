(: f (fn [x] (strcat (tostring x) "-abumba")))

(: i 100)
(while (> i 0) (print i) (: i (- i 1)))

(print (strcat (tostring (+ 1 2 3)) 'a 'b (f 'c)))
