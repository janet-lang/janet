# Remove carriage returns from file. Since piping things on 
# windows may add bad line endings, we can just force removal
# with this script.
(def fname ((dyn :args) 1))
(with [f (file/open fname :rb+)]
  (def source (:read f :all))
  (def new-source (string/replace-all "\r" "" source))
  (:seek f :set 0)
  (:write f new-source))
