# Patch janet.h 

(def [_ janeth janetconf output] (dyn :args))

(def- replace-peg
  (peg/compile
    ~(% (* '(to `#include "janetconf.h"`)
           (constant ,(slurp janetconf))
           (thru `#include "janetconf.h"`)
           '(any 1)))))

(spit output (first (peg/match replace-peg (slurp janeth))))
