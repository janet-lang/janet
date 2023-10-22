(def ir-asm
  '((link-name "redefine_type_fail")
    (type-prim Real f32)
    (type-prim 1 s32)
    (bind bob Real)
    (return bob)))

(def ctx (sysir/context))
(sysir/asm ctx ir-asm)
(print (sysir/to-c ctx))
