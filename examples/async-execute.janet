(defn dowork [name n]
  (print name " starting work...")
  (os/execute [(dyn :executable) "-e" (string "(os/sleep " n ")")] :p)
  (print name " finished work!"))

# Will be done in parallel
(print "starting group A")
(ev/call dowork "A 2" 2)
(ev/call dowork "A 1" 1)
(ev/call dowork "A 3" 3)

(ev/sleep 4)

# Will also be done in parallel
(print "starting group B")
(ev/call dowork "B 2" 2)
(ev/call dowork "B 1" 1)
(ev/call dowork "B 3" 3)

(ev/sleep 4)

(print "all work done")
