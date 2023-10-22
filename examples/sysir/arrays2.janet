(def ir-asm
  '((link-name "add_vectorp")
    (parameter-count 2)

    # Types
    (type-prim Double f64)
    (type-array BigVec Double 100)
    (type-pointer BigVecP BigVec)

    # Declarations
    (bind 0 BigVecP)
    (bind 1 BigVecP)
    (bind 2 BigVecP)
    (add 2 0 1)
    (return 2)))

(def ctx (sysir/context))
(sysir/asm ctx ir-asm)
(print (sysir/to-c ctx))
