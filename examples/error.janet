# An example file that errors out. Run with ./janet examples/error.janet
# to see stack trace for runtime errors.

(defn bork [x]

  (defn bark [y]
    (print "Woof!")
    (print y)
    (error y)
    (print "Woof!"))

  (bark (* 2 x))
  (bark (* 3 x)))

(defn pupper []
  (bork 3)
  1)

(do (pupper) 1)
