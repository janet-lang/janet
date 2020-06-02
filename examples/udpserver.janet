(def server (net/server "127.0.0.1" "8009" nil :datagram))
(while true
  (def buf @"")
  (def who (:recv-from server 1024 buf))
  (printf "got %q from %v, echoing!" buf who)
  (:send-to server who buf))
