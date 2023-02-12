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

# buffer/push-at
(assert (deep= @"abc456" (buffer/push-at @"abc123" 3 "456")) "buffer/push-at 1")
(assert (deep= @"abc456789" (buffer/push-at @"abc123" 3 "456789")) "buffer/push-at 2")
(assert (deep= @"abc423" (buffer/push-at @"abc123" 3 "4")) "buffer/push-at 3")

(end-suite)
