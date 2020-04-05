
(os/mkdir "./tools/afl/unmarshal_testcases/")

(defn spit-case [n v]
  (spit
    (string "./tools/afl/unmarshal_testcases/" (string n))
    (marshal v make-image-dict)))

(def cases [
  nil

  "abc"

  :def

  'hij

  123

  (int/s64 123)

  "7"

  [1 2 3]

  @[1 2 3]

  {:a 123}

  @{:b 'xyz}

  (peg/compile
    '{:a (* "a" :b "a")
      :b (* "b" (+ :a 0) "b")
      :main (* "(" :b ")")})

  (fn f [a] (fn [] {:ab a}))

  (fn f [a] (print "hello world!"))

  (do
    (defn f [a] (yield) @[1 "2"])
    (def fb (fiber/new f))
    (resume fb)
    fb)
])

(eachk i cases
  (spit-case i (in cases i)))
