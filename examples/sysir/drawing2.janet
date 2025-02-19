###
### Create a .bmp file on linux.
###

# Quick run and view on Linux:
# build/janet examples/sysir/drawing2.janet > temp.c && cc temp.c && ./a.out > temp.bmp && feh temp.bmp

(use ./frontend)

(setdyn :verbose true)

# Pointer types
(defpointer p32 uint)
(defpointer p16 u16)
(defpointer cursor p32)

# Linux syscalls
# (defn-syscall brk:p32 12 [amount:uint])
# (defn-syscall exit:void 60 [code:int])
# (defn-syscall write:void 1 [fd:int data:p32 size:uint])
# (defn-syscall write_string 1 [fd:int data:pointer size:uint])

# External
(defn-external write:void [fd:int mem:pointer size:uint])
(defn-external exit:void [x:int])
(defn-external malloc:p32 [size:uint])

(defsys w32:void [c:cursor x:uint]
  (def p:p32 (load c))
  (store p x)
  (store c (the p32 (pointer-add p 1)))
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
  (def c:cursor (cast (malloc 4)))
  #(def cursor_data:p32 mem)
  #(def c:cursor (address cursor_data))
  (store c mem)
  (w16 c 0x4D42) # ascii "BM"
  (w32 c size)
  (w32 c 0)
  (w32 c 56)
  (w32 c 40)
  (w32 c w)
  (w32 c h)
  (w16 c 1)
  (w16 c 32)
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
  (def green:uint 0xFF00FF00)
  (var y:uint 0)
  (while (< y h)
    (var x:uint 0)
    (while (< x w)
      (def d2:uint (+ (* x x) (* y y)))
      (if (> d2 100000)
        (if (> d2 200000) (w32 c green) (w32 c blue))
        (w32 c red))
      (set x (+ 1 x)))
    (set y (+ y 1)))
  (write 1 mem size)
  (return mem))

(defsys main:int []
  (def w:uint 512)
  (def h:uint 512)
  (makebmp w h)
  (return 0))

####

(dumpx64)

#(print "#include <unistd.h>")
#(dumpc)
