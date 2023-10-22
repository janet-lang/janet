(def types-asm
  '((type-prim Double f64)
    (type-array BigVec Double 100)))

(def add-asm
  '((link-name "add_vector")
    (parameter-count 2)
    # Declarations
    (bind a BigVec)
    (bind b BigVec)
    (bind c BigVec)
    (add c a b)
    (return c)))

(def sub-asm
  '((link-name "sub_vector")
    (parameter-count 2)
    (bind a BigVec)
    (bind b BigVec)
    (bind c BigVec)
    (subtract c a b)
    (return c)))

(def ctx (sysir/context))
(sysir/asm ctx types-asm)
(sysir/asm ctx add-asm)
(sysir/asm ctx sub-asm)
(print (sysir/to-c ctx))
