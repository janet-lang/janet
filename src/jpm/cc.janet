###
### C and C++ compiler rule utilties
###

(use ./config)
(use ./rules)
(use ./shutil)

(def- entry-replacer
  "Convert url with potential bad characters into an entry-name"
  (peg/compile ~(% (any (+ '(range "AZ" "az" "09" "__") (/ '1 ,|(string "_" ($ 0) "_")))))))

(defn entry-replace
  "Escape special characters in the entry-name"
  [name]
  (get (peg/match entry-replacer name) 0))

(defn embed-name
  "Rename a janet symbol for embedding."
  [path]
  (->> path
       (string/replace-all "\\" "___")
       (string/replace-all "/" "___")
       (string/replace-all ".janet" "")))

(defn out-path
  "Take a source file path and convert it to an output path."
  [path from-ext to-ext]
  (->> path
       (string/replace-all "\\" "___")
       (string/replace-all "/" "___")
       (string/replace-all from-ext to-ext)
       (string "build/")))

(defn make-define
  "Generate strings for adding custom defines to the compiler."
  [define value]
  (if value
    (string "-D" define "=" value)
    (string "-D" define)))

(defn make-defines
  "Generate many defines. Takes a dictionary of defines. If a value is
  true, generates -DNAME (/DNAME on windows), otherwise -DNAME=value."
  [defines]
  (seq [[d v] :pairs defines] (make-define d (if (not= v true) v))))

(defn- getflags
  "Generate the c flags from the input options."
  [opts compiler]
  (def flags (if (= compiler :cc) :cflags :cppflags))
  @[;(opt opts flags)
    (string "-I" (dyn:headerpath))
    (string "-I" (dyn:modpath))
    (string "-O" (opt opts :optimize))])

(defn entry-name
  "Name of symbol that enters static compilation of a module."
  [name]
  (string "janet_module_entry_" (entry-replace name)))

(defn compile-c
  "Compile a C file into an object file."
  [compiler opts src dest &opt static?]
  (def cc (opt opts compiler))
  (def cflags [;(getflags opts compiler) ;(if static? [] (dyn :dynamic-cflags))])
  (def entry-defines (if-let [n (and static? (opts :entry-name))]
                       [(make-define "JANET_ENTRY_NAME" n)]
                       []))
  (def defines [;(make-defines (opt opts :defines {})) ;entry-defines])
  (def headers (or (opts :headers) []))
  (rule dest [src ;headers]
        (print "compiling " src " to " dest "...")
        (create-dirs dest)
        (if (dyn :is-msvc)
          (shell cc ;defines "/c" ;cflags (string "/Fo" dest) src)
          (shell cc "-c" src ;defines ;cflags "-o" dest))))

(defn link-c
  "Link C or C++ object files together to make a native module."
  [has-cpp opts target & objects]
  (def linker (dyn (if has-cpp :c++-link :cc-link)))
  (def cflags (getflags opts (if has-cpp :cppflags :cflags)))
  (def lflags [;(opt opts :lflags)
               ;(if (opts :static) [] (dyn:dynamic-lflags))])
  (def deplibs (get opts :native-deps []))
  (def dep-ldflags (seq [x :in deplibs] (string (dyn:modpath) "/" x (dyn:modext))))
  # Use import libs on windows - we need an import lib to link natives to other natives.
  (def dep-importlibs (seq [x :in deplibs] (string (dyn:modpath) "/" x ".lib")))
  (def ldflags [;(opt opts :ldflags []) ;dep-ldflags])
  (rule target objects
        (print "linking " target "...")
        (create-dirs target)
        (if (dyn :is-msvc)
          (shell linker ;ldflags (string "/OUT:" target) ;objects
                 (string (dyn:headerpath) "/janet.lib") ;dep-importlibs ;lflags)
          (shell linker ;cflags ;ldflags `-o` target ;objects ;lflags))))

(defn archive-c
  "Link object files together to make a static library."
  [opts target & objects]
  (def ar (opt opts :ar))
  (rule target objects
        (print "creating static library " target "...")
        (create-dirs target)
        (if (dyn :is-msvc)
          (shell ar "/nologo" (string "/out:" target) ;objects)
          (shell ar "rcs" target ;objects))))

#
# Standalone C compilation
#

(defn create-buffer-c-impl
  [bytes dest name]
  (create-dirs dest)
  (def out (file/open dest :w))
  (def chunks (seq [b :in bytes] (string b)))
  (file/write out
              "#include <janet.h>\n"
              "static const unsigned char bytes[] = {"
              (string/join (interpose ", " chunks))
              "};\n\n"
              "const unsigned char *" name "_embed = bytes;\n"
              "size_t " name "_embed_size = sizeof(bytes);\n")
  (file/close out))

(defn create-buffer-c
  "Inline raw byte file as a c file."
  [source dest name]
  (rule dest [source]
        (print "generating " dest "...")
        (create-dirs dest)
        (with [f (file/open source :r)]
          (create-buffer-c-impl (:read f :all) dest name))))

(defn modpath-to-meta
  "Get the meta file path (.meta.janet) corresponding to a native module path (.so)."
  [path]
  (string (string/slice path 0 (- (length (dyn :modext)))) "meta.janet"))

(defn modpath-to-static
  "Get the static library (.a) path corresponding to a native module path (.so)."
  [path]
  (string (string/slice path 0 (- -1 (length (dyn :modext)))) (dyn :statext)))

(defn make-bin-source
  [declarations lookup-into-invocations no-core]
  (string
    declarations
    ```

int main(int argc, const char **argv) {

#if defined(JANET_PRF)
    uint8_t hash_key[JANET_HASH_KEY_SIZE + 1];
#ifdef JANET_REDUCED_OS
    char *envvar = NULL;
#else
    char *envvar = getenv("JANET_HASHSEED");
#endif
    if (NULL != envvar) {
        strncpy((char *) hash_key, envvar, sizeof(hash_key) - 1);
    } else if (janet_cryptorand(hash_key, JANET_HASH_KEY_SIZE) != 0) {
        fputs("unable to initialize janet PRF hash function.\n", stderr);
        return 1;
    }
    janet_init_hash_key(hash_key);
#endif

    janet_init();

    ```
    (if no-core
    ```
    /* Get core env */
    JanetTable *env = janet_table(8);
    JanetTable *lookup = janet_core_lookup_table(NULL);
    JanetTable *temptab;
    int handle = janet_gclock();
    ```
    ```
    /* Get core env */
    JanetTable *env = janet_core_env(NULL);
    JanetTable *lookup = janet_env_lookup(env);
    JanetTable *temptab;
    int handle = janet_gclock();
    ```)
    lookup-into-invocations
    ```
    /* Unmarshal bytecode */
    Janet marsh_out = janet_unmarshal(
      janet_payload_image_embed,
      janet_payload_image_embed_size,
      0,
      lookup,
      NULL);

    /* Verify the marshalled object is a function */
    if (!janet_checktype(marsh_out, JANET_FUNCTION)) {
        fprintf(stderr, "invalid bytecode image - expected function.");
        return 1;
    }
    JanetFunction *jfunc = janet_unwrap_function(marsh_out);

    /* Check arity */
    janet_arity(argc, jfunc->def->min_arity, jfunc->def->max_arity);

    /* Collect command line arguments */
    JanetArray *args = janet_array(argc);
    for (int i = 0; i < argc; i++) {
        janet_array_push(args, janet_cstringv(argv[i]));
    }

    /* Create enviornment */
    temptab = env;
    janet_table_put(temptab, janet_ckeywordv("args"), janet_wrap_array(args));
    janet_gcroot(janet_wrap_table(temptab));

    /* Unlock GC */
    janet_gcunlock(handle);

    /* Run everything */
    JanetFiber *fiber = janet_fiber(jfunc, 64, argc, argc ? args->data : NULL);
    fiber->env = temptab;
#ifdef JANET_EV
    janet_gcroot(janet_wrap_fiber(fiber));
    janet_schedule(fiber, janet_wrap_nil());
    janet_loop();
    int status = janet_fiber_status(fiber);
    janet_deinit();
    return status;
#else
    Janet out;
    JanetSignal result = janet_continue(fiber, janet_wrap_nil(), &out);
    if (result != JANET_SIGNAL_OK && result != JANET_SIGNAL_EVENT) {
      janet_stacktrace(fiber, out);
      janet_deinit();
      return result;
    }
    janet_deinit();
    return 0;
#endif
}

```))

(defn create-executable
  "Links an image with libjanet.a (or .lib) to produce an
  executable. Also will try to link native modules into the
  final executable as well."
  [opts source dest no-core]

  # Create executable's janet image
  (def cimage_dest (string dest ".c"))
  (def no-compile (opts :no-compile))
  (rule (if no-compile cimage_dest dest) [source]
        (print "generating executable c source...")
        (create-dirs dest)
        # Load entry environment and get main function.
        (def entry-env (dofile source))
        (def main ((entry-env 'main) :value))
        (def dep-lflags @[])
        (def dep-ldflags @[])

        # Create marshalling dictionary
        (def mdict1 (invert (env-lookup root-env)))
        (def mdict
          (if no-core
            (let [temp @{}]
              (eachp [k v] mdict1
                (if (or (cfunction? k) (abstract? k))
                  (put temp k v)))
              temp)
            mdict1))

        # Load all native modules
        (def prefixes @{})
        (def static-libs @[])
        (loop [[name m] :pairs module/cache
               :let [n (m :native)]
               :when n
               :let [prefix (gensym)]]
          (print "found native " n "...")
          (put prefixes prefix n)
          (array/push static-libs (modpath-to-static n))
          (def oldproto (table/getproto m))
          (table/setproto m nil)
          (loop [[sym value] :pairs (env-lookup m)]
            (put mdict value (symbol prefix sym)))
          (table/setproto m oldproto))

        # Find static modules
        (var has-cpp false)
        (def declarations @"")
        (def lookup-into-invocations @"")
        (loop [[prefix name] :pairs prefixes]
          (def meta (eval-string (slurp (modpath-to-meta name))))
          (if (meta :cpp) (set has-cpp true))
          (buffer/push-string lookup-into-invocations
                              "    temptab = janet_table(0);\n"
                              "    temptab->proto = env;\n"
                              "    " (meta :static-entry) "(temptab);\n"
                              "    janet_env_lookup_into(lookup, temptab, \""
                              prefix
                              "\", 0);\n\n")
          (when-let [lfs (meta :lflags)]
            (array/concat dep-lflags lfs))
          (when-let [lfs (meta :ldflags)]
            (array/concat dep-ldflags lfs))
          (buffer/push-string declarations
                              "extern void "
                              (meta :static-entry)
                              "(JanetTable *);\n"))

        # Build image
        (def image (marshal main mdict))
        # Make image byte buffer
        (create-buffer-c-impl image cimage_dest "janet_payload_image")
        # Append main function
        (spit cimage_dest (make-bin-source declarations lookup-into-invocations no-core) :ab)
        (def oimage_dest (out-path cimage_dest ".c" ".o"))
        # Compile and link final exectable
        (unless no-compile
          (def ldflags [;dep-ldflags ;(opt opts :ldflags []) ;(dyn :janet-ldflags)])
          (def lflags [;static-libs (dyn :libjanet) ;dep-lflags ;(opt opts :lflags) ;(dyn :janet-lflags)])
          (def defines (make-defines (opt opts :defines {})))
          (def cc (opt opts :cc))
          (def cflags [;(getflags opts :cc) ;(dyn :janet-cflags)])
          (print "compiling " cimage_dest " to " oimage_dest "...")
          (create-dirs oimage_dest)
          (if (dyn :is-msvc)
            (shell cc ;defines "/c" ;cflags (string "/Fo" oimage_dest) cimage_dest)
            (shell cc "-c" cimage_dest ;defines ;cflags "-o" oimage_dest))
          (if has-cpp
            (let [linker (opt opts (if (dyn :is-msvc) :cpp-linker :cpp-compiler))
                  cppflags [;(getflags opts :c++) ;(dyn :janet-cflags)]]
              (print "linking " dest "...")
              (if (dyn :is-msvc)
                (shell linker ;ldflags (string "/OUT:" dest) oimage_dest ;lflags)
                (shell linker ;cppflags ;ldflags `-o` dest oimage_dest ;lflags)))
            (let [linker (opt opts (if (dyn :is-msvc) :linker :compiler))]
              (print "linking " dest "...")
              (create-dirs dest)
              (if (dyn :is-msvc)
                (shell linker ;ldflags (string "/OUT:" dest) oimage_dest ;lflags)
                (shell linker ;cflags ;ldflags `-o` dest oimage_dest ;lflags)))))))
