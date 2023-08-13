(def ir-asm
  @{:instructions
   '(
     # Types
     (type-prim Real f32)
     (type-prim 1 s32)

     (bind bob Real)

     (return bob))
   :parameter-count 0
   :link-name "redefine_type_fail"})

(def as (sysir/asm ir-asm))
(print (sysir/to-c as))
