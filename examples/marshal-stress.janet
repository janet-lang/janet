(defn init-db [c]
  (def res @{:clients @{}})
  (var i 0)
  (repeat c
    (def n (string "client" i))
    (put-in res [:clients n] @{:name n :projects @{}})
    (++ i)
    (repeat c
      (def pn (string "project" i))
      (put-in res [:clients n :projects pn] @{:name pn})
      (++ i)
      (repeat c
        (def tn (string "task" i))
        (put-in res [:clients n :projects pn :tasks tn] @{:name pn})
        (++ i))))
  res)

(loop [c :range [30 80 1]]
  (var s (os/clock))
  (print "Marshal DB with " c " clients, "
         (* c c) " projects and "
         (* c c c) " tasks. "
         "Total " (+ (* c c c) (* c c) c) " tables")
  (def buf (marshal (init-db c) @{} @""))
  (print "Buffer is " (length buf) " bytes")
  (print "Duration " (- (os/clock) s))
  (set s (os/clock))
  (gccollect)
  (print "Collected garbage in " (- (os/clock) s)))

