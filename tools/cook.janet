# Library to help build janet natives and other
# build artifacts.

# Windows is the OS outlier
(def- is-win (= (os/which) :windows))

(defn- shell
  "Do a shell command"
  [& args]
  (print ;args)
  (def res (os/shell (string ;args)))
  (unless (zero? res)
    (print "Error executing command: " ;args)
    (os/exit res)))

(defn- mkdir
  "Make a directory. Not safe for user code."
  [path]
  (if is-win
    (shell "mkdir " path)
    (shell "mkdir -p " path)))

(defn- rm
  "Remove a directory. Not safe for user code."
  [path]
  (if is-win
    (shell "rmdir " path " /s")
    (shell "rm -rf " path)))

(defn- object-name
  "Rename a source file so it can be built in a flat source tree."
  [path]
  (if is-win
    (->> path
         (string/replace-all "\\" "___")
         (string/replace-all ".c" ".obj")
         (string "build\\")))
    (->> path
         (string/replace-all "/" "___")
         (string/replace-all ".c" ".o")
         (string "build/")))

(defn- lib-name
  "Generate name for dynamic library."
  [name]
  (if is-win
    (string "build\\" name ".dll")
    (string "build/" name ".so")))

# Defaults
(def OPTIMIZE 2)
(def CC (if is-win "cl" "cc"))
(def LD (if is-win "link" (string CC " -shared")))
(def CFLAGS (string (if is-win "/0" "-std=c99 -Wall -Wextra -fpic -O") OPTIMIZE))

(defn- compile-c
  "Compile a C file into an object file."
  [opts src dest]
  (def cc (or opts:compiler CC))
  (def cflags (or opts:cflags CFLAGS))
  (if is-win
    (shell cc " /nologo /c " cflags " /Fo" dest " " src)
    (shell cc " " cflags " -o " dest " -c " src)))

(defn- link-c
  "Link a number of object files together."
  [opts target & objects]
  (def ld (or opts:linker LD))
  (def cflags (or opts:cflags CFLAGS))
  (def olist (string/join objects " "))
  (if is-win
    (shell ld "/out:" target "  " olist)
    (shell ld " " cflags " -o " target " " olist)))

# Public

(defn make-native
  "Build a native binary. This is a shared library that can be loaded
  dynamically by a janet runtime."
  [& opts]
  (def opt-table (table ;opts))
  (mkdir "build")
  (loop [src :in opt-table:source]
    (compile-c opt-table src (object-name src)))
  (link-c opt-table (lib-name opt-table:name) ;(map object-name opt-table:source)))

(defn clean
  "Remove all built artifacts."
  []
  (rm "build"))

(defn make-archive
  "Build a janet archive. This is a file that bundles together many janet
  scripts into a janet form. This file can the be moved to any machine with
  a janet vm and the required dependencies and run there."
  [& opts]
  (error "Not Yet Implemented."))

(defn make-binary
  "Make a binary executable that can be run on the current platform. This function
  generates a self contained binary that can be run of the same architecture as the
  build machine, as the current janet vm will be packaged with the output binary."
  [& opts]
  (error "Not Yet Implemented."))
