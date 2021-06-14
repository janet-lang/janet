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
  (setdyn :cppflags @["-std=c99" "-Wall" "-Wextra"])
  (setdyn :dynamic-lflags @["-shared" "-lpthread"])
  (setdyn :dynamic-cflags @[])
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

  # Get flags
  (while (< i len)
    (if-let [m (peg/match argpeg (args i))]
      (if (= 2 (length m))
        (let [[key value] m]
          (setdyn (keyword key) value))
        (setdyn (keyword (m 0)) true))
      (break))
    (++ i))

  # Run subcommand
  (if (= i len)
    (commands/help)
    (do
      (if-let [com (get commands/subcommands (args i))]
        (com ;(tuple/slice args (+ i 1)))
        (do
          (print "invalid command " (args i))
          (commands/help))))))
