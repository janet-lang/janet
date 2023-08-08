(def ir-asm
  @{:instructions
   '((prim 0 s32)
     (prim 1 f64)
     (struct 2 0 1)
     (pointer 3 0)
     (array 4 1 1024)
     (bind 0 0)
     (bind 1 0)
     (bind 2 0)
     (bind 3 1)
     (bind bob 1)
     (bind 5 1)
     (bind 6 2)
     (constant 0 10)
     (constant 0 21)
     :location
     (add 2 1 0)
     (constant 3 1.77)
     (call 3 sin 3)
     (cast bob 2)
     (add 5 bob 3)
     (jump :location)
     (return 5))
   :parameter-count 0
   :link-name "test_function"})

(def as (sysir/asm ir-asm))
(print (sysir/to-c as))
