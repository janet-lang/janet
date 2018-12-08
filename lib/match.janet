
# Sentinel value bad matches
(def- sentinel ~',(gensym))

(defn- match-1
  [pattern expr onmatch seen]
  (cond
    (and (symbol? pattern) (not (keyword? pattern)))
    (if (get seen pattern)
      ~(if (= ,pattern ,expr) ,onmatch ,sentinel)
      (do
        (put seen pattern true)
        ~(if (= nil (def ,pattern ,expr)) ,sentinel ,onmatch)))
    (indexed? pattern) 
    (do
      (def len (length pattern))
      (def $arr (gensym))
      (defn aux [i]
        (if (= i len)
          onmatch
          (match-1 pattern.i ~(get ,$arr ,i) (aux (+ i 1)) seen)))
      ~(do (def ,$arr ,expr) ,(aux 0)))
    (dictionary? pattern)
    (do
      (def $dict (gensym))
      (defn aux [key]
        (if (= key nil)
          onmatch
          (match-1 (get pattern key) ~(get ,$dict ,key) (aux (next pattern key)) seen)))
      ~(do (def ,$dict ,expr) ,(aux (next pattern nil))))
    :else ~(if (= ,pattern ,expr) ,onmatch ,sentinel)))

(defmacro match
  "Pattern matching."
  [x & cases]
  (if (not (atomic? x))
    (do (def $x (gensym)) ~(do (def ,$x ,x) ,(match $x ;cases)))
    (do
      (def len (length cases))
      (def len-1 (dec len))
      (defn aux [i]
        (cond
          (= i len-1) (get cases i)
          (< i len-1) (do
                        (def $res (gensym))
                        ~(if (= ,sentinel (def ,$res ,(match-1 cases.i x (get cases (inc i)) @{})))
                           ,(aux (+ 2 i))
                           ,$res))))
      (aux 0))))
