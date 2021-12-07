(def f @{})
(var collisions 0)
(loop [x :range [0 300] y :range [0 300]]  
  (def key (hash (+ (* x 1000) y))) 
  (if (in f key) 
    (++ collisions)) 
  (put f key true))
(print "ints 1 collisions: " collisions)

(def f @{})
(var collisions 0)
(loop [x :range [100000 101000] y :range [100000 101000]]
  (def key (hash [x y]))
  (if (in f key) (++ collisions))
  (put f key true))
(print "int pair 1 collisions: " collisions)

(def f @{})
(var collisions 0)
(loop [x :range [10000 11000] y :range [10000 11000]]
  (def key (hash [x y]))
  (if (in f key) (++ collisions))
  (put f key true))
(print "int pair 2 collisions: " collisions)
