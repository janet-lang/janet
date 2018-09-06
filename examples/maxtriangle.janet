# Find the maximum path from the top (root)
# of the triangle to the leaves of the triangle.

(defn myfold [xs ys]
  (let [xs1 (tuple.prepend xs 0)
        xs2 (tuple.append xs 0)
        m1 (map + xs1 ys)
        m2 (map + xs2 ys)]
    (map max m1 m2)))

(defn maxpath [t]
  (extreme > (reduce myfold () t)))

# Test it
# Maximum path is 3 -> 10 -> 3 -> 9 for a total of 25

(def triangle 
  '[[3]
    [7 10]
    [4 3 7]
    [8 9 1 3]
    ])

(print (maxpath triangle))
