###
### Usage: janet examples/sigaction.janet 1|2|3|4 &
###
### Then at shell: kill -s SIGTERM $!
###

(defn action
  []
  (print "Handled SIGTERM!")
  (flush)
  (os/exit 1))

(defn main1
  []
  (os/sigaction :term action true)
  (forever))

(defn main2
  []
  (os/sigaction :term action)
  (forever))

(defn main3
  []
  (os/sigaction :term action true)
  (forever (ev/sleep math/inf)))

(defn main4
  []
  (os/sigaction :term action)
  (forever (ev/sleep math/inf)))

(defn main
  [& args]
  (def which (scan-number (get args 1 "1")))
  (case which
    1 (main1) # should work
    2 (main2) # will not work
    3 (main3) # should work
    4 (main4) # should work
    (error "bad main")))
