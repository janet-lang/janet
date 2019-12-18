(defn worker-main
  "Sends 11 messages back to parent"
  [parent]
  (def name (thread/receive))
  (def interval (thread/receive))
  (for i 0 10
    (os/sleep interval)
    (:send parent (string/format "thread %s wakeup no. %d" name i)))
  (:send parent name))

(defn make-worker
  [name interval]
  (-> (thread/new worker-main)
      (:send name)
      (:send interval)))

(def bob (make-worker "bob" 0.02))
(def joe (make-worker "joe" 0.03))
(def sam (make-worker "sam" 0.05))

# Receive out of order
(for i 0 33
  (print (thread/receive)))

#
# Recursive Thread Tree - should pause for a bit, and then print a cool zigzag.
#

(def rng (math/rng (os/cryptorand 16)))

(defn choose [& xs]
  (in xs (:int rng (length xs))))

(defn worker-tree
  [parent]
  (def name (thread/receive))
  (def depth (thread/receive))
  (if (< depth 5)
    (do
    (defn subtree []
      (-> (thread/new worker-tree)
          (:send (string name "/" (choose "bob" "marley" "harry" "suki" "anna" "yu")))
          (:send (inc depth))))
    (let [l (subtree)
          r (subtree)
          lrep (thread/receive)
          rrep (thread/receive)]
      (:send parent [name ;lrep ;rrep])))
    (do
      (:send parent [name]))))

(-> (thread/new worker-tree) (:send "adam") (:send 0))
(def lines (thread/receive))
(map print lines)

#
# Receive timeout
#

(def slow (make-worker "slow-loras" 0.5))
(for i 0 50
  (try
    (let [msg (thread/receive 0.1)]
      (print "\n" msg))
    ([err] (prin ".") (:flush stdout))))

(print "\ndone timing, timeouts ending.")
(try (while true (print (thread/receive))) ([err] (print "done")))
