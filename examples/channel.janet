(def c (ev/chan 4))

(defn writer []
  (for i 0 10
    (ev/sleep 0.1)
    (print "writer giving item " i "...")
    (ev/give c (string "item " i))))

(defn reader [name]
  (forever
    (print "reader " name " got " (ev/take c))))

(ev/call writer)
(each letter [:a :b :c :d :e :f :g]
  (ev/call reader letter))
