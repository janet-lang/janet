###
### Various defaults that can be set at compile time
### and configure the behavior of the module.
###

(def config-dyns 
  "A table of all of the dynamic config bindings."
  @{})

(defn- parse-boolean
  [kw x]
  (case (string/ascii-lower x)
    "f" false
    "0" false
    "false" false
    "off" false
    "no" false
    "t" true
    "1" true
    "on" true
    "yes" true
    "true" true
    (errorf "option :%s, unknown boolean option %s" kw x)))

(defn- parse-integer
  [kw x]
  (if-let [n (scan-number x)]
    (if (not= n (math/floor n))
      (errorf "option :%s, expected integer, got %v" kw x)
      n)
    (errorf "option :%s, expected integer, got %v" kw x)))

(defn- parse-string
  [kw x]
  x)

(def- config-parsers
  "A table of all of the option parsers."
  @{:int parse-integer
    :string parse-string
    :boolean parse-boolean})

(defmacro defdyn
  "Define a function that wraps (dyn :keyword). This will
  allow use of dynamic bindings with static runtime checks."
  [kw parser & meta]
  (put config-dyns kw (get config-parsers parser))
  (let [s (symbol "dyn:" kw)]
    ~(defn ,s ,;meta [&opt dflt]
       (def x (,dyn ,kw dflt))
       (if (= x nil)
         (,errorf "no value found for dynamic binding %v" ,kw)
         x))))

(defn opt
  "Get an option, allowing overrides via dynamic bindings AND some
  default value dflt if no dynamic binding is set."
  [opts key &opt dflt]
  (def ret (or (get opts key) (dyn key dflt)))
  (if (= nil ret)
    (error (string "option :" key " not set")))
  ret)

# All jpm settings.
(defdyn :ar :string)
(defdyn :auto-shebang :string)
(defdyn :binpath :string)
(defdyn :c++ :string)
(defdyn :c++-link :string)
(defdyn :cc :string)
(defdyn :cc-link :string)
(defdyn :cflags nil)
(defdyn :cppflags nil)
(defdyn :dynamic-cflags nil)
(defdyn :dynamic-lflags nil)
(defdyn :gitpath :string)
(defdyn :headerpath :string)
(defdyn :is-msvc :boolean)
(defdyn :janet :string)
(defdyn :janet-cflags nil)
(defdyn :janet-ldflags nil)
(defdyn :janet-lflags nil)
(defdyn :ldflags nil)
(defdyn :lflags nil)
(defdyn :libjanet :string)
(defdyn :libpath :string)
(defdyn :modext nil)
(defdyn :modpath :string)
(defdyn :offline :boolean)
(defdyn :optimize :int)
(defdyn :pkglist :string)
(defdyn :silent :boolean)
(defdyn :statext nil)
(defdyn :syspath nil)
(defdyn :use-batch-shell :boolean)
(defdyn :verbose :boolean)
(defdyn :workers :int)
