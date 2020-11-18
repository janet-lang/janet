# Patch janet.h 
(def [_ janeth janetconf output] (dyn :args))
(spit output (peg/replace `#include "janetconf.h"` (slurp janetconf) (slurp janeth)))
