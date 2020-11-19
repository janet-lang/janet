# Patch janet.h 
(def [_ janeth janetconf output] (dyn :args))
(spit output (string/replace `#include "janetconf.h"` (slurp janetconf) (slurp janeth)))
