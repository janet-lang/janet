###
### Utilties for running shell-like commands
###

(use ./config)

(defn is-win
  "Check if we should assume a DOS-like shell or default
  to posix shell."
  []
  (dyn:use-batch-shell))

(defn find-manifest-dir
  "Get the path to the directory containing manifests for installed
  packages."
  []
  (string (dyn:modpath) "/.manifests"))

(defn find-manifest
  "Get the full path of a manifest file given a package name."
  [name]
  (string (find-manifest-dir) "/" name ".jdn"))

(defn find-cache
  "Return the path to the global cache."
  []
  (def path (dyn:modpath))
  (string path "/.cache"))

(defn rm
  "Remove a directory and all sub directories."
  [path]
  (case (os/lstat path :mode)
    :directory (do
      (each subpath (os/dir path)
        (rm (string path "/" subpath)))
      (os/rmdir path))
    nil nil # do nothing if file does not exist
    # Default, try to remove
    (os/rm path)))

(defn rimraf
  "Hard delete directory tree"
  [path]
  (if (is-win)
    # windows get rid of read-only files
    (when (os/stat path :mode)
      (os/shell (string `rmdir /S /Q "` path `"`)))
    (rm path)))

(defn clear-cache
  "Clear the global git cache."
  []
  (def cache (find-cache))
  (print "clearing cache " cache "...")
  (rimraf cache))

(defn clear-manifest
  "Clear the global installation manifest."
  []
  (def manifest (find-manifest-dir))
  (print "clearing manifests " manifest "...")
  (rimraf manifest))

(defn pslurp
  "Like slurp, but with file/popen instead file/open. Also trims output"
  [cmd]
  (string/trim (with [f (file/popen cmd)] (:read f :all))))

(def path-splitter
  "split paths on / and \\."
  (peg/compile ~(any (* '(any (if-not (set `\/`) 1)) (+ (set `\/`) -1)))))

(defn create-dirs
  "Create all directories needed for a file (mkdir -p)."
  [dest]
  (def segs (peg/match path-splitter dest))
  (for i 1 (length segs)
    (def path (string/join (slice segs 0 i) "/"))
    (unless (empty? path) (os/mkdir path))))

(defn devnull
  []
  (os/open (if (= :windows (os/which)) "NUL" "/dev/null") :rw))

(defn shell
  "Do a shell command"
  [& args]
  (def args (map string args))
  (if (dyn :verbose)
    (print ;(interpose " " args)))
  (if (dyn :silent)
    (with [dn (devnull)]
      (os/execute args :px {:out dn :err dn}))
    (os/execute args :px)))

(defn copy
  "Copy a file or directory recursively from one location to another."
  [src dest]
  (print "copying " src " to " dest "...")
  (if (is-win)
    (let [end (last (peg/match path-splitter src))
          isdir (= (os/stat src :mode) :directory)]
      (shell "C:\\Windows\\System32\\xcopy.exe"
             (string/replace "/" "\\" src) (string/replace "/" "\\" (if isdir (string dest "\\" end) dest))
             "/y" "/s" "/e" "/i"))
    (shell "cp" "-rf" src dest)))

(defn abspath
  "Create an absolute path. Does not resolve . and .. (useful for
  generating entries in install manifest file)."
  [path]
  (if (if (is-win)
        (peg/match '(+ "\\" (* (range "AZ" "az") ":\\")) path)
        (string/has-prefix? "/" path))
    path
    (string (os/cwd) "/" path)))

(def- filepath-replacer
  "Convert url with potential bad characters into a file path element."
  (peg/compile ~(% (any (+ (/ '(set "<>:\"/\\|?*") "_") '1)))))

(defn filepath-replace
  "Remove special characters from a string or path
  to make it into a path segment."
  [repo]
  (get (peg/match filepath-replacer repo) 0))
