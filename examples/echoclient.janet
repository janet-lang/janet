(with [conn (net/connect "127.0.0.1" 8000)]
  (print "writing abcdefg...")
  (:write conn "abcdefg")
  (print "reading...")
  (printf "got: %v" (:read conn 1024)))
