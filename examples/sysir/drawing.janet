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

# assume 128x128 32 bit color image
#   Size : 128 * 128 * 4 + align(14 + 40, 4) = 65592
#   dib offset : align(14 + 40, 4) = 56

(setdyn :verbose true)

(defsys write_32:void [x:uint]
  (write 1 (address x) 4)
  (return))

(defsys write_16:void [x:uint]
  (write 1 (address x) 2)
  (return))

(defsys w32:void [c:cursor x:uint]
  (def p:p32 (load c))
  (store p x)
  (store c (the p32 (pointer-add p 1)))
  (return))

(defsys w16:void [c:cursor x:uint]
  (def p:p16 (cast (the p32 (load c))))
  (store p (the u16 (cast x)))
  # Why so much inference...
  (store c (the p32 (cast (the p16 (pointer-add p 1)))))
  (return))

(defsys write_header:void [w:uint h:uint]
  (write 1 "BM" 2)
  (def size:uint (+ 56 (* w h 4)))
  (write_32 size)
  (write_32 0)
  (write_32 56) # pixel array offset
  # Begin DIB
  (write_32 40) # dib size
  (write_32 w)
  (write_32 h)
  (write_16 1) # color panes - must be 1
  (write_16 32) # bits per pixel
  (write_32 0) # compression method - no compression
  (write_32 0) # image size - not needed when no compression, 0 should be fine
  (write_32 4096) # pixels per meter - horizontal resolution
  (write_32 4096) # pixels per meter - vertical resolution
  (write_32 0) # number of colors in palette - no palette so 0
  (write_32 0) # number of "important colors" ignored in practice
  (write_16 0) # add "gap 1" to align pixel array to multiple of 4 bytes
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

(defsys draw:void [w:uint h:uint]
  (def red:uint 0xFFFF0000)
  (def blue:uint 0xFF0000FF)
  (def size:uint (* w h 4))
  (var y:uint 0)
  (while (< y h)
    (var x:uint 0)
    (while (< x w)
      #(write_32 (if (< y 32) blue red))
      (if (> y 64)
        (write_32 blue)
        (write_32 red))
      (set x (+ 1 x)))
    (set y (+ y 1)))
  (return))

(defsys main:int []
  (def w:uint 128)
  (def h:uint 128)
  (write_header w h)
  (draw w h)
  #(makebmp w h)
  (return 0))

####

#(dump)
(print "#include <unistd.h>")
(dumpc)
#(dumpx64)
