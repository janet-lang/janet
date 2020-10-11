(def channels
  (seq [:repeat 5] (ev/chan 4)))

(defn writer [c]
  (for i 0 3
    (ev/sleep 0.1)
    (print "writer giving item " i " to " c "...")
    (ev/give c (string "item " i)))
  (print "Done!"))

(defn reader [name]
  (forever
    (def [_ c x] (ev/select ;channels))
    (print "reader " name " got " x " from " c)))

# Readers
(each letter [:a :b :c :d :e :f :g]
  (ev/call reader letter))

# Writers
(each c channels
  (ev/call writer c))
