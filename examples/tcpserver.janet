(defn handler
  "Simple handler for connections."
  [stream]
  (defer (:close stream)
    (def id (gensym))
    (def b @"")
    (print "Connection " id "!")
    (while (:read stream 1024 b)
      (:write stream b)
      (buffer/clear b))
    (printf "Done %v!" id)))

(net/server "127.0.0.1" "8000" handler)
