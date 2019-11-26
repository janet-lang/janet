# Load this file and run (myfn) to see the debugger

(defn myfn
  []
  (debug)
  (for i 0 10 (print i)))

(debug/fbreak myfn 3)

# Enable debugging in repl with
# (setdyn :debug true)
