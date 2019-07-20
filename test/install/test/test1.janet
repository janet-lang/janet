(import build/testmod :as testmod)

(if (not= 5 (testmod/get5)) (error "testmod/get5 failed"))

(import cook)

(with-dyns [:modpath (os/cwd)]
  (cook/install-git "https://github.com/janet-lang/json.git"))
