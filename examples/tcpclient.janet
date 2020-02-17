(with [conn (net/connect "127.0.0.1" "8000")]
  (printf "Connected to %q!" conn)
  (:write conn "hoho")
  (print "Wrote to connection...")
  (def res (:read conn 1024))
  (pp res))
