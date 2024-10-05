(defn install
  [manifest &]
  (bundle/add-file manifest "badmod.janet"))

(defn check
  [&]
  (error "Check failed!"))
