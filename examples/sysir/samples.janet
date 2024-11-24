(use ./frontend)

(defstruct vec3
   a float
   b float
   c float)

(defunion myunion
  a float
  b double
  c long)

(defarray myvec float 4)
(defarray mymat myvec 4)

(defn-external printf:int [fmt:pointer x:int]) # TODO varargs

(defn-external exit:void [x:int])

(defsys square:int
  [num:int]
  (return (* 1 num num)))

(defsys simple:int [x:int]
  (def xyz:int (+ 1 2 3))
  (return (* x 2 x)))

(defsys myprog:int []
  (def xyz:int (+ 1 2 3))
  (def abc:int (* 4 5 6))
  (def x:boolean (= xyz 5))
  (var i:int 0)
  (while (< i 10)
    (set i (+ 1 i))
    (printf "i = %d\n" i))
  (printf "hello, world!\n%d\n" (if x abc xyz))
  (return (simple (* abc xyz))))

(defsys doloop [x:int y:int]
  (var i:int x)
  (while (< i y)
    (set i (+ 1 i))
    (printf "i = %d\n" i))
  (myprog)
  (return x))

(defsys _start:void []
  #(syscall 1 1 "Hello, world!\n" 14)
  (doloop 10 20)
  (exit (the int 0))
  (return))

(defsys test_inttypes:ulong []
  (def x:ulong 123:u)
  (return (+ x x)))

(defsys test_arrays:myvec [a:myvec b:myvec]
  (return (+ a b)))

(defsys make_array:myvec []
  (def vec:myvec [0 0 0 0])
  (return vec))

(defsys make_mat:mymat []
  (def mat:mymat [[1 0 0 0] [0 1 0 0] [0 0 1 0] [0 0 0 1]])
  (return mat))

####

#(dump)
#(dumpc)
(dumpx64)
