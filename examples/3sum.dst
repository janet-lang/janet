(defn sum3
  "Solve the 3SUM problem in O(n^2) time."
  [s]
  (def tab @{})
  (def solutions @{})
  (def len (length s))
  (loop [k :range [0 len]]
    (put tab (get s k) k))
  (loop [i :range [0 len], j :range [0 len]]
    (def k (get tab (- 0 (get s i) (get s j))))
    (when (and k (not= k i) (not= k j) (not= i j))
      (put solutions {i true j true k true} true)))
  (map keys (keys solution)))
