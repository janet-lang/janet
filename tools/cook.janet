# Library to help build janet natives and other
# build artifacts.
# Copyright 2019 © Calvin Rose

# Windows is the OS outlier
(def- is-win (= (os/which) :windows))
(def- is-mac (= (os/which) :macos))
(def- sep (if is-win "\\" "/"))
(def- objext (if is-win ".obj" ".o"))
(def- modext (if is-win ".dll" ".so"))

# Get default paths and options from environment
(def prefix (or (os/getenv "PREFIX")
                (if is-win "C:\\Janet" "/usr/local")))
(def bindir (or (os/getenv "BINDIR")
                (string prefix sep "bin")))
(def libdir (or (os/getenv "LIBDIR")
                (string prefix sep (if is-win "Library" "lib/janet"))))
(def includedir (or (os/getenv "INCLUDEDIR") module/*headerpath*))
(def optimize (or (os/getenv "OPTIMIZE") 2))
(def CC (or (os/getenv "CC") (if is-win "cl" "cc")))

(defn artifact
  "Add an artifact. An artifact is an item that can be installed
  or otherwise depended upon after being built."
  [x]
  (let [as (dyn :artifacts)]
    (array/push (or as (setdyn :artifacts @[])) x)))

(defn- add-command
  "Add a build command."
  [cmd]
  (let [cmds (dyn :commands)]
    (array/push (or cmds (setdyn :commands @[])) cmd)))

(defn shell
  "Do a shell command"
  [& args]
  (add-command (string ;args)))

(defmacro delay-build
  "Delay an express to build time."
  [& expr]
  ~(,add-command (fn [] ,;expr)))

(defn- copy
  "Copy a file from one location to another."
  [src dest]
  (shell (if is-win "robocopy " "cp -rf ") src " " dest (if is-win " /s /e" "")))

(defn- needs-build
  [dest src]
  "Check if dest is older than src. Used for checking if a file should be updated."
  (def f (file/open dest))
  (if (not f) (break true))
  (file/close f)
  (let [mod-dest (os/stat dest :modified)
        mod-src (os/stat src :modified)]
    (< mod-dest mod-src)))

(defn- needs-build-some
  [f others]
  (some (partial needs-build f) others))

(defn- embed-name
  "Rename a janet symbol for embedding."
  [path]
  (->> path
       (string/replace-all sep "___")
       (string/replace-all ".janet" "")))

(defn- embed-c-name
  "Rename a janet file for embedding."
  [path]
  (->> path
       (string/replace-all sep "___")
       (string/replace-all ".janet" ".janet.c")
       (string "build" sep)))

(defn- embed-o-name
  "Get object file for c file."
  [path]
  (->> path
       (string/replace-all sep "___")
       (string/replace-all ".janet" (string ".janet" objext))
       (string "build" sep)))

(defn- object-name
  "Rename a source file so it can be built in a flat source tree."
  [path]
  (->> path
       (string/replace-all sep "___")
       (string/replace-all ".c" (if is-win ".obj" ".o"))
       (string "build" sep)))

(defn- lib-name
  "Generate name for dynamic library."
  [name]
  (string "build" sep name modext))

(defn- make-define
  "Generate strings for adding custom defines to the compiler."
  [define value]
  (def prefix (if is-win "/D" "-D"))
  (if value
    (string prefix define "=" value)
    (string prefix define)))

(defn- make-defines
  "Generate many defines. Takes a dictionary of defines. If a value is
  true, generates -DNAME (/DNAME on windows), otherwise -DNAME=value."
  [defines]
  (seq [[d v] :pairs defines] (make-define d (if (not= v true) v))))

# Defaults
(def LD (if is-win
          "link"
          (string CC
                  " -shared"
                  (if is-mac " -undefined dynamic_lookup" ""))))
(def CFLAGS (string
              (if is-win "/I" "-I")
              includedir
              (if is-win " /O" " -std=c99 -Wall -Wextra -fpic -O")
              optimize))

(defn- compile-c
  "Compile a C file into an object file. Delayed."
  [opts src dest]
  (def cc (or (opts :compiler) CC))
  (def cflags (or (opts :cflags) CFLAGS))
  (def defines (interpose " " (make-defines (or (opts :defines) {}))))
  (if (needs-build dest src)
    (if is-win
      (shell cc " " ;defines " /nologo /c " cflags " /Fo" dest " " src)
      (shell cc " -c " src " " ;defines " " cflags " -o " dest))))

(defn- link-c
  "Link a number of object files together. Delayed."
  [opts target & objects]
  (def ld (or (opts :linker) LD))
  (def cflags (or (opts :cflags) CFLAGS))
  (def lflags (or (opts :lflags) ""))
  (def olist (string/join objects " "))
  (if (needs-build-some target objects)
    (if is-win
      (shell ld " /DLL /OUT:" target " " olist " %JANET_PATH%\\janet.lib")
      (shell ld " " cflags " -o " target " " olist " " lflags))))

(defn- create-buffer-c
  "Inline raw byte file as a c file. Immediate."
  [source dest name]
  (when (needs-build dest source)
    (def f (file/open source :r))
    (if (not f) (error (string "file " f " not found")))
    (def out (file/open dest :w))
    (def chunks (seq [b :in (file/read f :all)] (string b)))
    (file/write out
                "#include <janet.h>\n"
                "static const unsigned char bytes[] = {"
                ;(interpose ", " chunks)
                "};\n\n"
                "const unsigned char *" name "_embed = bytes;\n"
                "size_t " name "_embed_size = sizeof(bytes);\n")
    (file/close out)
    (file/close f)))

# Installation Helpers

(defn- prep-install
  [dir]
  (try (os/mkdir dir) ([err] nil)))

(defn- install-janet-module
  "Install a janet source module."
  [name]
  (prep-install libdir)
  (copy name libdir))

(defn- install-native-module
  "Install a native module."
  [name]
  (prep-install libdir)
  (copy name libdir))

(defn- install-binscript
  "Install a binscript."
  [name]
  (prep-install bindir)
  (copy name bindir))

# Declaring Artifacts - used in project.janet

(defn declare-native
  "Build a native binary. This is a shared library that can be loaded
  dynamically by a janet runtime."
  [& opts]
  (def opt-table (table ;opts))
  (def sources (opt-table :source))
  (def name (opt-table :name))
  (def lname (lib-name name))
  (artifact [lname :native opt-table])
  (loop [src :in sources]
    (compile-c opt-table src (object-name src)))
  (def objects (map object-name sources))
  (when-let [embedded (opt-table :embedded)]
            (loop [src :in embedded]
              (def c-src (embed-c-name src))
              (def o-src (embed-o-name src))
              (array/push objects o-src)
              (delay-build (create-buffer-c src c-src (embed-name src)))
              (compile-c opt-table c-src o-src)))
  (link-c opt-table lname ;objects))

(defn declare-source
  "Create a Janet modules. This does not actually build the module(s),
  but registers it for packaging and installation."
  [& opts]
  (def opt-table (table ;opts))
  (def sources (opt-table :source))
  (each s sources
    (artifact [s :janet opt-table])))

(defn declare-binscript
  "Declare a janet file to be installed as an executable script."
  [& opts]
  (def opt-table (table ;opts))
  (def main (opt-table :main))
  (artifact [main :binscript opt-table]))

(defn declare-archive
  "Build a janet archive. This is a file that bundles together many janet
  scripts into a janet image. This file can the be moved to any machine with
  a janet vm and the required dependencies and run there."
  [& opts]
  (def opt-table (table ;opts))
  (def entry (opt-table :entry))
  (def name (opt-table :name))
  (def iname (string "build" sep name ".jimage"))
  (artifact [iname :image opt-table])
  (delay-build (spit iname (make-image (require entry)))))

(defn declare-project
  "Define your project metadata."
  [&keys meta]
  (setdyn :project meta))

# Tool usage - called from tool

(defn- rm
  "Remove a directory and all sub directories."
  [path]
  (if (= (os/stat path :mode) :directory)
    (do
      (each subpath (os/dir path)
        (rm (string path sep subpath)))
      (os/rmdir path))
    (os/rm path)))

(defn- flush-commands
  "Run all pending commands."
  []
  (os/mkdir "build")
  (when-let [cmds (dyn :commands)]
            (each cmd cmds
              (if (bytes? cmd)
                (do
                  (print cmd)
                  (def res (os/shell cmd))
                  (unless (zero? res)
                    (error (string "command exited with status " res))))
                (cmd)))
            (setdyn :commands @[])))

(defn clean
  "Remove all built artifacts."
  []
  (print "cleaning...")
  (rm "build"))

(defn build
  "Build all artifacts."
  []
  (print "building...")
  (flush-commands))

(defn install
  "Install all artifacts."
  []
  (flush-commands)
  (print "installing...")
  (each [name kind opts] (dyn :artifacts ())
    (case kind
      :janet (install-janet-module name)
      :image (install-janet-module name)
      :native (install-native-module name)
      :binscript (install-binscript name)))
  (flush-commands))

(defn test
  "Run all tests. This means executing janet files in the test directory."
  []
  (flush-commands)
  (print "testing...")
  (defn dodir
    [dir]
    (each sub (os/dir dir)
      (def ndir (string dir sep sub))
      (case (os/stat ndir :mode)
        :file (when (string/has-suffix? ".janet" ndir)
                (print "running " ndir " ...")
                (dofile ndir :exit true))
        :directory (dodir ndir))))
  (dodir "test")
  (print "All tests passed."))
