# Patch jpm to have the correct paths for the current install.
# usage: janet patch-jpm.janet output --libdir=/usr/local/lib/x64-linux/ --binpath

(def- argpeg
  (peg/compile
    '(* "--" '(to "=") "=" '(any 1))))

(def- args (tuple/slice (dyn :args) 3))
(def- len (length args))
(var i :private 0)

(def install-paths @{})

# Get flags
(each a args
  (if-let [m (peg/match argpeg a)]
    (let [[key value] m]
      (put install-paths (keyword key) value))))

(def- replace-peg
  (peg/compile
    ~(% (* '(to "###START###")
           (constant ,(string/format "# Inserted by tools/patch-jpm.janet\n(defn- install-paths [] %j)" install-paths))
           (thru "###END###")
           '(any 1)))))

(def source (slurp ((dyn :args) 1)))
(def newsource (0 (peg/match replace-peg source)))

(spit ((dyn :args) 2) newsource)

(unless (= :windows (os/which))
  (os/shell (string `chmod +x "` ((dyn :args) 2) `"`)))
