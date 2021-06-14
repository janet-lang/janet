###
### Rule generation for adding native source code
###

(use ./config)
(use ./rules)
(use ./shutil)
(use ./cc)
(use ./pm)

(defn declare-native
  "Declare a native module. This is a shared library that can be loaded
  dynamically by a janet runtime. This also builds a static libary that
  can be used to bundle janet code and native into a single executable."
  [&keys opts]
  (def sources (opts :source))
  (def name (opts :name))
  (def path (dyn:modpath))

  (def modext (dyn:modext))
  (def statext (dyn:statext))

  # Make dynamic module
  (def lname (string "build/" name modext))

  # Get objects to build with
  (var has-cpp false)
  (def objects
    (seq [src :in sources]
      (def suffix
        (cond
          (string/has-suffix? ".cpp" src) ".cpp"
          (string/has-suffix? ".cc" src) ".cc"
          (string/has-suffix? ".c" src) ".c"
          (errorf "unknown source file type: %s, expected .c, .cc, or .cpp" src)))
      (def op (out-path src suffix ".o"))
      (if (= suffix ".c")
        (compile-c :cc opts src op)
        (do (compile-c :c++ opts src op)
          (set has-cpp true)))
      op))

  (when-let [embedded (opts :embedded)]
    (loop [src :in embedded]
      (def c-src (out-path src ".janet" ".janet.c"))
      (def o-src (out-path src ".janet" ".janet.o"))
      (array/push objects o-src)
      (create-buffer-c src c-src (embed-name src))
      (compile-c :cc opts c-src o-src)))
  (link-c has-cpp opts lname ;objects)
  (add-dep "build" lname)
  (install-rule lname path)

  # Add meta file
  (def metaname (modpath-to-meta lname))
  (def ename (entry-name name))
  (rule metaname []
        (print "generating meta file " metaname "...")
        (os/mkdir "build")
        (spit metaname (string/format
                         "# Metadata for static library %s\n\n%.20p"
                         (string name statext)
                         {:static-entry ename
                          :cpp has-cpp
                          :ldflags ~',(opts :ldflags)
                          :lflags ~',(opts :lflags)})))
  (add-dep "build" metaname)
  (install-rule metaname path)

  # Make static module
  (unless (dyn :nostatic)
    (def sname (string "build/" name statext))
    (def opts (merge @{:entry-name ename} opts))
    (def sobjext ".static.o")
    (def sjobjext ".janet.static.o")

    # Get static objects
    (def sobjects
      (seq [src :in sources]
        (def suffix
          (cond
            (string/has-suffix? ".cpp" src) ".cpp"
            (string/has-suffix? ".cc" src) ".cc"
            (string/has-suffix? ".c" src) ".c"
            (errorf "unknown source file type: %s, expected .c, .cc, or .cpp" src)))
        (def op (out-path src suffix sobjext))
        (compile-c (if (= ".c" suffix) :cc :c++) opts src op true)
        op))

    (when-let [embedded (opts :embedded)]
      (loop [src :in embedded]
        (def c-src (out-path src ".janet" ".janet.c"))
        (def o-src (out-path src ".janet" sjobjext))
        (array/push sobjects o-src)
        # Buffer c-src is already declared by dynamic module
        (compile-c :cc opts c-src o-src true)))
    (archive-c opts sname ;sobjects)
    (add-dep "build" sname)
    (install-rule sname path)))

(defn declare-source
  "Create Janet modules. This does not actually build the module(s),
  but registers them for packaging and installation. :source should be an
  array of files and directores to copy into JANET_MODPATH or JANET_PATH.
  :prefix can optionally be given to modify the destination path to be
  (string JANET_PATH prefix source)."
  [&keys {:source sources :prefix prefix}]
  (def path (string (dyn:modpath) "/" (or prefix "")))
  (if (bytes? sources)
    (install-rule sources path)
    (each s sources
      (install-rule s path))))

(defn declare-headers
  "Declare headers for a library installation. Installed headers can be used by other native
  libraries."
  [&keys {:headers headers :prefix prefix}]
  (def path (string (dyn:modpath) "/" (or prefix "")))
  (if (bytes? headers)
    (install-rule headers path)
    (each h headers
      (install-rule h path))))

(defn declare-bin
  "Declare a generic file to be installed as an executable."
  [&keys {:main main}]
  (install-rule main (dyn:binpath)))

(defn declare-executable
  "Declare a janet file to be the entry of a standalone executable program. The entry
  file is evaluated and a main function is looked for in the entry file. This function
  is marshalled into bytecode which is then embedded in a final executable for distribution.\n\n
  This executable can be installed as well to the --binpath given."
  [&keys {:install install :name name :entry entry :headers headers
          :cflags cflags :lflags lflags :deps deps :ldflags ldflags
          :no-compile no-compile :no-core no-core}]
  (def name (if (= (os/which) :windows) (string name ".exe") name))
  (def dest (string "build/" name))
  (create-executable @{:cflags cflags :lflags lflags :ldflags ldflags :no-compile no-compile} entry dest no-core)
  (if no-compile
    (let [cdest (string dest ".c")]
      (add-dep "build" cdest))
    (do
      (add-dep "build" dest)
      (when headers
        (each h headers (add-dep dest h)))
      (when deps
        (each d deps (add-dep dest d)))
      (when install
        (install-rule dest (dyn:binpath))))))

(defn declare-binscript
  ``Declare a janet file to be installed as an executable script. Creates
  a shim on windows. If hardcode is true, will insert code into the script
  such that it will run correctly even when JANET_PATH is changed. if auto-shebang
  is truthy, will also automatically insert a correct shebang line.
  ``
  [&keys {:main main :hardcode-syspath hardcode :is-janet is-janet}]
  (def binpath (dyn:binpath))
  (def auto-shebang (and is-janet (dyn:auto-shebang)))
  (if (or auto-shebang hardcode)
    (let [syspath (dyn:modpath)]
      (def parts (peg/match path-splitter main))
      (def name (last parts))
      (def path (string binpath "/" name))
      (array/push (dyn :installed-files) path)
      (task "install" []
                (def contents
                  (with [f (file/open main)]
                    (def first-line (:read f :line))
                    (def second-line (string/format "(put root-env :syspath %v)\n" syspath))
                    (def rest (:read f :all))
                    (string (if auto-shebang
                              (string "#!" (dyn:binpath) "/janet\n"))
                            first-line (if hardcode second-line) rest)))
                (create-dirs path)
                (spit path contents)
                (unless (= :windows (os/which)) (shell "chmod" "+x" path))))
    (install-rule main binpath))
  # Create a dud batch file when on windows.
  (when (dyn:use-batch-shell)
    (def name (last (peg/match path-splitter main)))
    (def fullname (string binpath "/" name))
    (def bat (string "@echo off\r\njanet \"" fullname "\" %*"))
    (def newname (string binpath "/" name ".bat"))
    (array/push (dyn :installed-files) newname)
    (task "install" []
              (spit newname bat))))

(defn declare-archive
  "Build a janet archive. This is a file that bundles together many janet
  scripts into a janet image. This file can the be moved to any machine with
  a janet vm and the required dependencies and run there."
  [&keys opts]
  (def entry (opts :entry))
  (def name (opts :name))
  (def iname (string "build/" name ".jimage"))
  (rule iname (or (opts :deps) [])
        (create-dirs iname)
        (spit iname (make-image (require entry))))
  (def path (dyn:modpath))
  (add-dep "build" iname)
  (install-rule iname path))

(defn run-tests
  "Run tests on a project in the current directory."
  [&opt root-directory]
  (defn dodir
    [dir]
    (each sub (sort (os/dir dir))
      (def ndir (string dir "/" sub))
      (case (os/stat ndir :mode)
        :file (when (string/has-suffix? ".janet" ndir)
                (print "running " ndir " ...")
                (def result (os/execute [(dyn:janet) ndir] :p))
                (when (not= 0 result)
                  (errorf "non-zero exit code in %s: %d" ndir result)))
        :directory (dodir ndir))))
  (dodir (or root-directory "test"))
  (print "All tests passed."))

(defn declare-project
  "Define your project metadata. This should
  be the first declaration in a project.janet file.
  Also sets up basic task targets like clean, build, test, etc."
  [&keys meta]
  (setdyn :project meta)

  (def installed-files @[])
  (def manifests (find-manifest-dir))
  (def manifest (find-manifest (meta :name)))
  (setdyn :manifest manifest)
  (setdyn :manifest-dir manifests)
  (setdyn :installed-files installed-files)

  (task "build" [])

  (task "manifest" [manifest])
  (rule manifest []
        (print "generating " manifest "...")
        (os/mkdir manifests)
        (def sha (pslurp (string "\"" (dyn:gitpath) "\" rev-parse HEAD")))
        (def url (pslurp (string "\"" (dyn:gitpath) "\" remote get-url origin")))
        (def man
          {:sha (if-not (empty? sha) sha)
           :repo (if-not (empty? url) url)
           :dependencies (array/slice (get meta :dependencies []))
           :paths installed-files})
        (spit manifest (string/format "%j\n" man)))

  (task "install" ["uninstall" "build" manifest]
        (when (dyn :test)
          (run-tests))
        (print "Installed as '" (meta :name) "'."))

  (task "install-deps" []
        (if-let [deps (meta :dependencies)]
          (each dep deps
            (bundle-install dep))
          (print "no dependencies found")))

  (task "uninstall" []
        (uninstall (meta :name)))

  (task "clean" []
        (when (os/stat "./build" :mode)
          (rm "build")
          (print "Deleted build directory.")))

  (task "test" ["build"]
         (run-tests)))
