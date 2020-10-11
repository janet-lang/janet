###
### examples/select2.janet
###
### Mix reads and writes in select.
###

(def c1 (ev/chan 40))
(def c2 (ev/chan 40))
(def c3 (ev/chan 40))
(def c4 (ev/chan 40))

(def c5 (ev/chan 4))

(defn worker
  [c n x]
  (forever
    (ev/sleep n)
    (ev/give c x)))

(defn writer-worker
  [c]
  (forever
    (ev/sleep 0.2)
    (print "writing " (ev/take c))))

(ev/call worker c1 1 :item1)
(ev/sleep 0.2)
(ev/call worker c2 1 :item2)
(ev/sleep 0.1)
(ev/call worker c3 1 :item3)
(ev/sleep 0.2)
(ev/call worker c4 1 :item4)
(ev/sleep 0.1)
(ev/call worker c4 1 :item5)
(ev/call writer-worker c5)

(forever (pp (ev/rselect c1 c2 c3 c4 [c5 :thing])))
