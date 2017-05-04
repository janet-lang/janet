# Real compiler

# Make compiler
(: make-compiler (fn [] {
    'scopes []
    'env []
    'labels {}
}))

# Make default form options
(: make-formopts (fn [] {
    'target nil
    'resultUnused false
    'canChoose true
    'isTail false
}))

# Make scope
(: make-scope (fn [] {
    'level 0
    'nextSlot 0
    'frameSize 0
    'freeSlots []
    'literals {}
    'literalsArray []
    'slotMap []
}))
