(def p (parser/new))
(parser/consume p (slurp ((dyn :args) 1)))
(while (parser/has-more p)
  (pp (parser/produce p)))
