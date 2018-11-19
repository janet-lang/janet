# A game of life implementation

(def- windows
  (fora [x :range [-1 2]
         y :range [-1 2]
         :when (not (and (zero? x) (zero? y)))]
        (tuple x y)))

(defn- neighbors
  [[x y]]
  (mapa (fn [[x1 y1]] (tuple (+ x x1) (+ y y1))) windows))

(defn tick
  "Get the next state in the Game Of Life."
  [state]
  (def neighbor-set (frequencies (mapcat neighbors (keys state))))
  (def next-state @{})
  (loop [coord :keys neighbor-set
         :let [count (get neighbor-set coord)]]
    (if (if (get state coord)
          (or (= count 2) (= count 3))
          (= count 3))
      (put next-state coord true)))
  next-state)

(defn draw
  "Draw cells in the game of life from (x1, y1) to (x2, y2)"
  [state x1 y1 x2 y2]
  (loop [:before (print "+" (string.repeat "--" (inc (- y2 y1))) "+")
         :after (print "+" (string.repeat "--" (inc (- y2 y1))) "+")
         x :range [x1 (+ 1 x2)]
         :before (file.write stdout "|")
         :after (file.write stdout "|\n")
         y :range [y1 (+ 1 y2)]]
    (file.write stdout (if (get state (tuple x y)) "X " ". ")))
  (print))

#
# Run the example
#

(var *state* {'(0 0) true '(-1 0) true '(1 0) true '(1 1) true '(0 2) true})

(loop [i :range [0 20]]
  (print "generation " i)
  (draw *state* -7 -7 7 7)
  (:= *state* (tick *state*)))
