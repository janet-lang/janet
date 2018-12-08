# Copyright 2017-2018 (C) Calvin Rose

(do

  (var *should-repl* :private false)
  (var *no-file* :private true)
  (var *raw-stdin* :private false)
  (var *handleopts* :private true)
  (var *exit-on-error* :private true)

  # Flag handlers
  (def handlers :private
    {"h" (fn [&]
           (print "usage: " process/args.0 " [options] scripts...")
           (print
             `Options are:
  -h Show this help
  -v Print the version string
  -s Use raw stdin instead of getline like functionality
  -e Execute a string of janet
  -r Enter the repl after running all scripts
  -p Keep on executing if there is a top level error (persistent)
  -- Stop handling options`)
           (os/exit 0)
           1)
     "v" (fn [&] (print janet/version "-" janet/build) (os/exit 0) 1)
     "s" (fn [&] (:= *raw-stdin* true) (:= *should-repl* true) 1)
     "r" (fn [&] (:= *should-repl* true) 1)
     "p" (fn [&] (:= *exit-on-error* false) 1)
     "-" (fn [&] (:= *handleopts* false) 1)
     "e" (fn [i &]
           (:= *no-file* false)
           (eval (get process/args (+ i 1)))
           2)})

  (defn- dohandler [n i &]
    (def h (get handlers n))
    (if h (h i) (do (print "unknown flag -" n) ((get handlers "h")))))

  # Process arguments
  (var i 1)
  (def lenargs (length process/args))
  (while (< i lenargs)
    (def arg (get process/args i))
    (if (and *handleopts* (= "-" (string/slice arg 0 1)))
      (+= i (dohandler (string/slice arg 1 2) i))
      (do
        (:= *no-file* false)
        (import* _env arg :prefix "" :exit *exit-on-error*)
        (++ i))))

  (when (or *should-repl* *no-file*)
    (if *raw-stdin*
      (repl nil identity)
      (do
        (print (string "Janet " janet/version "-" janet/build "  Copyright (C) 2017-2018 Calvin Rose"))
        (repl (fn [buf p]
                (def [line] (parser/where p))
                (def prompt (string "janet:" line ":" (parser/state p) "> "))
                (getline prompt buf)))))))
