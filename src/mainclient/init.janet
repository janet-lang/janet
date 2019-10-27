# Copyright 2017-2019 (C) Calvin Rose

(do

  (var *should-repl* false)
  (var *no-file* true)
  (var *quiet* false)
  (var *raw-stdin* false)
  (var *handleopts* true)
  (var *exit-on-error* true)
  (var *colorize* true)
  (var *compile-only* false)

  (if-let [jp (os/getenv "JANET_PATH")] (setdyn :syspath jp))
  (if-let [jp (os/getenv "JANET_HEADERPATH")] (setdyn :headerpath jp))
  (def args (dyn :args))

  # Flag handlers
  (def handlers :private
    {"h" (fn [&]
           (print "usage: " (get args 0) " [options] script args...")
           (print
             `Options are:
  -h : Show this help
  -v : Print the version string
  -s : Use raw stdin instead of getline like functionality
  -e code : Execute a string of janet
  -r : Enter the repl after running all scripts
  -p : Keep on executing if there is a top level error (persistent)
  -q : Hide prompt, logo, and repl output (quiet)
  -k : Compile scripts but do not execute
  -m syspath : Set system path for loading global modules
  -c source output : Compile janet source code into an image
  -n : Disable ANSI color output in the repl
  -l path : Execute code in a file before running the main script
  -- : Stop handling options`)
           (os/exit 0)
           1)
     "v" (fn [&] (print janet/version "-" janet/build) (os/exit 0) 1)
     "s" (fn [&] (set *raw-stdin* true) (set *should-repl* true) 1)
     "r" (fn [&] (set *should-repl* true) 1)
     "p" (fn [&] (set *exit-on-error* false) 1)
     "q" (fn [&] (set *quiet* true) 1)
     "k" (fn [&] (set *compile-only* true) (set *exit-on-error* false) 1)
     "n" (fn [&] (set *colorize* false) 1)
     "m" (fn [i &] (setdyn :syspath (get args (+ i 1))) 2)
     "c" (fn [i &]
           (def e (dofile (get args (+ i 1))))
           (spit (get args (+ i 2)) (make-image e))
           (set *no-file* false)
           3)
     "-" (fn [&] (set *handleopts* false) 1)
     "l" (fn [i &]
           (import* (get args (+ i 1))
                    :prefix "" :exit *exit-on-error*)
           2)
     "e" (fn [i &]
           (set *no-file* false)
           (eval-string (get args (+ i 1)))
           2)})

  (defn- dohandler [n i &]
    (def h (get handlers n))
    (if h (h i) (do (print "unknown flag -" n) ((get handlers "h")))))

  (def- safe-forms {'defn true 'defn- true 'defmacro true 'defmacro- true})
  (def- importers {'import true 'import* true 'use true 'dofile true 'require true})
  (defn- evaluator
    [thunk source env where]
    (if *compile-only*
      (when (tuple? source)
        (cond
          (safe-forms (source 0)) (thunk)
          (importers (source 0))
          (do
            (let [[l c] (tuple/sourcemap source)
                  newtup (tuple/setmap (tuple ;source :evaluator evaluator) l c)]
              ((compile newtup env where))))))
      (thunk)))

  # Process arguments
  (var i 0)
  (def lenargs (length args))
  (while (< i lenargs)
    (def arg (get args i))
    (if (and *handleopts* (= "-" (string/slice arg 0 1)))
      (+= i (dohandler (string/slice arg 1 2) i))
      (do
        (set *no-file* false)
        (dofile arg :prefix "" :exit *exit-on-error* :evaluator evaluator)
        (set i lenargs))))

  (when (and (not *compile-only*) (or *should-repl* *no-file*))
    (if-not *quiet*
      (print "Janet " janet/version "-" janet/build "  Copyright (C) 2017-2019 Calvin Rose"))
    (defn noprompt [_] "")
    (defn getprompt [p]
      (def [line] (parser/where p))
      (string "janet:" line ":" (parser/state p :delimiters) "> "))
    (def prompter (if *quiet* noprompt getprompt))
    (defn getstdin [prompt buf]
      (file/write stdout prompt)
      (file/flush stdout)
      (file/read stdin :line buf))
    (def getter (if *raw-stdin* getstdin getline))
    (defn getchunk [buf p]
      (getter (prompter p) buf))
    (def onsig (if *quiet* (fn [x &] x) nil))
    (setdyn :pretty-format (if *colorize* "%.20Q" "%.20q"))
    (setdyn :err-color (if *colorize* true))
    (repl getchunk onsig)))
