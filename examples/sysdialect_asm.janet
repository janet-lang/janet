(def ir-asm
  @{:instructions
   '((prim 0 s32)
     (bind 0 0)
     (bind 1 0)
     (bind 2 0)
     #(constant 0 10)
     (constant 1 20)
     (add 2 1 0)
     (return 2))
   :parameter-count 0
   :link-name "main"})

(def as (sysir/asm ir-asm))
(print :did-assemble)
(os/sleep 0.5)
(print (sysir/to-c as))
