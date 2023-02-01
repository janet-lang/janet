# test *debug* flags

(import ./helper :prefix "" :exit true)
(start-suite 15)

(assert (deep= (in (disasm (defn a [] (def x 10) x)) :symbolslots) nil)
  "no symbolslots when *debug* is false")

(setdyn *debug* true)
(assert (deep= (in (disasm (defn a [] (def x 10) x)) :symbolslots)
               @[[0 2147483647 0 "a"] [1 2147483647 1 "x"]])
  "symbolslots when *debug* is true")
(setdyn *debug* false)

(setdyn *debug* true)
(defn a [arg]
    (def x 10)
    (do
      (def y 20)
      (def z 30)
      (+ x y z)))
(def symbolslots (in (disasm a) :symbolslots))
(def f (asm (disasm a)))
(assert (deep= (in (disasm f) :symbolslots)
                symbolslots)
  "symbolslots survive disasm/asm")
(setdyn *debug* false)

(setdyn *debug* true)
(assert (deep= (in (disasm (defn a [arg]
                            (def x 10)
                            (do
                              (def y 20)
                              (def z 30)
                              (+ x y z)))) :symbolslots)
               @[[-1 2147483647 0 "arg"]
                 [0 2147483647 1 "a"]
                 [1 2147483647 2 "x"]
                 [2 7 3 "y"]
                 [3 7 4 "z"]])
  "arg & inner symbolslots")
(setdyn *debug* false)

(end-suite)
