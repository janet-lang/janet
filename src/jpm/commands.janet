###
### All of the CLI sub commands
###

(use ./config)
(use ./declare)
(use ./rules)
(use ./shutil)
(use ./cc)
(use ./pm)

(defn- help
  []
  (print `
usage: jpm [--key=value, --flag] ... [subcommand] [args] ...

Run from a directory containing a project.janet file to perform operations
on a project, or from anywhere to do operations on the global module cache (modpath).
Commands that need write permission to the modpath are considered privileged commands - in
some environments they may require super user privileges.
Other project-level commands need to have a ./project.janet file in the current directory.

Unprivileged global subcommands:
  help : show this help text
  show-paths : prints the paths that will be used to install things.
  quickbin entry executable : Create an executable from a janet script with a main function.

Privileged global subcommands:
  install (repo or name)... : install artifacts. If a repo is given, install the contents of that
                   git repository, assuming that the repository is a jpm project. If not, build
                   and install the current project.
  uninstall (module)... : uninstall a module. If no module is given, uninstall the module
                       defined by the current directory.
  clear-cache : clear the git cache. Useful for updating dependencies.
  clear-manifest : clear the manifest. Useful for fixing broken installs.
  make-lockfile (lockfile) : Create a lockfile based on repositories in the cache. The
            lockfile will record the exact versions of dependencies used to ensure a reproducible
            build. Lockfiles are best used with applications, not libraries. The default lockfile
            name is lockfile.jdn.
  load-lockfile (lockfile) : Install modules from a lockfile in a reproducible way. The
                             default lockfile name is lockfile.jdn.
  update-pkgs : Update the current package listing from the remote git repository selected.

Privileged project subcommands:
  deps : install dependencies for the current project.
  install : install artifacts of the current project.
  uninstall : uninstall the current project's artifacts.

Unprivileged project subcommands:
  build : build all artifacts
  clean : remove any generated files or artifacts
  test : run tests. Tests should be .janet files in the test/ directory relative to project.janet.
  run rule : run a rule. Can also run custom rules added via (phony "task" [deps...] ...)
             or (rule "ouput.file" [deps...] ...).
  rules : list rules available with run.
  list-installed : list installed packages in the current syspath.
  list-pkgs (search) : list packages in the package listing that the contain the string search.
                       If no search pattern is given, prints the entire package listing.
  rule-tree (root rule) (depth) : Print a nice tree to see what rules depend on other rules.
                                  Optionally provide a root rule to start printing from, and a
                                  max depth to print. Without these options, all rules will print
                                  their full dependency tree.
  debug-repl : Run a repl in the context of the current project.janet file. This lets you run rules and
               otherwise debug the current project.janet file.

Keys are:
  --modpath : The directory to install modules to. Defaults to $JANET_MODPATH, $JANET_PATH, or (dyn :syspath)
  --headerpath : The directory containing janet headers. Defaults to $JANET_HEADERPATH.
  --binpath : The directory to install binaries and scripts. Defaults to $JANET_BINPATH.
  --libpath : The directory containing janet C libraries (libjanet.*). Defaults to $JANET_LIBPATH.
  --compiler : C compiler to use for natives. Defaults to $CC or cc (cl.exe on windows).
  --cpp-compiler : C++ compiler to use for natives. Defaults to $CXX or c++ (cl.exe on windows).
  --archiver : C archiver to use for static libraries. Defaults to $AR ar (lib.exe on windows).
  --linker : C linker to use for linking natives. Defaults to link.exe on windows, not used on
             other platforms.
  --pkglist : URL of git repository for package listing. Defaults to $JANET_PKGLIST or https://github.com/janet-lang/pkgs.git

Flags are:
  --nocolor : Disable color in the jpm repl.
  --verbose : Print shell commands as they are executed.
  --test : If passed to jpm install, runs tests before installing. Will run tests recursively on dependencies.
  --offline : Prevents jpm from going to network to get dependencies - all dependencies should be in the cache or this command will fail.
    `))

(defn- local-rule
  [rule &opt no-deps]
  (import-rules "./project.janet" no-deps)
  (do-rule rule))

(defn show-help
  []
  (print help))

(defn show-paths
  []
  (print "binpath:    " (dyn:binpath))
  (print "modpath:    " (dyn:modpath))
  (print "libpath:    " (dyn:libpath))
  (print "headerpath: " (dyn:headerpath))
  (print "syspath:    " (dyn:syspath)))

(defn build
  []
  (local-rule "build"))

(defn clean
  []
  (local-rule "clean"))

(defn install
  [& repo]
  (if (empty? repo)
    (local-rule "install")
    (each rep repo (bundle-install rep))))

(defn test
  []
  (local-rule "test"))

(defn- uninstall-cmd
  [& what]
  (if (empty? what)
    (local-rule "uninstall")
    (each wha what (uninstall wha))))

(defn deps
  []
  (local-rule "install-deps" true))

(defn- print-rule-tree
  "Show dependencies for a given rule recursively in a nice tree."
  [root depth prefix prefix-part]
  (print prefix root)
  (when-let [{:inputs root-deps} ((getrules) root)]
    (when (pos? depth)
      (def l (-> root-deps length dec))
      (eachp [i d] (sorted root-deps)
        (print-rule-tree
          d (dec depth)
          (string prefix-part (if (= i l) " └─" " ├─"))
          (string prefix-part (if (= i l) "   " " │ ")))))))

(defn show-rule-tree
  [&opt root depth]
  (import-rules "./project.janet")
  (def max-depth (if depth (scan-number depth) math/inf))
  (if root
    (print-rule-tree root max-depth "" "")
    (let [ks (sort (seq [k :keys (dyn :rules)] k))]
      (each k ks (print-rule-tree k max-depth "" "")))))

(defn list-rules
  [&opt ctx]
  (import-rules "./project.janet")
  (def ks (sort (seq [k :keys (dyn :rules)] k)))
  (each k ks (print k)))

(defn list-installed
  []
  (def xs
    (seq [x :in (os/dir (find-manifest-dir))
          :when (string/has-suffix? ".jdn" x)]
      (string/slice x 0 -5)))
  (sort xs)
  (each x xs (print x)))

(defn list-pkgs
  [&opt search]
  (def [ok _] (module/find "pkgs"))
  (unless ok
    (eprint "no local package listing found. Run `jpm update-pkgs` to get listing.")
    (os/exit 1))
  (def pkgs-mod (require "pkgs"))
  (def ps
    (seq [p :keys (get-in pkgs-mod ['packages :value] [])
          :when (if search (string/find search p) true)]
      p))
  (sort ps)
  (each p ps (print p)))

(defn update-pkgs
  []
  (bundle-install (dyn:pkglist)))

(defn quickbin
  [input output]
  (if (= (os/stat output :mode) :file)
    (print "output " output " exists."))
  (create-executable @{:no-compile (dyn :no-compile)} input output (dyn :no-core))
  (do-rule output))

(defn jpm-debug-repl
  []
  (def env
    (try
      (require-jpm "./project.janet")
      ([err f]
        (if (= "cannot open ./project.janet" err)
          (put (make-jpm-env) :project {})
          (propagate err f)))))
  (setdyn :pretty-format (if-not (dyn :nocolor) "%.20Q" "%.20q"))
  (setdyn :err-color (if-not (dyn :nocolor) true))
  (def p (env :project))
  (def name (p :name))
  (if name (print "Project:     " name))
  (if-let [r (p :repo)] (print "Repository:  " r))
  (if-let [a (p :author)] (print "Author:      " a))
  (defn getchunk [buf p]
    (def [line] (parser/where p))
    (getline (string "jpm[" (or name "repl") "]:" line ":" (parser/state p :delimiters) "> ") buf env))
  (repl getchunk nil env))

(def subcommands
  {"build" build
   "clean" clean
   "help" show-help
   "install" install
   "test" test
   "help" help
   "deps" deps
   "debug-repl" jpm-debug-repl
   "rule-tree" show-rule-tree
   "show-paths" show-paths
   "list-installed" list-installed
   "list-pkgs" list-pkgs
   "clear-cache" clear-cache
   "clear-manifest" clear-manifest
   "run" local-rule
   "rules" list-rules
   "update-pkgs" update-pkgs
   "uninstall" uninstall-cmd
   "make-lockfile" make-lockfile
   "load-lockfile" load-lockfile
   "quickbin" quickbin})


