# test *debug* flags

(import ./helper :prefix "" :exit true)
(start-suite 15)

(assert (deep= (in (disasm (defn a [] (def x 10) x)) :symbolmap)
               @[[0 3 0 'a] [1 3 1 'x]])
        "symbolslots when *debug* is true")

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

# need to fix upvalues
(comment
  (setdyn *debug* true)
  (setdyn :pretty-format "%.40M")
  (def f (fn [x] (fn [y] (+ x y))))
  (assert (deep= (map last (in (disasm (f 10)) :symbolmap))
                 @['x 'y])
          "symbolslots upvalues"))

(assert (deep= (in (disasm (defn a [arg]
                             (def x 10)
                             (do
                               (def y 20)
                               (def z 30)
                               (+ x y z)))) :symbolmap)
               @[[0 7 0 'arg]
                 [0 7 1 'a]
                 [1 7 2 'x]
                 [2 7 3 'y]
                 [3 7 4 'z]])
        "arg & inner symbolslots")

(end-suite)
