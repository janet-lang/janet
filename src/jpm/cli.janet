###
### Command Line interface for jpm.
###

(use ./config)
(import ./commands)

# Import some submodules to create a jpm env.
(import ./declare :prefix "" :export true)
(import ./rules :prefix "" :export true)
(import ./shutil :prefix "" :export true)
(import ./cc :prefix "" :export true)
(import ./pm :prefix "" :export true)

(def- _env (curenv))

(def- argpeg
  (peg/compile
    '(* "--" '(some (if-not "=" 1)) (+ (* "=" '(any 1)) -1))))

(defn main
  "Script entry."
  [& argv]

  (def- args (tuple/slice argv 1))
  (def- len (length args))
  (var i :private 0)

  # Get env variables
  (def JANET_PATH (os/getenv "JANET_PATH"))
  (def JANET_HEADERPATH (os/getenv "JANET_HEADERPATH"))
  (def JANET_LIBPATH (os/getenv "JANET_LIBPATH"))
  (def JANET_MODPATH (os/getenv "JANET_MODPATH"))
  (def JANET_BINPATH (os/getenv "JANET_BINPATH"))
  (def JANET_PKGLIST (os/getenv "JANET_PKGLIST"))
  (def JANET_GIT (os/getenv "JANET_GIT"))
  (def JANET_OS_WHICH (os/getenv "JANET_OS_WHICH"))
  (def CC (os/getenv "CC"))
  (def CXX (os/getenv "CXX"))
  (def AR (os/getenv "AR"))

  # Set dynamic bindings
  (setdyn :gitpath (or JANET_GIT "git"))
  (setdyn :pkglist (or JANET_PKGLIST "https://github.com/janet-lang/pkgs.git"))
  (setdyn :modpath (or JANET_MODPATH (dyn :syspath)))
  (setdyn :headerpath (or JANET_HEADERPATH "/usr/local/include/janet"))
  (setdyn :libpath (or JANET_LIBPATH "/usr/local/lib"))
  (setdyn :binpath (or JANET_BINPATH "/usr/local/bin"))
  (setdyn :use-batch-shell false)
  (setdyn :cc (or CC "cc"))
  (setdyn :c++ (or CXX "c++"))
  (setdyn :cc-link (or CC "cc"))
  (setdyn :c++-link (or CXX "c++"))
  (setdyn :ar (or AR "ar"))
  (setdyn :lflags @[])
  (setdyn :ldflags @[])
  (setdyn :cflags @["-std=c99" "-Wall" "-Wextra"])
  (setdyn :cppflags @["-std=c++11" "-Wall" "-Wextra"])
  (setdyn :dynamic-lflags @["-shared" "-lpthread"])
  (setdyn :dynamic-cflags @["-fPIC"])
  (setdyn :optimize 2)
  (setdyn :modext ".so")
  (setdyn :statext ".a")
  (setdyn :is-msvc false)
  (setdyn :libjanet (string (dyn :libpath) "/libjanet.a"))
  (setdyn :janet-ldflags @[])
  (setdyn :janet-lflags @["-lm" "-ldl" "-lrt" "-lpthread"])
  (setdyn :janet-cflags @[])
  (setdyn :jpm-env _env)
  (setdyn :janet (dyn :executable))
  (setdyn :auto-shebang true)
  (setdyn :workers nil)
  (setdyn :verbose false)

  # Get flags
  (def cmdbuf @[])
  (var flags-done false)
  (each a args
    (cond
      (= a "--")
      (set flags-done true)

      flags-done
      (array/push cmdbuf a)

      (if-let [m (peg/match argpeg a)]
        (do
          (def key (keyword (get m 0)))
          (def value-parser (get config-dyns key))
          (unless value-parser
            (error (string "unknown cli option " key)))
          (if (= 2 (length m))
            (do
              (def v (value-parser key (get m 1)))
              (setdyn key v))
            (setdyn key true)))
        (array/push cmdbuf a))))

  # Run subcommand
  (if (empty? cmdbuf)
    (commands/help)
    (if-let [com (get commands/subcommands (first cmdbuf))]
        (com ;(slice cmdbuf 1))
        (do
          (print "invalid command " (first cmdbuf))
          (commands/help)))))
