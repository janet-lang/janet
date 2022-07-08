(defn sleep
  "Sleep the entire thread, not just a single fiber."
  [n]
  (os/sleep (* 0.1 n)))

(defn work [lock n]
  (ev/acquire-lock lock)
  (print "working " n "...")
  (sleep n)
  (print "done working...")
  (ev/release-lock lock))

(defn reader
  [rwlock n]
  (ev/acquire-rlock rwlock)
  (print "reading " n "...")
  (sleep n)
  (print "done reading " n "...")
  (ev/release-rlock rwlock))

(defn writer
  [rwlock n]
  (ev/acquire-wlock rwlock)
  (print "writing " n "...")
  (sleep n)
  (print "done writing...")
  (ev/release-wlock rwlock))

(defn test-lock
  []
  (def lock (ev/lock))
  (for i 3 7
    (ev/spawn-thread
      (work lock i))))

(defn test-rwlock
  []
  (def rwlock (ev/rwlock))
  (for i 0 20
    (if (> 0.1 (math/random))
      (ev/spawn-thread (writer rwlock i))
      (ev/spawn-thread (reader rwlock i)))))

(test-rwlock)
(test-lock)
