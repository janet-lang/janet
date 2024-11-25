###
### Create a .bmp file on linux.
###

(use ./frontend)

(defpointer p32 uint)
(defpointer p16 u16)
(defpointer cursor p32)
(defn-external write:void [fd:int mem:pointer size:uint])
(defn-external exit:void [x:int])
(defn-external malloc:p32 [x:uint])
(defn-external free:void [m:p32])

(defsys w32:void [c:cursor x:uint]
  (def p:p32 (load c))
  (store p x)
  (store c (the p32 (pointer-add p 4)))
  (return))

(defsys w16:void [c:cursor x:uint]
  # Casting needs revisiting
  (def p:p16 (cast (the p32 (load c))))
  (store p (the u16 (cast x)))
  (store c (the p32 (cast (the p16 (pointer-add p 1)))))
  (return))

(defsys makebmp:p32 [w:uint h:uint]
  (def size:uint (+ 56 (* w h 4)))
  (def mem:p32 (malloc size))
  (def c:cursor (address mem))
  (w16 c 0x424D) # ascii "BM"
  (w32 c size)
  (w32 c 56)
  (w32 c 40)
  (w32 c w)
  (w32 c h)
  (w32 c 0x20001)
  (w32 c 0)
  (w32 c 0)
  (w32 c 4096)
  (w32 c 4096)
  (w32 c 0)
  (w32 c 0)
  (w16 c 0) # padding
  # Draw
  (def red:uint 0xFFFF0000)
  (def blue:uint 0xFF0000FF)
  (def size:uint (* w h 4))
  (var y:uint 0)
  (while (< y h)
    (var x:uint 0)
    (while (< x w)
      #(write_32 (if (< y 32) blue red))
      (if (> y 64)
        (w32 c blue)
        (w32 c red))
      (set x (+ 1 x)))
    (set y (+ y 1)))
  (write 1 mem size)
  (return mem))

(defsys makebmp2:p32 [w:uint h:uint]
  (def size:uint 56)
  (def mem:p32 (malloc size))
  (def c:cursor (address mem))
  (w32 c 0x12345678)
  (w32 c size)
  (w32 c 56)
  (w32 c 40)
  (w32 c w)
  (w32 c h)
  (w32 c 0x20001)
  (w32 c 0)
  (w32 c 0)
  (w32 c 4096)
  (w32 c 4096)
  (w32 c 0)
  (w32 c 0)
  (write 1 mem size)
  (return mem))

(defsys main:int []
  (def w:uint 128)
  (def h:uint 128)
  #(makebmp w h)
  (makebmp2 w h)
  (return 0))

####

#(dump)
(print "#include <unistd.h>")
(dumpc)
#(dumpx64)
