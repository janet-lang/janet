###
### A useful debugger library for Janet. Should be used
### inside a debug repl.
###

(defn .fiber
  "Get the current fiber being debugged."
  []
  (if-let [entry (dyn '_fiber)]
    (entry :value)
    (dyn :fiber)))

(defn .stack
  "Print the current fiber stack"
  []
  (print)
  (debug/stacktrace (.fiber) "")
  (print))

(defn .frame
  "Show a stack frame"
  [&opt n]
  (def stack (debug/stack (.fiber)))
  (in stack (or n 0)))

(defn .fn
  "Get the current function"
  [&opt n]
  (in (.frame n) :function))

(defn .slots
  "Get an array of slots in a stack frame"
  [&opt n]
  (in (.frame n) :slots))

(defn .slot
  "Get the value of the nth slot."
  [&opt nth frame-idx]
  (in (.slots frame-idx) (or nth 0)))

(defn .quit
  "Resume (dyn :fiber) with the value passed to it after exiting the debugger."
  [&opt val]
  (setdyn :exit true)
  (setdyn :resume-value val)
  nil)

(defn .disasm
  "Gets the assembly for the current function."
  [&opt n]
  (def frame (.frame n))
  (def func (frame :function))
  (disasm func))

(defn .bytecode
  "Get the bytecode for the current function."
  [&opt n]
  ((.disasm n) 'bytecode))

(defn .ppasm
  "Pretty prints the assembly for the current function"
  [&opt n]
  (def frame (.frame n))
  (def func (frame :function))
  (def dasm (disasm func))
  (def bytecode (dasm 'bytecode))
  (def pc (frame :pc))
  (def sourcemap (dasm 'sourcemap))
  (var last-loc [-2 -2])
  (print "\n  function:   " (dasm 'name) " [" (in dasm 'source "") "]")
  (when-let [constants (dasm 'constants)]
    (printf "  constants:  %.4Q\n" constants))
  (printf "  slots:      %.4Q\n\n" (frame :slots))
  (def padding (string/repeat " " 20))
  (loop [i :range [0 (length bytecode)]
         :let [instr (bytecode i)]]
    (prin (if (= (tuple/type instr) :brackets) "*" " "))
    (prin (if (= i pc) "> " "  "))
    (printf "\e[33m%.20s\e[0m" (string (string/join (map string instr) " ") padding))
    (when sourcemap
      (let [[sl sc] (sourcemap i)
            loc [sl sc]]
        (when (not= loc last-loc)
          (set last-loc loc)
          (prin " # line " sl ", column " sc))))
    (print))
  (print))

(defn .source
  "Show the source code for the function being debugged."
  [&opt n]
  (def frame (.frame n))
  (def s (frame :source))
  (def all-source (slurp s))
  (print "\n\e[33m" all-source "\e[0m\n"))

(defn .breakall
  "Set breakpoints on all instructions in the current function."
  [&opt n]
  (def fun (.fn n))
  (def bytecode (.bytecode n))
  (for i 0 (length bytecode)
    (debug/fbreak fun i))
  (print "Set " (length bytecode) " breakpoints in " fun))

(defn .clearall
  "Clear all breakpoints on the current function."
  [&opt n]
  (def fun (.fn n))
  (def bytecode (.bytecode n))
  (for i 0 (length bytecode)
    (debug/unfbreak fun i))
  (print "Cleared " (length bytecode) " breakpoints in " fun))

(defn .break
  "Set breakpoint at the current pc."
  []
  (def frame (.frame))
  (def fun (frame :function))
  (def pc (frame :pc))
  (debug/fbreak fun pc)
  (print "Set breakpoint in " fun " at pc=" pc))

(defn .clear
  "Clear the current breakpoint"
  []
  (def frame (.frame))
  (def fun (frame :function))
  (def pc (frame :pc))
  (debug/unfbreak fun pc)
  (print "Cleared breakpoint in " fun " at pc=" pc))

(defn .next
  "Go to the next breakpoint."
  [&opt n]
  (var res nil)
  (for i 0 (or n 1)
    (set res (resume (.fiber))))
  res)

(defn .nextc
  "Go to the next breakpoint, clearing the current breakpoint."
  [&opt n]
  (.clear)
  (.next n))

(defn .step
  "Execute the next n instructions."
  [&opt n]
  (var res nil)
  (for i 0 (or n 1)
    (set res (debug/step (.fiber))))
  res)
