#
# A simple OO implementation similar to that of the Self language.
# Objects are just tables in which the class is the prototype table.
# This means that classes themselves are also objects.
#
# Create a new class
#   (defclass Car)
# or
#   (def Car @{})
#
# Define a constructor
#   (defm Car:init [color]
#     (put self :color color))
#
# Define a method
#   (defm Car:honk []
#     (print "I am a " (get self :color) " car!"))
#
# Create an instance
#   (def mycar (new Car :red))
#
# Call a method
#   (mcall mycar:honk)
# or
#   ($ mycar:honk)
# If the method declaration is in scope, one can also
# invoke the method directly.
#   (Car:honk mycar)

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

#
# Public API
#

(def class 
  "(class obj)\n\nGets the class of an object."
  table.getproto)

(defn instance-of?
  "Checks if an object is an instance of a class."
  [class obj]
  (if obj (or 
            (= class obj) 
            (instance-of? class (table.getproto obj)))))

(defmacro mcall
  "Call a method."
  [signature & args]
  (def [method self] (parse-signature signature))
  (apply tuple (tuple get self method) self args))

(def $ :macro mcall)

(defmacro wrap-mcall
  "Wrap a method call in a function."
  [signature & args]
  (def [method self] (parse-signature signature))
  (def $m (gensym))
  (def $args (gensym))
  (tuple 'do
         (tuple 'def $m (tuple get self method))
         (tuple 'fn (symbol "wrapped-" signature) [tuple '& $args]
                (tuple apply $m self $args))))

(defn new
  "Create a new instance of a class by creating a new
  table whose prototype is class. If class has an init method,
  that will be called on the new object. Returns the new object."
  [class & args]
  (def obj (table.setproto @{} class))
  (def init (get class 'init))
  (when init (apply init obj args))
  obj)

(defmacro defm
  "Defines a method for a class."
  [signature & args]
  (def [method self] (parse-signature signature))
  (def newargs (add-self-to-body args))
  (tuple put self method (apply defn signature newargs)))

(defmacro defclass
  "Defines a new class."
  [name & args]
  (if (not name) (error "expected a name"))
  (tuple 'def name
         (apply tuple table :name (tuple 'quote name) args)))
