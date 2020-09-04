(use build/testmod)
(use build/testmod2)
(use build/testmod3)

(defn main [&]
  (print "Hello from executable!")
  (print (+ (get5) (get6) (get7))))
