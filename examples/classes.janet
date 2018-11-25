# Classes need to:
# 1. Construct Objects
# 2. Keep metadata of objects
# 3. Support Method Lookup given a method signature
# 4. Add Methods
# 5. Keep around state

(defn- parse-signature
  "Turn a signature into a (method, object) pair."
  [signature]
  (when (not (symbol? signature)) (error "expected method signature"))
  (def parts (string.split ":" signature))
  (def self (symbol (get parts 0)))
  (def method (apply symbol (tuple.slice parts 1)))
  (tuple (tuple 'quote method) self))

(defn- add-self-to-body
  "Take a function definition and add the parameter 'self'
  to the declaration."
  [body]
  (def args-index (find-index tuple? body))
  (def bodya (apply array body))
  (put bodya args-index (tuple.prepend (get bodya args-index) 'self))
  bodya)

(defmacro call
  "Call a method."
  [signature & args]
  (def [method self] (parse-signature signature))
  (apply tuple (tuple get self method) self args))

(def :macro $ call)

(defn class
  "Create a new class."
  [& args]
  (def classobj (apply table args))

  # Set up super class
  (def super (get classobj :super))
  (when super
    (put classobj :super nil)
    (table.setproto classobj super))

  classobj)

(defn new
  "Create a new instance of a class."
  [class & args]
  (def obj (table.setproto @{} class))
  (def init (get class 'init))
  (when init (apply init obj args))
  obj)

(defmacro defmethod
  "Defines a method for a class."
  [signature & args]
  (def [method self] (parse-signature signature))
  (def newargs (add-self-to-body args))
  (tuple put self method (tuple.prepend newargs signature 'fn)))

(defmacro defclass
  "Defines a new class."
  [name & body]
  (tuple 'def name
         (tuple.prepend body class)))

