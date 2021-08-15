(def chan (ev/thread-chan 10))

(ev/spawn
  (ev/sleep 0)
  (print "started fiber!")
  (ev/give chan (math/random))
  (ev/give chan (math/random))
  (ev/give chan (math/random))
  (ev/sleep 0.5)
  (for i 0 10
    (print "giving to channel...")
    (ev/give chan (math/random))
    (ev/sleep 1))
  (print "finished fiber!")
  (:close chan))

(ev/do-thread
  (print "started thread!")
  (ev/sleep 1)
  (while (def x (do (print "taking from channel...") (ev/take chan)))
    (print "got " x " from thread!"))
  (print "finished thread!"))
