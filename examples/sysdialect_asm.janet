(def ir-asm
  @{:instructions
   '((add 2 1 0)
    (return 2))
   :types
   '(s32 s32 s32)
   :parameter-count 2
   :link-name "add_2_ints"})

(-> ir-asm sysir/asm sysir/to-c print)
