(defn handler
  "Simple handler for connections."
  [stream]
  (def id (gensym))
  (def b @"")
  (print "Connection " id "!")
  (while (:read stream 1024 b)
    (repeat 10 (print "work for " id " ...") (ev/sleep 1))
    (:write stream b)
    (buffer/clear b))
  (printf "Done %v!" id))

(defn spawn-kid
  "Run handler in a new fiber"
  [conn]
  (def f (fiber/new handler))
  (ev/go f conn))

(print "Starting echo server on 127.0.0.1:8000")

(def server (net/server "127.0.0.1" "8000"))

# Run server.
(while true
  (with [conn (:accept server)]
    (spawn-kid conn)
    (ev/sleep 0.1)))
