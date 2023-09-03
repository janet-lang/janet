(def ir-asm
  @{:instructions
   '(
     # Types
     (type-prim Double f64)
     (type-array BigVec Double 100)

     # Declarations
     (bind 0 BigVec)
     (bind 1 BigVec)
     (bind 2 BigVec)
     (add 2 0 1)
     (return 2))
   :parameter-count 2
   :link-name "add_vector"})

(def as (sysir/asm ir-asm))
(print (sysir/to-c as))
