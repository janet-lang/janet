###
### Various defaults that can be set at compile time
### and configure the behavior of the module.
###

(def config-dyns 
  "A table of all of the dynamic config bindings."
  @{})

(defmacro defdyn
  "Define a function that wraps (dyn :keyword). This will
  allow use of dynamic bindings with static runtime checks."
  [kw & meta]
  (put config-dyns kw true)
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
(defdyn :ar)
(defdyn :auto-shebang)
(defdyn :binpath)
(defdyn :c++)
(defdyn :c++-link)
(defdyn :cc)
(defdyn :cc-link)
(defdyn :cflags)
(defdyn :cppflags)
(defdyn :dynamic-cflags)
(defdyn :dynamic-lflags)
(defdyn :gitpath)
(defdyn :headerpath)
(defdyn :is-msvc)
(defdyn :janet)
(defdyn :janet-cflags)
(defdyn :janet-ldflags)
(defdyn :janet-lflags)
(defdyn :ldflags)
(defdyn :lflags)
(defdyn :libjanet)
(defdyn :libpath)
(defdyn :modext)
(defdyn :modpath)
(defdyn :offline)
(defdyn :optimize)
(defdyn :pkglist)
(defdyn :statext)
(defdyn :syspath)
(defdyn :use-batch-shell)
