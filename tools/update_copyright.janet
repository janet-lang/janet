(def usage (string "usage: janet " (first (dyn :args)) " <last-year> <this-year>"))

(def ignores [".git"])
(def exts ["LICENSE" "Makefile" ".build" ".c" ".h" ".janet"])

(defn arg [i]
  (defn bail [] (print usage) (quit))
  (if-not (= 3 (length (dyn :args)))
    (bail)
    (if-let [val (get (dyn :args) i)]
      val
      (bail))))

(def oy (arg 1))
(def ny (arg 2))
(def od (string "Copyright (c) " oy " Calvin Rose"))
(def nd (string "Copyright (c) " ny " Calvin Rose"))

(defn join [dir name]
  (os/realpath (string dir "/" name)))

(defn add-children [dir paths]
  (loop [name :in (os/dir dir)
              :unless (has-value? ignores name)]
   (array/push paths (join dir name))))

(defn ends-in? [exts s]
  (find (fn [ext] (string/has-suffix? ext s)) exts))

(defn update-disclaimer [path]
  (if-let [_   (ends-in? exts path)
           oc  (slurp path)
           pos (string/find od oc)
           nc  (string (string/slice oc 0 pos) nd (string/slice oc (+ pos (length od))))]
    (spit path nc)))

(def cwd (os/cwd))
(def paths (if (string/has-suffix? "janet" cwd)
             @[cwd]
             @[(join cwd "..")]))
(loop [p :in paths]
  (if (= :directory ((os/stat p) :mode))
    (add-children p paths)
    (update-disclaimer p)))
