(export! "scheck" (fn [x]
    (: dat (serialize x))
    (: deser (deserialize dat))
    (print (debugp deser))
    deser
))

(scheck 1)
(scheck true)
(scheck nil)
(scheck "asdasdasd")
(scheck (struct 1 2 3 4))
(scheck (tuple 1 2 3))
(scheck 123412.12)
(scheck (funcdef (fn [] 1)))
(scheck (funcenv (fn [] 1)))
(do
    (: producer (fn [a] (fn [] a)))
    (: f        (producer "hello!"))
    (scheck (funcenv f))
)
(scheck (fn [] 1))
