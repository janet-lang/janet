(def conn (net/connect "127.0.0.1" "8009" :datagram))
(:write conn (string/format "%q" (os/cryptorand 16)))
(def x (:read conn 1024))
(pp x)

