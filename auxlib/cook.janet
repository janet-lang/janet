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
(def- absprefix (if is-win "C:\\" "/"))

#
# Rule Engine
#

(defn- getrules []
  (if-let [rules (dyn :rules)] rules (setdyn :rules @{})))

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

(defn import-rules
  "Import another file that defines more cook rules. This ruleset
  is merged into the current ruleset."
  [path]
  (def env (make-env))
  (unless (os/stat path :mode)
    (error (string "cannot open " path)))
  (loop [k :keys _env :when (symbol? k)]
     (unless ((_env k) :private) (put env k (_env k))))
  (def currenv (fiber/getenv (fiber/current)))
  (loop [k :keys currenv :when (keyword? k)]
     (put env k (currenv k)))
  (dofile path :env env)
  (when-let [rules (env :rules)] (merge-into (getrules) rules)))

#
# Configuration
#

# Installation settings
(def JANET_MODPATH (or (os/getenv "JANET_MODPATH") (dyn :syspath)))
(def JANET_HEADERPATH (os/getenv "JANET_HEADERPATH"))
(def JANET_BINPATH (or (os/getenv "JANET_BINPATH") (unless is-win "/usr/local/bin")))

# Compilation settings
(def- OPTIMIZE (or (os/getenv "OPTIMIZE") 2))
(def- COMPILER (or (os/getenv "COMPILER") (if is-win "cl" "cc")))
(def- LINKER (or (os/getenv "LINKER") (if is-win "link" COMPILER)))
(def- LFLAGS
  (if-let [lflags (os/getenv "LFLAGS")]
    (string/split " " lflags)
    (if is-win ["/nologo" "/DLL"]
      (if is-mac
        ["-shared" "-undefined" "dynamic_lookup"]
        ["-shared"]))))
(def- CFLAGS
  (if-let [cflags (os/getenv "CFLAGS")]
    (string/split " " cflags)
    (if is-win
      ["/nologo"]
      ["-std=c99" "-Wall" "-Wextra" "-fpic"])))

# Some defaults
(def default-cflags CFLAGS)
(def default-lflags LFLAGS)
(def default-cc COMPILER)
(def default-ld LINKER)

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
  (def res (os/execute args :p))
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
  (print "copying " src " to " dest "...")
  (if is-win
    (shell "xcopy" src dest "/y" "/e")
    (shell "cp" "-rf" src dest)))

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
  @[;(opt opts :cflags CFLAGS)
    (string (if is-win "/I" "-I") (opt opts :headerpath JANET_HEADERPATH))
    (string (if is-win "/O" "-O") (opt opts :optimize OPTIMIZE))])

(defn- compile-c
  "Compile a C file into an object file."
  [opts src dest]
  (def cc (opt opts :compiler COMPILER))
  (def cflags (getcflags opts))
  (def defines (interpose " " (make-defines (opt opts :defines {}))))
  (def headers (or (opts :headers) []))
  (rule dest [src ;headers]
        (print "compiling " dest "...")
        (if is-win
          (shell cc ;defines "/c" ;cflags (string "/Fo" dest) src)
          (shell cc "-c" src ;defines ;cflags "-o" dest))))

(defn- link-c
  "Link a number of object files together."
  [opts target & objects]
  (def ld (opt opts :linker LINKER))
  (def cflags (getcflags opts))
  (def lflags (opt opts :lflags LFLAGS))
  (rule target objects
        (print "linking " target "...")
        (if is-win
          (shell ld ;lflags (string "/OUT:" target) ;objects (string (opt opts :headerpath JANET_HEADERPATH) `\\janet.lib`))
          (shell ld ;cflags `-o` target ;objects ;lflags))))

(defn- create-buffer-c
  "Inline raw byte file as a c file."
  [source dest name]
  (rule dest [source]
        (print "generating " dest "...")
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

(defn- abspath
  "Create an absolute path. Does not resolve . and .. (useful for
  generating entries in install manifest file)."
  [path]
  (if (string/has-prefix? absprefix)
    path
    (string (os/cwd) sep path)))

#
# Public utilities
#

(def- filepath-replacer
  "Convert url with potential bad characters into a file path element."
  (peg/compile ~(% (* (+ (/ '(set "<>:\"/\\|?*") "_") '2)))))

(defn repo-id
  "Convert a repo url into a path component that serves as its id."
  [repo]
  (get (peg/match filepath-replacer repo) 0))

(defn find-manifest-dir
  "Get the path to the directory containing manifests for installed
  packages."
  [&opt opts]
  (string (opt (or opts @{}) :modpath JANET_MODPATH) sep ".manifests"))

(defn find-manifest
  "Get the full path of a manifest file given a package name."
  [name &opt opts]
  (string (find-manifest-dir opts) sep name ".txt"))

(defn find-cache
  "Return the path to the global cache."
  [&opt opts]
  (def path (opt (or opts @{}) :modpath JANET_MODPATH))
  (string path sep ".cache"))

(defn uninstall
  "Uninstall bundle named name"
  [name &opt opts]
  (def manifest (find-manifest name opts))
  (def f (file/open manifest :r))
  (unless f (print manifest " does not exist") (break))
  (loop [line :iterate (:read f :line)]
    (def path ((string/split "\n" line) 0))
    (print "removing " path)
    (try (rm path) ([err]
                    (unless (= err "No such file or directory")
                      (error err)))))
  (print "removing " manifest)
  (rm manifest)
  (:close f)
  (print "Uninstalled."))

(defn clear-cache
  "Clear the global git cache."
  [&opt opts]
  (rm (find-cache opts)))

(defn install-git
  "Install a bundle from git. If the bundle is already installed, the bundle
  is reinistalled (but not rebuilt if artifacts are cached)."
  [repo &opt opts]
  (def cache (find-cache opts))
  (os/mkdir cache)
  (def id (repo-id repo))
  (def module-dir (string cache sep id))
  (when (os/mkdir module-dir)
    (os/execute ["git" "clone" repo module-dir] :p))
  (def olddir (os/cwd))
  (os/cd module-dir)
  (try
    (with-dyns [:rules @{}]
      (import-rules "./project.janet")
      (do-rule "install-deps")
      (do-rule "build")
      (do-rule "install"))
    ([err] nil))
  (os/cd olddir))

(defn install-rule
  "Add install and uninstall rule for moving file from src into destdir."
  [src destdir]
  (def parts (string/split sep src))
  (def name (last parts))
  (def path (string destdir sep name))
  (array/push (dyn :installed-files) path)
  (add-body "install"
            (try (os/mkdir destdir) ([err] nil))
            (copy src destdir)))

#
# Declaring Artifacts - used in project.janet, targets specifically
# tailored for janet.
#

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

(defn declare-bin
  "Declare a generic file to be installed as an executable."
  [&keys opts]
  (def main (opts :main))
  (def binpath (opt opts :binpath JANET_BINPATH))
  (install-rule main binpath))

(defn declare-binscript
  "Declare a janet file to be installed as an executable script. Creates
  a shim on windows."
  [&keys opts]
  (def main (opts :main))
  (def binpath (opt opts :binpath JANET_BINPATH))
  (install-rule main binpath)
  # Create a dud batch file when on windows.
  (when is-win
    (def name (last (string/split sep main)))
    (def bat (string "@echo off\r\njanet %~dp0\\" name "%*"))
    (def newname (string binpath sep name ".bat"))
    (add-body "install"
              (spit newname bat))
    (add-body "uninstall"
              (os/rm newname))))

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

  (def installed-files @[])
  (def manifests (find-manifest-dir))
  (def manifest (find-manifest (meta :name)))
  (setdyn :manifest manifest)
  (setdyn :manifest-dir manifests)
  (setdyn :installed-files installed-files)

  (rule "./build" [] (os/mkdir "build"))
  (phony "build" ["./build"])

  (phony "manifest" []
         (print "generating " manifest "...")
         (os/mkdir manifests)
         (spit manifest (string (string/join installed-files "\n") "\n")))
  (phony "install" ["uninstall" "build" "manifest"]
         (print "Installed as '" (meta :name) "'."))

  (phony "install-deps" []
         (if-let [deps (meta :dependencies)]
           (each dep deps
             (install-git dep))
           (print "no dependencies found")))

  (phony "uninstall" []
         (uninstall (meta :name)))

  (phony "clean" []
         (rm "build")
         (print "Deleted build directory."))

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
