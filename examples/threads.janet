(defn worker-main
  [parent]
  (def name (thread/receive parent))
  (for i 0 10
    (os/sleep 1)
    (print "thread " name " wakeup no. " i))
  (thread/send parent :done))

(defn make-worker
  [name]
  (-> (thread/new make-image-dict) (thread/send worker-main) (thread/send name)))

(def bob (make-worker "bob"))
(os/sleep 0.5)
(def joe (make-worker "joe"))

(thread/receive bob)
(thread/receive joe)
