(defn action []
  (print "cleanup")
  (os/exit 1))

(defn main [_]
  # Set the interrupt-interpreter argument to `true` to allow
  # interrupting the busy loop `(forever)`. By default, will not
  # interrupt the interpreter.
  (os/sigaction :term action true)
  (os/sigaction :int action true)
  (forever))
