(defn worker-main
  "Sends 11 messages back to parent"
  [parent]
  (def name (:receive parent))
  (def interval (:receive parent))
  (for i 0 10
    (os/sleep interval)
    (:send parent (string/format "thread %s wakeup no. %d" name i)))
  (:send parent name))

(defn make-worker
  [name interval]
  (-> (thread/new)
      (:send worker-main)
      (:send name)
      (:send interval)))

(def bob (make-worker "bob" 0.02))
(def joe (make-worker "joe" 0.03))
(def sam (make-worker "sam" 0.05))

(:close joe)

(try (:receive joe) ([err] (print "Got expected error: " err)))

# Receive out of order
(for i 0 22
  (print (thread/receive [bob sam])))

#
# Recursive Thread Tree - should pause for a bit, and then print a cool zigzag.
#

(def rng (math/rng (os/cryptorand 16)))

(defn choose [& xs]
  (in xs (:int rng (length xs))))

(defn worker-tree
  [parent]
  (def name (:receive parent))
  (def depth (:receive parent))
  (if (< depth 5)
    (do
    (defn subtree []
      (-> (thread/new)
          (:send worker-tree)
          (:send (string name "/" (choose "bob" "marley" "harry" "suki" "anna" "yu")))
          (:send (inc depth))))
    (let [l (subtree)
          r (subtree)
          lrep (thread/receive l)
          rrep (thread/receive r)]
      (:send parent [name ;lrep ;rrep])))
    (do
      (:send parent [name]))))

(def lines (:receive (-> (thread/new) (:send worker-tree) (:send "adam") (:send 0))))
(map print lines)
