# Linux only - uses abstract unix domain sockets
(ev/spawn (net/server :unix "@abc123" (fn [conn] (print (:read conn 1024)) (:close conn))))
(ev/sleep 1)
(def s (net/connect :unix "@abc123" :stream))
(:write s "hello")
(:close s)
