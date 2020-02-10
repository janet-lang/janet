(defn handler
  "Simple handler for connections."
  [stream]
  (def id (gensym))
  (def b @"")
  (print "Connection " id "!")
  (while (net/read stream 1024 b)
    (net/write stream b)
    (buffer/clear b))
  (printf "Done %v!" id)
  (net/close stream))

(net/server "127.0.0.1" "8000" handler)
