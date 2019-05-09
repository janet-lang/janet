# Library to help build janet natives and other
# build artifacts.

# Windows is the OS outlier
(def- is-win (= (os/which) :windows))
(def- is-mac (= (os/which) :macos))
(def- sep (if is-win "\\" "/"))
(def- objext (if is-win ".obj" ".o"))
(def- modext (if is-win ".dll" ".so"))

(def prefix (or (os/getenv "PREFIX") "/usr/local"))

(defn shell
  "Do a shell command"
  [& args]
  (def cmd (string ;args))
  (print cmd)
  (def res (os/shell cmd))
  (unless (zero? res)
    (error (string "command exited with status " res))))

(defn- rm
  "Remove a directory and all sub directories."
  [path]
  (if (= (os/stat path :mode) :directory)
    (do
      (each subpath (os/dir path)
        (rm (string path sep subpath)))
      (os/rmdir path))
    (os/rm path)))

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
(def OPTIMIZE 2)
(def CC (if is-win "cl" "cc"))
(def LD (if is-win
          "link"
          (string CC
                  " -shared"
                  (if is-mac " -undefined dynamic_lookup" ""))))
(def CFLAGS (string
              (if is-win "/I" "-I")
              module/*syspath*
              (if is-win " /O" " -std=c99 -Wall -Wextra -fpic -O")
              OPTIMIZE))

(defn- compile-c
  "Compile a C file into an object file."
  [opts src dest]
  (def cc (or (opts :compiler) CC))
  (def cflags (or (opts :cflags) CFLAGS))
  (def defines (interpose " " (make-defines (or (opts :defines) {}))))
  (if (needs-build dest src)
    (if is-win
      (shell cc " " ;defines " /nologo /c " cflags " /Fo" dest " " src)
      (shell cc " -c " src " " ;defines " " cflags " -o " dest))))

(defn- link-c
  "Link a number of object files together."
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
  "Inline raw byte file as a c file."
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

# Public

(defn make-native
  "Build a native binary. This is a shared library that can be loaded
  dynamically by a janet runtime."
  [& opts]
  (def opt-table (table ;opts))
  (os/mkdir "build")
  (def sources (opt-table :source))
  (def name (opt-table :name))
  (loop [src :in sources]
    (compile-c opt-table src (object-name src)))
  (def objects (map object-name sources))
  (when-let [embedded (opt-table :embedded)]
    (loop [src :in embedded]
      (def c-src (embed-c-name src))
      (def o-src (embed-o-name src))
      (array/push objects o-src)
      (create-buffer-c src c-src (embed-name src))
      (compile-c opt-table c-src o-src)))
  (link-c opt-table (lib-name name) ;objects))

(defn clean
  "Remove all built artifacts."
  []
  (rm "build"))

(defn make-archive
  "Build a janet archive. This is a file that bundles together many janet
  scripts into a janet image. This file can the be moved to any machine with
  a janet vm and the required dependencies and run there."
  [& opts]
  (def opt-table (table ;opts))
  (os/mkdir "build")
  (def entry (opt-table :entry))
  (def name (opt-table :name))
  (spit (string name ".jimage") (make-image (require entry))))

(defn make-binary
  "Make a binary executable that can be run on the current platform. This function
  generates a self contained binary that can be run of the same architecture as the
  build machine, as the current janet vm will be packaged with the output binary."
  [& opts]
  (error "Not Yet Implemented."))
