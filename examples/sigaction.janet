(defn action []
  (print "Handled SIGHUP!")
  (flush))

(defn main [_]
  # Set the interrupt-interpreter argument to `true` to allow
  # interrupting the busy loop `(forever)`. By default, will not
  # interrupt the interpreter.
  (os/sigaction :hup action true)
  (forever))
