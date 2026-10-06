(let [t @{:a 1 :b 2 :c 3 :d 4 :e 5 :f 6}
      st {:a 1 :b 2 :c 3 :d 4 :e 5 :f 6}
      proto @{:area (fn [self] (* (in self :w) (in self :h)))}
      o (table/setproto @{:w 3 :h 4} proto)
      start (os/clock :monotonic)]
  (var s 0)
  (for i 0 5_000_000
    (+= s (in t :a) (get t :f) (in st :d) (get st :b))
    (if (get t :zz) (++ s))
    (+= s (:area o)))
  (print s)
  (print (- (os/clock :monotonic) start) " s"))
