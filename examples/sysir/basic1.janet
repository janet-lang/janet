(def ir-asm
  '((link-name "test_function")

    # Types
    (type-prim Int s32)
    (type-prim Double f64)
    (type-struct MyPair 0 1)
    (type-pointer PInt 0)
    (type-array DoubleArray 1 1024)

    # Declarations
    (bind 0 Int)
    (bind 1 Int)
    (bind 2 Int)
    (bind 3 Double)
    (bind bob Double)
    (bind 5 Double)
    (bind 6 MyPair)

    # Code
    (constant 0 10)
    (constant 0 21)
    :location
    (add 2 1 0)
    (constant 3 1.77)
    (call 3 sin 3)
    (cast bob 2)
    (call :default bob test_function)
    (add 5 bob 3)
    (jump :location)
    (return 5)))

(def ctx (sysir/context))
(sysir/asm ctx ir-asm)
(print (sysir/to-c ctx))
