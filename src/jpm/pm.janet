###
### Package management functionality
###

(use ./config)
(use ./shutil)
(use ./rules)

(defn- proto-flatten
  [into x]
  (when x
    (proto-flatten into (table/getproto x))
    (merge-into into x))
  into)

(defn make-jpm-env
  "Create an environment that is preloaded with jpm symbols."
  [&opt base-env]
  (default base-env (dyn :jpm-env {}))
  (def env (make-env))
  (loop [k :keys base-env :when (symbol? k)
         :let [x (get base-env k)]]
    (unless (get x :private) (put env k x)))
  (def currenv (proto-flatten @{} (curenv)))
  (loop [k :keys currenv :when (keyword? k)]
    (put env k (currenv k)))
  env)

(defn require-jpm
  "Require a jpm file project file. This is different from a normal require
  in that code is loaded in the jpm environment."
  [path &opt no-deps base-env]
  (unless (os/stat path :mode)
    (error (string "cannot open " path)))
  (def env (make-jpm-env base-env))
  (dofile path :env env :exit true)
  env)

(defn import-rules
  "Import another file that defines more rules. This ruleset
  is merged into the current ruleset."
  [path &opt no-deps base-env]
  (def env (require-jpm path no-deps base-env))
  (when-let [rules (get env :rules)] (merge-into (getrules) rules))
  env)

(defn git
  "Make a call to git."
  [& args]
  (os/execute [(dyn:gitpath) ;args] :px))

(defn install-rule
  "Add install and uninstall rule for moving file from src into destdir."
  [src destdir]
  (def parts (peg/match path-splitter src))
  (def name (last parts))
  (def path (string destdir "/" name))
  (array/push (dyn :installed-files) path)
  (task "install" []
          (os/mkdir destdir)
          (copy src destdir)))

(var- bundle-install-recursive nil)

(defn resolve-bundle-name
  "Convert short bundle names to URLs."
  [bname]
  (if (string/find ":" bname)
    (let [pkgs (try
                 (require "pkgs")
                 ([err]
                   (bundle-install-recursive (dyn:pkglist))
                   (require "pkgs")))
          url (get-in pkgs ['packages :value (symbol bname)])]
      (unless url
        (error (string "bundle " bname " not found.")))
      url)
    bname))

(defn download-bundle
  "Donwload the package source (using git) to the local cache. Return the
  path to the downloaded or cached soure code."
  [url &opt tag]
  (default tag "master")
  (def cache (find-cache))
  (os/mkdir cache)
  (def id (filepath-replace url))
  (def bundle-dir (string cache "/" id))
  (var fresh false)
  (if (dyn :offline)
    (if (not= :directory (os/stat bundle-dir :mode))
      (error (string "did not find cached repository for dependency " url))
      (set fresh true))
    (when (os/mkdir bundle-dir)
      (set fresh true)
      (print "cloning repository " url " to " bundle-dir)
      (unless (zero? (git "clone" url bundle-dir))
        (rimraf bundle-dir)
        (error (string "could not clone git dependency " url)))))
  (def gd (string "--git-dir=" bundle-dir "/.git"))
  (def wt (string "--work-tree=" bundle-dir))
  (unless (or (dyn :offline) fresh)
    (git gd wt "pull" "origin" "master" "--ff-only"))
  (when tag
    (git gd wt "reset" "--hard" tag))
  (unless (dyn :offline)
    (git gd wt "submodule" "update" "--init" "--recursive"))
  bundle-dir)

(defn bundle-install
  "Install a bundle from a git repository."
  [repotab &opt no-deps]
  (def repo (resolve-bundle-name
              (if (string? repotab) repotab (repotab :repo))))
  (def tag (unless (string? repotab) (repotab :tag)))
  (def bdir (download-bundle repo tag))
  (def olddir (os/cwd))
  (defer (os/cd olddir)
    (os/cd bdir)
    (with-dyns [:rules @{}
                :modpath (abspath (dyn:modpath))
                :headerpath (abspath (dyn:headerpath))
                :libpath (abspath (dyn:libpath))
                :binpath (abspath (dyn:binpath))]
      (def dep-env (require-jpm "./project.janet" true))
      (def rules 
        (if no-deps
          ["build" "install"]
          ["install-deps" "build" "install"]))
      (each r rules
        (build-rules (get dep-env :rules {}) r)))))

(set bundle-install-recursive bundle-install)

(defn make-lockfile
  [&opt filename]
  (default filename "lockfile.jdn")
  (def cwd (os/cwd))
  (def packages @[])
  # Read installed modules from manifests
  (def mdir (find-manifest-dir))
  (each man (os/dir mdir)
    (def package (parse (slurp (string mdir "/"  man))))
    (if (and (dictionary? package) (package :repo) (package :sha))
      (array/push packages package)
      (print "Cannot add local or malformed package " mdir "/" man " to lockfile, skipping...")))
  # Put in correct order, such that a package is preceded by all of its dependencies
  (def ordered-packages @[])
  (def resolved @{})
  (while (< (length ordered-packages) (length packages))
    (var made-progress false)
    (each p packages
      (def {:repo r :sha s :dependencies d} p)
      (def dep-urls (map |(if (string? $) $ ($ :repo)) d))
      (unless (resolved r)
        (when (all resolved dep-urls)
          (array/push ordered-packages {:repo r :sha s})
          (set made-progress true)
          (put resolved r true))))
    (unless made-progress
      (error (string/format "could not resolve package order for: %j"
                            (filter (complement resolved) (map |($ :repo) packages))))))
  # Write to file, manual format for better diffs.
  (with [f (file/open filename :w)]
    (with-dyns [:out f]
      (prin "@[")
      (eachk i ordered-packages
        (unless (zero? i)
          (prin "\n  "))
        (prinf "%j" (ordered-packages i)))
      (print "]")))
  (print "created " filename))

(defn load-lockfile
  "Load packages from a lockfile."
  [&opt filename]
  (default filename "lockfile.jdn")
  (def lockarray (parse (slurp filename)))
  (each {:repo url :sha sha} lockarray
    (bundle-install {:repo url :tag sha} true)))

(defn uninstall
  "Uninstall bundle named name"
  [name]
  (def manifest (find-manifest name))
  (when-with [f (file/open manifest)]
    (def man (parse (:read f :all)))
    (each path (get man :paths [])
      (print "removing " path)
      (rm path))
    (print "removing manifest " manifest)
    (:close f) # I hate windows
    (rm manifest)
    (print "Uninstalled.")))

(defmacro post-deps
  "Run code at the top level if jpm dependencies are installed. Build
  code that imports dependencies should be wrapped with this macro, as project.janet
  needs to be able to run successfully even without dependencies installed."
  [& body]
  (unless (dyn :jpm-no-deps)
    ~',(reduce |(eval $1) nil body)))

(defn do-rule
  "Evaluate a given rule in a one-off manner."
  [target]
  (build-rules (dyn :rules) [target]))
