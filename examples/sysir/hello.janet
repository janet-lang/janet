(use ./frontend)

(defn-external printf:int [fmt:pointer])
(defn-external exit:void [x:int])

(defsys _start:void []
  (printf "hello, world!\n")
  (exit (the int 0))
  (return))

(defn main [& args]
  (def [_ what] args)
  (eprint "MODE: " what)
  (case what
    "c" (dumpc)
    "x64" (dumpx64)))
