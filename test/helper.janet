# Helper code for running tests

(var num-tests-passed 0)
(var num-tests-run 0)
(var suite-name 0)
(var start-time 0)
(var skip-count 0)
(var skip-n 0)

(def is-verbose (os/getenv "VERBOSE"))

(defn- assert-no-tail
  "Override's the default assert with some nice error handling."
  [x &opt e]
  (++ num-tests-run)
  (when (pos? skip-n)
    (-- skip-n)
    (++ skip-count)
    (break x))
  (default e "assert error")
  (when x (++ num-tests-passed))
  (def str (string e))
  (def stack (debug/stack (fiber/current)))
  (def frame (last stack))
  (def line-info (string/format "%s:%d"
                              (frame :source) (frame :source-line)))
  (if x
    (when is-verbose (eprintf "\e[32m✔\e[0m %s: %s: %v" line-info (describe e) x))
    (do
      (eprintf "\e[31m✘\e[0m %s: %s: %v" line-info (describe e) x) (eflush)))
  x)

(defn skip-asserts
  "Skip some asserts"
  [n]
  (+= skip-n n)
  nil)

(defmacro assert
  [x &opt e]
  (def xx (gensym))
  (default e (string/format "%j" x))
  ~(do
     (def ,xx ,x)
     (,assert-no-tail ,xx ,e)
     ,xx))

(defmacro assert-error
  [msg & forms]
  (def errsym (keyword (gensym)))
  ~(assert (= ,errsym (try (do ,;forms) ([_] ,errsym))) ,msg))

(defn check-compile-error
  [form]
  (def result (compile form))
  (assert (table? result) (string/format "expected compilation error for %j, but compiled without error" form)))

(defmacro assert-no-error
  [msg & forms]
  (def e (gensym))
  (def f (gensym))
  (if is-verbose
  ~(try (do ,;forms (,assert true ,msg)) ([,e ,f] (,assert false ,msg) (,debug/stacktrace ,f ,e "\e[31m✘\e[0m ")))
  ~(try (do ,;forms (,assert true ,msg)) ([_] (,assert false ,msg)))))

(defn start-suite [&opt x]
  (default x (dyn :current-file))
  (set suite-name
       (cond
         (number? x) (string x)
         (string x)))
  (set start-time (os/clock))
  (eprint "Starting suite " suite-name "..."))

(defn end-suite []
  (def delta (- (os/clock) start-time))
  (eprinf "Finished suite %s in %.3f seconds - " suite-name delta)
  (eprint num-tests-passed " of " num-tests-run " tests passed (" skip-count " skipped).")
  (if (not= (+ skip-count num-tests-passed) num-tests-run) (os/exit 1)))

(defn rmrf
  "rm -rf in janet"
  [x]
  (case (os/lstat x :mode)
    nil nil
    :directory (do
                 (each y (os/dir x)
                   (rmrf (string x "/" y)))
                 (os/rmdir x))
    (os/rm x))
  nil)

(defn randdir
  "Get a random directory name"
  []
  (string "tmp_dir_" (slice (string (math/random) ".tmp") 2)))
