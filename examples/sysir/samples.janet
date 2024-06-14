(use ./frontend)

(def square
  '(defn square:int [num:int]
     (return (* num num))))

(def simple
  '(defn simple:int [x:int]
     (def xyz:int (+ 1 2 3))
     (return (the int (* x 2 x)))))

(def myprog
  '(defn myprog:int []
     (def xyz:int (+ 1 2 3))
     (def abc:int (* 4 5 6))
     (def x:boolean (= (the int 5) xyz))
     (var i:int 0)
     (while (< i (the int 10))
       (set i (the int (+ 1 i)))
       (printf "i = %d\n" i))
     (printf "hello, world!\n%d\n" (the int (if x abc xyz)))
     (return (* abc xyz))))
     #(return (the int (simple (* abc xyz))))))

(def doloop
  '(defn doloop [x:int y:int]
     (var i:int x)
     (while (< i y)
       (set i (the int (+ 1 i)))
       (printf "i = %d\n" (the int i)))
     (myprog)
     (return x)))

(def main-fn
  '(defn _start:void []
     (syscall 1 1 "Hello, world!\n" 14)
     (doloop 10 20)
     (exit (the int 0))
     (return)))

####

#(compile1 square)
(compile1 simple)
(compile1 myprog)
(compile1 doloop)
(compile1 main-fn)
#(dump)
#(dumpc)
(dumpx64)

# Run with
# janet examples/sysir/scratch.janet > temp.nasm && nasm -felf64 temp.nasm -l temp.lst && ld temp.o && ./a.out