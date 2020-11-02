(defn handler
  "Simple handler for connections."
  [stream]
  (defer (:close stream)
    (def id (gensym))
    (def b @"")
    (print "Connection " id "!")
    (while (:read stream 1024 b)
      (repeat 10 (print "work for " id " ...") (ev/sleep 0.1))
      (:write stream b)
      (buffer/clear b))
    (printf "Done %v!" id)))

# Run server.
(let [server (net/server "127.0.0.1" "8000")]
  (print "Starting echo server on 127.0.0.1:8000")
  (forever
    (if-let [conn (:accept server)]
      (ev/call handler conn)
      (print "no new connections"))))
