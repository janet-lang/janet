
(def ir-asm
  @{:instructions
   '(
     # Types
     (type-prim Double f64)
     (type-array BigVec Double 100)
     (type-pointer BigVecP BigVec)

     # Declarations
     (bind 0 BigVecP)
     (bind 1 BigVecP)
     (bind 2 BigVecP)
     (add 2 0 1)
     (return 2))
   :parameter-count 2
   :link-name "add_vectorp"})

(def as (sysir/asm ir-asm))
(print (sysir/to-c as))
