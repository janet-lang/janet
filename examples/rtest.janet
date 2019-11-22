# How random is the RNG really?

(def counts (seq [_ :range [0 100]] 0))

(for i 0 1000000
  (let [x (math/random)
        intrange (math/floor (* 100 x))
        oldcount (counts intrange)]
    (put counts intrange (if oldcount (+ 1 oldcount) 1))))

(pp counts)
