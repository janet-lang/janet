(import build/numarray)

(def a (numarray/new 30))
(print (get a 20))
(print (a 20))

(put a 5 3.14)
(print (a 5))
(set (a 5) 100)
(print (a 5))

# (numarray/scale a 5))
# ((a :scale) a 5)
(:scale a 5)
(for i 0 10 (print (a i)))

(print "sum=" (:sum a))
