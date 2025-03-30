(use ./frontend)

(defn-external printf:int [fmt:pointer x:int])
(defn-external exit:void [x:int])

(defsys doloop [x:int y:int]
  (var i:int x)
  (printf "initial i = %d\n" i)
  (while (< i y)
    (set i (+ 1 i))
    (printf "i = %d\n" i))
  (return x))

(defsys _start:void []
  (doloop 10 20)
  (exit (the int 0))
  (return))

(defn main [& args]
  (def [_ what] args)
  (dump)
  (eprint "MODE: " what)
  (case what
    "c" (dumpc)
    "x64" (dumpx64)))
