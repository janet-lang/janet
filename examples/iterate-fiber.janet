(def f
  (coro
    (for i 0 10
      (yield (string "yield " i))
      (os/sleep 0))))

(print "simple yielding")
(each item f (print "got: " item ", now " (fiber/status f)))

(def f2
  (coro
    (for i 0 10
      (yield (string "yield " i))
      (ev/sleep 0))))

(print "complex yielding")
(each item f2 (print "got: " item ", now " (fiber/status f2)))

(print (fiber/status f2))
