# Helper code for running tests

(var num-tests-passed 0)
(var num-tests-run 0)
(var suite-num 0)

(defn assert [x e]
 (++ num-tests-run)
 (when x (++ num-tests-passed))
 (print (if x 
         "  \e[32m✔\e[0m "
         "  \e[31m✘\e[0m ") e)
 x)

(defn start-suite [x]
 (:= suite-num x)
 (print "\nRunning test suite " x " tests...\n"))

(defn end-suite []
 (print "\nTest suite " suite-num " finished.")
 (print num-tests-passed " of " num-tests-run " tests passed.\n")
 (if (not= num-tests-passed num-tests-run) (os/exit 1)))
