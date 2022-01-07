# Helper code for running tests

(var num-tests-passed 0)
(var num-tests-run 0)
(var suite-num 0)
(var start-time 0)

(def is-verbose (os/getenv "VERBOSE"))

(defn assert
  "Override's the default assert with some nice error handling."
  [x &opt e]
  (default e "assert error")
  (++ num-tests-run)
  (when x (++ num-tests-passed))
  (def str (string e))
  (if x
    (when is-verbose (eprintf "\e[32m✔\e[0m %s: %v" (describe e) x))
    (eprintf "\e[31m✘\e[0m %s: %v" (describe e) x))
  x)

(defmacro assert-error
  [msg & forms]
  (def errsym (keyword (gensym)))
  ~(assert (= ,errsym (try (do ,;forms) ([_] ,errsym))) ,msg))

(defmacro assert-no-error
  [msg & forms]
  (def errsym (keyword (gensym)))
  ~(assert (not= ,errsym (try (do ,;forms) ([_] ,errsym))) ,msg))

(defn start-suite [x]
  (set suite-num x)
  (set start-time (os/clock))
  (eprint "Starting suite " x "..."))

(defn end-suite []
  (def delta (- (os/clock) start-time))
  (eprinf "Finished suite %d in %.3f seconds - " suite-num delta)
  (eprint num-tests-passed " of " num-tests-run " tests passed.")
  (if (not= num-tests-passed num-tests-run) (os/exit 1)))
