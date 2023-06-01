# test *debug* flags

(import ./helper :prefix "" :exit true)
(start-suite 15)

(assert (deep= (in (disasm (defn a [] (def x 10) x)) :symbolmap)
               @[[0 2 0 'a] [0 2 1 'x]])
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
               @[[0 6 0 'arg]
                 [0 6 1 'a]
                 [0 6 2 'x]
                 [1 6 3 'y]
                 [2 6 4 'z]])
        "arg & inner symbolslots")

# buffer/push-at
(assert (deep= @"abc456" (buffer/push-at @"abc123" 3 "456")) "buffer/push-at 1")
(assert (deep= @"abc456789" (buffer/push-at @"abc123" 3 "456789")) "buffer/push-at 2")
(assert (deep= @"abc423" (buffer/push-at @"abc123" 3 "4")) "buffer/push-at 3")

(assert (= 10 (do (var x 10) (def y x) (++ x) y)) "no invalid aliasing")

# Crash issue #1174 - bad debug info
(defn crash []
  (debug/stack (fiber/current)))
(do
  (math/random)
  (defn foo [_]
    (crash)
    1)
  (foo 0)
  10)

(end-suite)
