# Remove carriage returns from file. Since piping things on 
# windows may add bad line endings, we can just force removal
# with this script.
(def fname ((dyn :args) 1))
(def source (slurp fname))
(def new-source (string/replace-all "\r" "" source))
(spit fname new-source :wb)
