(use ./frontend)

(def winmain
  '(defn Start:void []
     (MessageBoxExA (the pointer 0) "Hello, world!" "Test" 0 (the s16 0))
     (ExitProcess (the int 0))
     (return)))

####

(compile1 winmain)
#(dump)
#(dumpc)
(dumpx64)
