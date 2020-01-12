(use build/testmod)
(use build/testmod2)

(defn main [&]
  (print "Hello from executable!")
  (print (+ (get5) (get6))))
