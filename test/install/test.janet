(import cook)

(cook/make-native
    :name "testmod"
    :source @["testmod.c"])

(import build/testmod :as testmod)

(if (not= 5 (testmod/get5)) (error "testmod/get5 failed"))

(print "OK!")
