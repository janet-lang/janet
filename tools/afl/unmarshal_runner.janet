# Unmarshal garbage.
(def v (unmarshal (slurp ((dyn :args) 1)) load-image-dict))
# Trigger leaks or use after free.
(gccollect)
# Attempt to use generated value.
(marshal v make-image-dict)
