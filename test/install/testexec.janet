(use build/testmod)
(use build/testmod2)
(use build/testmod3)
(use build/test-mod-4)

(defn main [&]
  (print "Hello from executable!")
  (print (+ (get5) (get6) (get7) (get8))))
