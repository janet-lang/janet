(import build/testmod :as testmod)

(if (not= 5 (testmod/get5)) (error "testmod/get5 failed"))
