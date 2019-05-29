### cook.janet
###
### Library to help build janet natives and other
### build artifacts.
###
### Copyright 2019 © Calvin Rose

#
# Basic Path Settings
#

# Windows is the OS outlier
(def- is-win (= (os/which) :windows))
(def- is-mac (= (os/which) :macos))
(def- sep (if is-win "\\" "/"))
(def- objext (if is-win ".obj" ".o"))
(def- modext (if is-win ".dll" ".so"))

#
# Rule Engine
#

(defn- getrules []
  (def rules (dyn :rules))
  (if rules rules (setdyn :rules @{})))

(defn- gettarget [target]
  (def item ((getrules) target))
  (unless item (error (string "No rule for target " target)))
  item)

(defn- rule-impl
  [target deps thunk &opt phony]
  (put (getrules) target @[(array/slice deps) thunk phony]))

(defmacro rule
  "Add a rule to the rule graph."
  [target deps & body]
  ~(,rule-impl ,target ,deps (fn [] nil ,;body)))

(defmacro phony
  "Add a phony rule to the rule graph. A phony rule will run every time
  (it is always considered out of date). Phony rules are good for defining
  user facing tasks."
  [target deps & body]
  ~(,rule-impl ,target ,deps (fn [] nil ,;body) true))

(defn add-dep
  "Add a dependency to an existing rule. Useful for extending phony
  rules or extending the dependency graph of existing rules."
  [target dep]
  (def [deps] (gettarget target))
  (array/push deps dep))

(defn- add-thunk
  [target more]
  (def item (gettarget target))
  (def [_ thunk] item)
  (put item 1 (fn [] (more) (thunk))))

(defmacro add-body
  "Add recipe code to an existing rule. This makes existing rules do more but
  does not modify the dependency graph."
  [target & body]
  ~(,add-thunk ,target (fn [] ,;body)))

(defn- needs-build
  [dest src]
  (let [mod-dest (os/stat dest :modified)
        mod-src (os/stat src :modified)]
    (< mod-dest mod-src)))

(defn- needs-build-some
  [dest sources]
  (def f (file/open dest))
  (if (not f) (break true))
  (file/close f)
  (some (partial needs-build dest) sources))

(defn do-rule
  "Evaluate a given rule."
  [target]
  (def item ((getrules) target))
  (unless item
    (if (os/stat target :mode)
      (break target)
      (error (string "No rule for file " target " found."))))
  (def [deps thunk phony] item)
  (def realdeps (seq [dep :in deps :let [x (do-rule dep)] :when x] x))
  (when (or phony (needs-build-some target realdeps))
    (thunk))
  (unless phony target))

(def- _env (fiber/getenv (fiber/current)))
(defn- import-rules*
  [path & args]
  (def [realpath] (module/find path))
  (def env (make-env))
  (loop [k :keys _env :when (symbol? k)]
     (unless ((_env k) :private) (put env k (_env k))))
  (def currenv (fiber/getenv (fiber/current)))
  (loop [k :keys currenv :when (keyword? k)]
     (put env k (currenv k)))
  (require path :env env ;args)
  (when-let [rules (env :rules)] (merge-into (getrules) rules)))

(defmacro import-rules
  "Import another file that defines more cook rules. This ruleset
  is merged into the current ruleset."
  [path & args]
  ~(,import-rules* ,(string path) ,;args))

#
# Configuration
#

# Installation settings
(def JANET_MODPATH (or (os/getenv "JANET_MODPATH") module/*syspath*))
(def JANET_HEADERPATH (or (os/getenv "JANET_HEADERPATH") module/*headerpath*))
(def JANET_BINPATH (or (os/getenv "JANET_BINPATH") (unless is-win "/usr/local/bin")))
                    
# Compilation settings
(def OPTIMIZE (or (os/getenv "OPTIMIZE") 2))
(def CC (or (os/getenv "CC") (if is-win "cl" "cc")))
(def LD (or (os/getenv "LINKER") (if is-win "link" CC)))
(def LDFLAGS (or (os/getenv "LFLAGS")
                 (if is-win " /nologo"
                   (string " -shared"
                           (if is-mac " -undefined dynamic_lookup" "")))))
(def CFLAGS (or (os/getenv "CFLAGS") (if is-win "" " -std=c99 -Wall -Wextra -fpic")))

(defn- opt
  "Get an option, allowing overrides via dynamic bindings AND some
  default value dflt if no dynamic binding is set."
  [opts key dflt]
  (def ret (or (opts key) (dyn key dflt)))
  (if (= nil ret)
    (error (string "option :" key " not set")))
  ret)

#
# OS and shell helpers
#

(defn shell
  "Do a shell command"
  [& args]
  (def cmd (string/join args))
  (print cmd)
  (def res (os/shell cmd))
  (unless (zero? res)
    (error (string "command exited with status " res))))

(defn rm
  "Remove a directory and all sub directories."
  [path]
  (if (= (os/stat path :mode) :directory)
    (do
      (each subpath (os/dir path)
        (rm (string path sep subpath)))
      (os/rmdir path))
    (os/rm path)))

(defn copy
  "Copy a file or directory recursively from one location to another."
  [src dest]
  (shell (if is-win "xcopy " "cp -rf ") `"` src `" "` dest (if is-win `" /h /y /e` `"`)))

#
# C Compilation
#

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
  (def pre (if is-win "/D" "-D"))
  (if value
    (string pre define "=" value)
    (string pre define)))

(defn- make-defines
  "Generate many defines. Takes a dictionary of defines. If a value is
  true, generates -DNAME (/DNAME on windows), otherwise -DNAME=value."
  [defines]
  (seq [[d v] :pairs defines] (make-define d (if (not= v true) v))))

(defn- getcflags
  "Generate the c flags from the input options."
  [opts]
  (string (opt opts :cflags CFLAGS)
          (if is-win " /I\"" " \"-I")
          (opt opts :headerpath JANET_HEADERPATH)
          `"`
          (if is-win " /O\"" " \"-O")
          (opt opts :optimize OPTIMIZE)
          `"`))

(defn- compile-c
  "Compile a C file into an object file."
  [opts src dest]
  (def cc (opt opts :compiler CC))
  (def cflags (getcflags opts))
  (def defines (interpose " " (make-defines (opt opts :defines {}))))
  (rule dest [src]
        (if is-win
          (shell cc " " ;defines " /nologo /c " cflags " /Fo\"" dest `" "` src `"`)
          (shell cc " -c '" src "' " ;defines " " cflags " -o '" dest `'`))))

(defn- link-c
  "Link a number of object files together."
  [opts target & objects]
  (def ld (opt opts :linker LD))
  (def cflags (getcflags opts))
  (def lflags (opt opts :lflags LDFLAGS))
  (def olist (string/join objects `" "`))
  (rule target objects
        (if is-win
          (shell ld " " lflags " /DLL /OUT:" target ` "` olist `" "` (opt opts :headerpath JANET_HEADERPATH) `"\\janet.lib`)
          (shell ld " " cflags ` -o "` target `" "` olist `" ` lflags))))

(defn- create-buffer-c
  "Inline raw byte file as a c file."
  [source dest name]
  (rule dest [source]
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

#
# Declaring Artifacts - used in project.janet, targets specifically
# tailored for janet.
#

(defn- install-rule
  "Add install and uninstall rule for moving file from src into destdir."
  [src destdir]
  (def parts (string/split sep src))
  (def name (last parts))
  (add-body "install"
            (try (os/mkdir destdir) ([err] nil))
            (copy src destdir))
  (add-body "uninstall"
            (def path (string destdir sep name))
            (print "removing " path)
            (try (rm path) ([err]
                            (unless (= err "No such file or directory")
                              (error err))))))

(defn declare-native
  "Declare a native binary. This is a shared library that can be loaded
  dynamically by a janet runtime."
  [&keys opts]
  (def sources (opts :source))
  (def name (opts :name))
  (def lname (lib-name name))
  (loop [src :in sources]
    (compile-c opts src (object-name src)))
  (def objects (map object-name sources))
  (when-let [embedded (opts :embedded)]
            (loop [src :in embedded]
              (def c-src (embed-c-name src))
              (def o-src (embed-o-name src))
              (array/push objects o-src)
              (create-buffer-c src c-src (embed-name src))
              (compile-c opts c-src o-src)))
  (link-c opts lname ;objects)
  (add-dep "build" lname)
  (def path (opt opts :modpath JANET_MODPATH))
  (install-rule lname path))

(defn declare-source
  "Create a Janet modules. This does not actually build the module(s),
  but registers it for packaging and installation."
  [&keys opts]
  (def sources (opts :source))
  (def path (opt opts :modpath JANET_MODPATH))
  (each s sources
    (install-rule s path)))

(defn declare-binscript
  "Declare a janet file to be installed as an executable script."
  [&keys opts]
  (def main (opts :main))
  (def binpath (opt opts :binpath JANET_BINPATH))
  (install-rule main binpath))

(defn declare-archive
  "Build a janet archive. This is a file that bundles together many janet
  scripts into a janet image. This file can the be moved to any machine with
  a janet vm and the required dependencies and run there."
  [&keys opts]
  (def entry (opts :entry))
  (def name (opts :name))
  (def iname (string "build" sep name ".jimage"))
  (rule iname (or (opts :deps) [])
        (spit iname (make-image (require entry))))
  (def path (opt opts :modpath JANET_MODPATH))
  (install-rule iname path))

(defn declare-project
  "Define your project metadata. This should
  be the first declaration in a project.janet file.
  Also sets up basic phony targets like clean, build, test, etc."
  [&keys meta]
  (setdyn :project meta)
  (try (os/mkdir "build") ([err] nil))
  (phony "build" [])
  (phony "install" ["build"] (print "Installed."))
  (phony "uninstall" [] (print "Uninstalled."))
  (phony "clean" [] (rm "build") (print "Deleted build directory."))
  (phony "test" ["build"]
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
         (print "All tests passed.")))
