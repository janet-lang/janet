# test *debug* flags

(import ./helper :prefix "" :exit true)
(start-suite 15)

(assert (deep= (in (disasm (defn a [] (def x 10) x)) :slotsyms)
               @[])
  "no slotsyms when *debug* is false")

(setdyn *debug* true)
(assert (deep= (in (disasm (defn a [] (def x 10) x)) :slotsyms)
               @[[:top :top @{"a" @[[0 0]] "x" @[[1 1]]}]])
  "slotsyms when *debug* is true")
(setdyn *debug* false)

(setdyn *debug* true)
(assert (deep= (in (disasm (defn a []
                            (def x 10)
                            (do
                              (def y 20)
                              (+ x y)))) :slotsyms)
               @[[:top :top @{"a" @[[0 0]] "x" @[[1 1]]}]
                 [2    5    @{"y" @[[2 2]]}]])
  "inner slotsyms")
(setdyn *debug* false)

(end-suite)
