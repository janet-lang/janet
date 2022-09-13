(import ./helper :prefix "" :exit true)
(start-suite 14)

(assert (deep=
  (peg/match '(not (* (constant 7) "a")) "hello")
  @[]) "peg not")

(assert (deep=
  (peg/match '(if-not (* (constant 7) "a") "hello") "hello")
  @[]) "peg if-not")

(assert (deep=
  (peg/match '(if-not (drop (* (constant 7) "a")) "hello") "hello")
  @[]) "peg if-not drop")

(assert (deep=
  (peg/match '(if (not (* (constant 7) "a")) "hello") "hello")
  @[]) "peg if not")

(end-suite)
