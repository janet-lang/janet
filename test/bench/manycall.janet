(let [add1 (fn [x] (+ x 1)) # not inc because selective "best case"
      fib (fn fib [n] (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))
      start (os/clock :monotonic)]
  (var s 0)
  (for i 0 50_000_000 (set s (add1 s)))
  (+= s (fib 30))
  (print s)
  (print (- (os/clock :monotonic) start) " s"))
