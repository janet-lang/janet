(def channels
  (seq [:repeat 5] (ev/chan 4)))

(defn writer [c]
  (for i 0 3
    (def item (string i ":" (hash c)))
    (ev/sleep 0.1)
    (print "writer giving item " item " to " c "...")
    (ev/give c item))
  (print "Done!"))

(defn reader [name]
  (forever
    (def c (ev/select ;channels))
    (print "reader " name " got " (ev/take c) " from " c)))

# Readers
(each letter [:a :b :c :d :e :f :g]
  (ev/call reader letter))

# Writers
(each c channels
  (ev/call writer c))
