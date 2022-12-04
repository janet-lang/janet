###
### Relies on NASM being installed to assemble code.
### Only works on x86-64 Linux.
###
### Before running, compile hello.nasm to hello.bin with
### $ nasm hello.nasm -o hello.bin

(def bin (slurp "hello.bin"))
(def f (ffi/jitfn bin))
(def signature (ffi/signature :default :void))
(ffi/call f signature)
(print "called a jitted function with FFI!")
(print "machine code: " (describe (string/slice f)))
