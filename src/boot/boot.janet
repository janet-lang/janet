# The core janet library
# Copyright 2022 © Calvin Rose

###
###
### Macros and Basic Functions
###
###

(def defn :macro
  ```
  (defn name & more)

  Define a function. Equivalent to `(def name (fn name [args] ...))`.
  ```
  (fn defn [name & more]
    (def len (length more))
    (def modifiers @[])
    (var docstr "")
    (def fstart
      (fn recur [i]
        (def {i ith} more)
        (def t (type ith))
        (if (= t :tuple)
          i
          (do
            (if (= t :string)
              (set docstr ith)
              (array/push modifiers ith))
            (if (< i len) (recur (+ i 1)))))))
    (def start (fstart 0))
    (def args (in more start))
    # Add function signature to docstring
    (var index 0)
    (def arglen (length args))
    (def buf (buffer "(" name))
    (while (< index arglen)
      (buffer/push-string buf " ")
      (buffer/format buf "%j" (in args index))
      (set index (+ index 1)))
    (array/push modifiers (string buf ")\n\n" docstr))
    # Build return value
    ~(def ,name ,;modifiers (fn ,name ,;(tuple/slice more start)))))

(defn defmacro :macro
  "Define a macro."
  [name & more]
  (setdyn name @{}) # override old macro definitions in the case of a recursive macro
  (apply defn name :macro more))

(defmacro as-macro
  ``Use a function or macro literal `f` as a macro. This lets
  any function be used as a macro. Inside a quasiquote, the
  idiom `(as-macro ,my-custom-macro arg1 arg2...)` can be used
  to avoid unwanted variable capture of `my-custom-macro`.``
  [f & args]
  (f ;args))

(defmacro defmacro-
  "Define a private macro that will not be exported."
  [name & more]
  (apply defn name :macro :private more))

(defmacro defn-
  "Define a private function that will not be exported."
  [name & more]
  (apply defn name :private more))

(defmacro def-
  "Define a private value that will not be exported."
  [name & more]
  ~(def ,name :private ,;more))

(defmacro var-
  "Define a private var that will not be exported."
  [name & more]
  ~(var ,name :private ,;more))

(defmacro toggle
  "Set a value to its boolean inverse. Same as `(set value (not value))`."
  [value]
  ~(set ,value (,not ,value)))

(defn defglobal
  "Dynamically create a global def."
  [name value]
  (def name* (symbol name))
  (setdyn name* @{:value value})
  nil)

(defn varglobal
  "Dynamically create a global var."
  [name init]
  (def name* (symbol name))
  (setdyn name* @{:ref @[init]})
  nil)

# Basic predicates
(defn nan? "Check if x is NaN." [x] (not= x x))
(defn number? "Check if x is a number." [x] (= (type x) :number))
(defn fiber? "Check if x is a fiber." [x] (= (type x) :fiber))
(defn string? "Check if x is a string." [x] (= (type x) :string))
(defn symbol? "Check if x is a symbol." [x] (= (type x) :symbol))
(defn keyword? "Check if x is a keyword." [x] (= (type x) :keyword))
(defn buffer? "Check if x is a buffer." [x] (= (type x) :buffer))
(defn function? "Check if x is a function (not a cfunction)." [x]
  (= (type x) :function))
(defn cfunction? "Check if x a cfunction." [x] (= (type x) :cfunction))
(defn table? "Check if x a table." [x] (= (type x) :table))
(defn struct? "Check if x a struct." [x] (= (type x) :struct))
(defn array? "Check if x is an array." [x] (= (type x) :array))
(defn tuple? "Check if x is a tuple." [x] (= (type x) :tuple))
(defn boolean? "Check if x is a boolean." [x] (= (type x) :boolean))
(defn bytes? "Check if x is a string, symbol, keyword, or buffer." [x]
  (def t (type x))
  (if (= t :string) true (if (= t :symbol) true (if (= t :keyword) true (= t :buffer)))))
(defn dictionary? "Check if x is a table or struct." [x]
  (def t (type x))
  (if (= t :table) true (= t :struct)))
(defn indexed? "Check if x is an array or tuple." [x]
  (def t (type x))
  (if (= t :array) true (= t :tuple)))
(defn truthy? "Check if x is truthy." [x] (if x true false))
(defn true? "Check if x is true." [x] (= x true))
(defn false? "Check if x is false." [x] (= x false))
(defn nil? "Check if x is nil." [x] (= x nil))
(defn empty? "Check if xs is empty." [xs] (= nil (next xs nil)))

# For macros, we define an imcomplete odd? function that will be overriden.
(defn odd? [x] (= 1 (mod x 2)))

(def idempotent?
  ```
  (idempotent? x)

  Check if x is a value that evaluates to itself when compiled.
  ```
  (do
    (def non-atomic-types
      {:array true
       :tuple true
       :table true
       :buffer true
       :struct true})
    (fn idempotent? [x] (not (in non-atomic-types (type x))))))

# C style macros and functions for imperative sugar. No bitwise though.
(defn inc "Returns x + 1." [x] (+ x 1))
(defn dec "Returns x - 1." [x] (- x 1))
(defmacro ++ "Increments the var x by 1." [x] ~(set ,x (,+ ,x ,1)))
(defmacro -- "Decrements the var x by 1." [x] ~(set ,x (,- ,x ,1)))
(defmacro += "Increments the var x by n." [x n] ~(set ,x (,+ ,x ,n)))
(defmacro -= "Decrements the var x by n." [x n] ~(set ,x (,- ,x ,n)))
(defmacro *= "Shorthand for (set x (\\* x n))." [x n] ~(set ,x (,* ,x ,n)))
(defmacro /= "Shorthand for (set x (/ x n))." [x n] ~(set ,x (,/ ,x ,n)))
(defmacro %= "Shorthand for (set x (% x n))." [x n] ~(set ,x (,% ,x ,n)))

(defmacro assert
  "Throw an error if x is not truthy. Will not evaluate `err` if x is truthy."
  [x &opt err]
  (def v (gensym))
  ~(do
     (def ,v ,x)
     (if ,v
       ,v
       (,error ,(if err err (string/format "assert failure in %j" x))))))

(defn errorf
  "A combination of `error` and `string/format`. Equivalent to `(error (string/format fmt ;args))`."
  [fmt & args]
  (error (string/format fmt ;args)))

(defmacro default
  ``Define a default value for an optional argument.
  Expands to `(def sym (if (= nil sym) val sym))`.``
  [sym val]
  ~(def ,sym (if (= nil ,sym) ,val ,sym)))

(defmacro comment
  "Ignores the body of the comment."
  [&])

(defmacro if-not
  "Shorthand for `(if (not condition) else then)`."
  [condition then &opt else]
  ~(if ,condition ,else ,then))

(defmacro when
  "Evaluates the body when the condition is true. Otherwise returns nil."
  [condition & body]
  ~(if ,condition (do ,;body)))

(defmacro unless
  "Shorthand for `(when (not condition) ;body)`. "
  [condition & body]
  ~(if ,condition nil (do ,;body)))

(defmacro cond
  `Evaluates conditions sequentially until the first true condition
  is found, and then executes the corresponding body. If there are an
  odd number of forms, and no forms are matched, the last expression
  is executed. If there are no matches, returns nil.`
  [& pairs]
  (defn aux [i]
    (def restlen (- (length pairs) i))
    (if (= restlen 0) nil
      (if (= restlen 1) (in pairs i)
        (tuple 'if (in pairs i)
               (in pairs (+ i 1))
               (aux (+ i 2))))))
  (aux 0))

(defmacro case
  ``Select the body that equals the dispatch value. When `pairs`
  has an odd number of elements, the last is the default expression.
  If no match is found, returns nil.``
  [dispatch & pairs]
  (def atm (idempotent? dispatch))
  (def sym (if atm dispatch (gensym)))
  (defn aux [i]
    (def restlen (- (length pairs) i))
    (if (= restlen 0) nil
      (if (= restlen 1) (in pairs i)
        (tuple 'if (tuple = sym (in pairs i))
               (in pairs (+ i 1))
               (aux (+ i 2))))))
  (if atm
    (aux 0)
    (tuple 'do
           (tuple 'def sym dispatch)
           (aux 0))))

(defmacro let
  ``Create a scope and bind values to symbols. Each pair in `bindings` is
  assigned as if with `def`, and the body of the `let` form returns the last
  value.``
  [bindings & body]
  (if (odd? (length bindings)) (error "expected even number of bindings to let"))
  (def len (length bindings))
  (var i 0)
  (var accum @['do])
  (while (< i len)
    (def {i k (+ i 1) v} bindings)
    (array/push accum (tuple 'def k v))
    (+= i 2))
  (array/concat accum body)
  (tuple/slice accum 0))

(defmacro try
  ``Try something and catch errors. `body` is any expression,
  and `catch` should be a form, the first element of which is a tuple. This tuple
  should contain a binding for errors and an optional binding for
  the fiber wrapping the body. Returns the result of `body` if no error,
  or the result of `catch` if an error.``
  [body catch]
  (let [[[err fib]] catch
        f (gensym)
        r (gensym)]
    ~(let [,f (,fiber/new (fn [] ,body) :ie)
           ,r (,resume ,f)]
       (if (,= (,fiber/status ,f) :error)
         (do (def ,err ,r) ,(if fib ~(def ,fib ,f)) ,;(tuple/slice catch 1))
         ,r))))

(defmacro protect
  `Evaluate expressions, while capturing any errors. Evaluates to a tuple
  of two elements. The first element is true if successful, false if an
  error, and the second is the return value or error.`
  [& body]
  (let [f (gensym) r (gensym)]
    ~(let [,f (,fiber/new (fn [] ,;body) :ie)
           ,r (,resume ,f)]
       [(,not= :error (,fiber/status ,f)) ,r])))

(defmacro and
  `Evaluates to the last argument if all preceding elements are truthy, otherwise
  evaluates to the first falsey argument.`
  [& forms]
  (var ret true)
  (def len (length forms))
  (var i len)
  (while (> i 0)
    (-- i)
    (def v (in forms i))
    (set ret (if (= ret true)
               v
               (if (idempotent? v)
                 ['if v ret v]
                 (do (def s (gensym))
                   ['if ['def s v] ret s])))))
  ret)

(defmacro or
  `Evaluates to the last argument if all preceding elements are falsey, otherwise
  evaluates to the first truthy element.`
  [& forms]
  (def len (length forms))
  (var i (- len 1))
  (var ret (get forms i))
  (while (> i 0)
    (-- i)
    (def fi (in forms i))
    (set ret (if (idempotent? fi)
               (tuple 'if fi fi ret)
               (do
                 (def $fi (gensym))
                 (tuple 'do (tuple 'def $fi fi)
                        (tuple 'if $fi $fi ret))))))
  ret)

(defmacro with-syms
  "Evaluates `body` with each symbol in `syms` bound to a generated, unique symbol."
  [syms & body]
  (var i 0)
  (def len (length syms))
  (def accum @[])
  (while (< i len)
    (array/push accum (in syms i) [gensym])
    (++ i))
  ~(let (,;accum) ,;body))

(defmacro defer
  ``Run `form` unconditionally after `body`, even if the body throws an error.
  Will also run `form` if a user signal 0-4 is received.``
  [form & body]
  (with-syms [f r]
    ~(do
       (def ,f (,fiber/new (fn [] ,;body) :ti))
       (def ,r (,resume ,f))
       ,form
       (if (= (,fiber/status ,f) :dead)
         ,r
         (,propagate ,r ,f)))))

(defmacro edefer
  ``Run `form` after `body` in the case that body terminates abnormally (an error or user signal 0-4).
  Otherwise, return last form in `body`.``
  [form & body]
  (with-syms [f r]
    ~(do
       (def ,f (,fiber/new (fn [] ,;body) :ti))
       (def ,r (,resume ,f))
       (if (= (,fiber/status ,f) :dead)
         ,r
         (do ,form (,propagate ,r ,f))))))

(defmacro prompt
  ``Set up a checkpoint that can be returned to. `tag` should be a value
  that is used in a `return` statement, like a keyword.``
  [tag & body]
  (with-syms [res target payload fib]
    ~(do
       (def ,fib (,fiber/new (fn [] [,tag (do ,;body)]) :i0))
       (def ,res (,resume ,fib))
       (def [,target ,payload] ,res)
       (if (,= ,tag ,target)
         ,payload
         (,propagate ,res ,fib)))))

(defmacro chr
  `Convert a string of length 1 to its byte (ascii) value at compile time.`
  [c]
  (unless (and (string? c) (= (length c) 1))
    (error (string/format "expected string of length 1, got %v" c)))
  (c 0))

(defmacro label
  ``Set a label point that is lexically scoped. `name` should be a symbol
  that will be bound to the label.``
  [name & body]
  ~(do
     (def ,name @"")
     ,(apply prompt name body)))

(defn return
  "Return to a prompt point."
  [to &opt value]
  (signal 0 [to value]))

(defmacro with
  ``Evaluate `body` with some resource, which will be automatically cleaned up
  if there is an error in `body`. `binding` is bound to the expression `ctor`, and
  `dtor` is a function or callable that is passed the binding. If no destructor
  (`dtor`) is given, will call :close on the resource.``
  [[binding ctor dtor] & body]
  ~(do
     (def ,binding ,ctor)
     ,(apply defer [(or dtor :close) binding] body)))

(defmacro when-with
  ``Similar to with, but if binding is false or nil, returns
  nil without evaluating the body. Otherwise, the same as `with`.``
  [[binding ctor dtor] & body]
  ~(if-let [,binding ,ctor]
     ,(apply defer [(or dtor :close) binding] body)))

(defmacro if-with
  ``Similar to `with`, but if binding is false or nil, evaluates
  the falsey path. Otherwise, evaluates the truthy path. In both cases,
  `ctor` is bound to binding.``
  [[binding ctor dtor] truthy &opt falsey]
  ~(if-let [,binding ,ctor]
     ,(apply defer [(or dtor :close) binding] [truthy])
     ,falsey))

(defn- for-var-template
  [i start stop step comparison delta body]
  (with-syms [s]
    (def st (if (idempotent? step) step (gensym)))
    (def loop-body
      ~(while (,comparison ,i ,s)
         ,;body
         (set ,i (,delta ,i ,st))))
    ~(do
       (var ,i ,start)
       (def ,s ,stop)
       ,;(if (= st step) [] [~(def ,st ,step)])
       ,(if (and (number? st) (> st 0))
          loop-body
          ~(if (,> ,st 0) ,loop-body)))))

(defn- for-template
  [binding start stop step comparison delta body]
  (def i (gensym))
  (for-var-template i start stop step comparison delta
                    [~(def ,binding ,i) ;body]))

(defn- check-indexed [x]
  (if (indexed? x)
    x
    (error (string "expected tuple for range, got " x))))

(defn- range-template
  [binding object rest op comparison]
  (let [[start stop step] (check-indexed object)]
    (for-template binding start stop (or step 1) comparison op [rest])))

(defn- each-template
  [binding inx kind body]
  (with-syms [k]
    (def ds (if (idempotent? inx) inx (gensym)))
    ~(do
       ,(unless (= ds inx) ~(def ,ds ,inx))
       (var ,k (,next ,ds nil))
       (while (,not= nil ,k)
         (def ,binding
           ,(case kind
              :each ~(,in ,ds ,k)
              :keys k
              :pairs ~(,tuple ,k (,in ,ds ,k))))
         ,;body
         (set ,k (,next ,ds ,k))))))

(defn- iterate-template
  [binding expr body]
  (with-syms [i]
    ~(do
       (var ,i nil)
       (while (set ,i ,expr)
         (def ,binding ,i)
         ,body))))

(defn- loop1
  [body head i]

  # Terminate recursion
  (when (<= (length head) i)
    (break ~(do ,;body)))

  (def {i binding
        (+ i 1) verb} head)

  # 2 term expression
  (when (keyword? binding)
    (break
      (let [rest (loop1 body head (+ i 2))]
        (case binding
          :until ~(do (if ,verb (break) nil) ,rest)
          :while ~(do (if ,verb nil (break)) ,rest)
          :let ~(let ,verb (do ,rest))
          :after ~(do ,rest ,verb nil)
          :before ~(do ,verb ,rest nil)
          :repeat (with-syms [iter]
                    ~(do (var ,iter ,verb) (while (> ,iter 0) ,rest (-- ,iter))))
          :when ~(when ,verb ,rest)
          (error (string "unexpected loop modifier " binding))))))

  # 3 term expression
  (def {(+ i 2) object} head)
  (let [rest (loop1 body head (+ i 3))]
    (case verb
      :range (range-template binding object rest + <)
      :range-to (range-template binding object rest + <=)
      :down (range-template binding object rest - >)
      :down-to (range-template binding object rest - >=)
      :keys (each-template binding object :keys [rest])
      :pairs (each-template binding object :pairs [rest])
      :in (each-template binding object :each [rest])
      :iterate (iterate-template binding object rest)
      (error (string "unexpected loop verb " verb)))))

(defmacro forv
  ``Do a C-style for-loop for side effects. The iteration variable `i`
  can be mutated in the loop, unlike normal `for`. Returns nil.``
  [i start stop & body]
  (for-var-template i start stop 1 < + body))

(defmacro for
  "Do a C-style for-loop for side effects. Returns nil."
  [i start stop & body]
  (for-template i start stop 1 < + body))

(defmacro eachk
  "Loop over each key in `ds`. Returns nil."
  [x ds & body]
  (each-template x ds :keys body))

(defmacro eachp
  "Loop over each (key, value) pair in `ds`. Returns nil."
  [x ds & body]
  (each-template x ds :pairs body))

(defmacro repeat
  "Evaluate body n times. If n is negative, body will be evaluated 0 times. Evaluates to nil."
  [n & body]
  (with-syms [iter]
    ~(do (var ,iter ,n) (while (> ,iter 0) ,;body (-- ,iter)))))

(defmacro forever
  "Evaluate body forever in a loop, or until a break statement."
  [& body]
  ~(while true ,;body))

(defmacro each
  "Loop over each value in `ds`. Returns nil."
  [x ds & body]
  (each-template x ds :each body))

(defmacro loop
  ```
  A general purpose loop macro. This macro is similar to the Common Lisp loop
  macro, although intentionally much smaller in scope.  The head of the loop
  should be a tuple that contains a sequence of either bindings or
  conditionals. A binding is a sequence of three values that define something
  to loop over. Bindings are written in the format:

      binding :verb object/expression

  where `binding` is a binding as passed to def, `:verb` is one of a set of
  keywords, and `object` is any expression. Each subsequent binding creates a
  nested loop within the loop created by the previous binding.

  The available verbs are:

  * `:iterate` -- repeatedly evaluate and bind to the expression while it is
    truthy.

  * `:range` -- loop over a range. The object should be a two-element tuple with
    a start and end value, and an optional positive step. The range is half
    open, [start, end).

  * `:range-to` -- same as :range, but the range is inclusive [start, end].

  * `:down` -- loop over a range, stepping downwards. The object should be a
    two-element tuple with a start and (exclusive) end value, and an optional
    (positive!) step size.

  * `:down-to` -- same as :down, but the range is inclusive [start, end].

  * `:keys` -- iterate over the keys in a data structure.

  * `:pairs` -- iterate over the key-value pairs as tuples in a data structure.

  * `:in` -- iterate over the values in a data structure or fiber.

  `loop` also accepts conditionals to refine the looping further. Conditionals are of
  the form:

      :modifier argument

  where `:modifier` is one of a set of keywords, and `argument` is keyword-dependent.
  `:modifier` can be one of:

  * `:while expression` -- breaks from the current loop if `expression` is
    falsey.

  * `:until expression` -- breaks from the current loop if `expression` is
    truthy.

  * `:let bindings` -- defines bindings inside the current loop as passed to the
    `let` macro.

  * `:before form` -- evaluates a form for a side effect before the next inner
    loop.

  * `:after form` -- same as `:before`, but the side effect happens after the
    next inner loop.

  * `:repeat n` -- repeats the next inner loop `n` times.

  * `:when condition` -- only evaluates the current loop body when `condition`
    is true.

  The `loop` macro always evaluates to nil.
  ```
  [head & body]
  (loop1 body head 0))

(defmacro seq
  ``Similar to `loop`, but accumulates the loop body into an array and returns that.
  See `loop` for details.``
  [head & body]
  (def $accum (gensym))
  ~(do (def ,$accum @[]) (loop ,head (,array/push ,$accum (do ,;body))) ,$accum))

(defmacro tabseq
  ``Similar to `loop`, but accumulates key value pairs into a table.
  See `loop` for details.``
  [head key-body & value-body]
  (def $accum (gensym))
  ~(do (def ,$accum @{}) (loop ,head (,put ,$accum ,key-body (do ,;value-body))) ,$accum))

(defmacro generate
  ``Create a generator expression using the `loop` syntax. Returns a fiber
  that yields all values inside the loop in order. See `loop` for details.``
  [head & body]
  ~(,fiber/new (fn [] (loop ,head (yield (do ,;body)))) :yi))

(defmacro coro
  "A wrapper for making fibers that may yield multiple values (coroutine). Same as `(fiber/new (fn [] ;body) :yi)`."
  [& body]
  (tuple fiber/new (tuple 'fn '[] ;body) :yi))

(defmacro fiber-fn
  "A wrapper for making fibers. Same as `(fiber/new (fn [] ;body) flags)`."
  [flags & body]
  (tuple fiber/new (tuple 'fn '[] ;body) flags))

(defn sum
  "Returns the sum of xs. If xs is empty, returns 0."
  [xs]
  (var accum 0)
  (each x xs (+= accum x))
  accum)

(defn mean
  "Returns the mean of xs. If empty, returns NaN."
  [xs]
  (/ (sum xs) (length xs)))

(defn product
  "Returns the product of xs. If xs is empty, returns 1."
  [xs]
  (var accum 1)
  (each x xs (*= accum x))
  accum)

(defmacro if-let
  ``Make multiple bindings, and if all are truthy,
  evaluate the `tru` form. If any are false or nil, evaluate
  the `fal` form. Bindings have the same syntax as the `let` macro.``
  [bindings tru &opt fal]
  (def len (length bindings))
  (if (= 0 len) (error "expected at least 1 binding"))
  (if (odd? len) (error "expected an even number of bindings"))
  (defn aux [i]
    (if (>= i len)
      tru
      (do
        (def bl (in bindings i))
        (def br (in bindings (+ 1 i)))
        (def atm (idempotent? bl))
        (def sym (if atm bl (gensym)))
        (if atm
          # Simple binding
          (tuple 'do
                 (tuple 'def sym br)
                 (tuple 'if sym (aux (+ 2 i)) fal))
          # Destructured binding
          (tuple 'do
                 (tuple 'def sym br)
                 (tuple 'if sym
                        (tuple 'do
                               (tuple 'def bl sym)
                               (aux (+ 2 i)))
                        fal))))))
  (aux 0))

(defmacro when-let
  "Same as `(if-let bindings (do ;body))`."
  [bindings & body]
  ~(if-let ,bindings (do ,;body)))

(defn comp
  `Takes multiple functions and returns a function that is the composition
  of those functions.`
  [& functions]
  (case (length functions)
    0 nil
    1 (in functions 0)
    2 (let [[f g] functions] (fn [& x] (f (g ;x))))
    3 (let [[f g h] functions] (fn [& x] (f (g (h ;x)))))
    4 (let [[f g h i] functions] (fn [& x] (f (g (h (i ;x))))))
    (let [[f g h i] functions]
      (comp (fn [x] (f (g (h (i x)))))
            ;(tuple/slice functions 4 -1)))))

(defn identity
  "A function that returns its argument."
  [x]
  x)

(defn complement
  "Returns a function that is the complement to the argument."
  [f]
  (fn [x] (not (f x))))

(defn extreme
  ``Returns the most extreme value in `args` based on the function `order`.
  `order` should take two values and return true or false (a comparison).
  Returns nil if `args` is empty.``
  [order args]
  (var [ret] args)
  (each x args (if (order x ret) (set ret x)))
  ret)

(defn max
  "Returns the numeric maximum of the arguments."
  [& args] (extreme > args))

(defn min
  "Returns the numeric minimum of the arguments."
  [& args] (extreme < args))

(defn max-of
  "Returns the numeric maximum of the argument sequence."
  [args] (extreme > args))

(defn min-of
  "Returns the numeric minimum of the argument sequence."
  [args] (extreme < args))

(defn first
  "Get the first element from an indexed data structure."
  [xs]
  (get xs 0))

(defn last
  "Get the last element from an indexed data structure."
  [xs]
  (get xs (- (length xs) 1)))

## Polymorphic comparisons

(defn compare
  ``Polymorphic compare. Returns -1, 0, 1 for x < y, x = y, x > y respectively.
  Differs from the primitive comparators in that it first checks to
  see whether either x or y implement a `compare` method which can
  compare x and y. If so, it uses that method. If not, it
  delegates to the primitive comparators.``
  [x y]
  (or
    (when-let [f (get x :compare)] (f x y))
    (when-let [f (get y :compare)] (- (f y x)))
    (cmp x y)))

(defn- compare-reduce [op xs]
  (var r true)
  (loop [i :range [0 (- (length xs) 1)]
         :let [c (compare (xs i) (xs (+ i 1)))
               ok (op c 0)]
         :when (not ok)]
    (set r false)
    (break))
  r)

(defn compare=
  ``Equivalent of `=` but using polymorphic `compare` instead of primitive comparator.``
  [& xs]
  (compare-reduce = xs))

(defn compare<
  ``Equivalent of `<` but using polymorphic `compare` instead of primitive comparator.``
  [& xs]
  (compare-reduce < xs))

(defn compare<=
  ``Equivalent of `<=` but using polymorphic `compare` instead of primitive comparator.``
  [& xs]
  (compare-reduce <= xs))

(defn compare>
  ``Equivalent of `>` but using polymorphic `compare` instead of primitive comparator.``
  [& xs]
  (compare-reduce > xs))

(defn compare>=
  ``Equivalent of `>=` but using polymorphic `compare` instead of primitive comparator.``
  [& xs]
  (compare-reduce >= xs))

(defn zero? "Check if x is zero." [x] (= (compare x 0) 0))
(defn pos? "Check if x is greater than 0." [x] (= (compare x 0) 1))
(defn neg? "Check if x is less than 0." [x] (= (compare x 0) -1))
(defn one? "Check if x is equal to 1." [x] (= (compare x 1) 0))
(defn even? "Check if x is even." [x] (= 0 (compare 0 (mod x 2))))
(defn odd? "Check if x is odd." [x] (= 0 (compare 1 (mod x 2))))

###
###
### Indexed Combinators
###
###

(defn- median-of-three [a b c]
  (if (not= (> a b) (> a c))
    a
    (if (not= (> b a) (> b c)) b c)))

(defn- sort-help [a lo hi before?]
  (when (< lo hi)
    (def pivot
      (median-of-three (in a hi) (in a lo)
                       (in a (math/floor (/ (+ lo hi) 2)))))
    (var left lo)
    (var right hi)
    (while true
      (while (before? (in a left) pivot) (++ left))
      (while (before? pivot (in a right)) (-- right))
      (when (<= left right)
        (def tmp (in a left))
        (set (a left) (in a right))
        (set (a right) tmp)
        (++ left)
        (-- right))
      (if (>= left right) (break)))
    (sort-help a lo right before?)
    (sort-help a left hi before?))
  a)

(defn sort
  ``Sort `ind` in-place, and return it. Uses quick-sort and is not a stable sort.
  If a `before?` comparator function is provided, sorts elements using that,
  otherwise uses `<`.``
  [ind &opt before?]
  (sort-help ind 0 (- (length ind) 1) (or before? <)))

(defn sort-by
  ``Returns `ind` sorted by calling
  a function `f` on each element and comparing the result with `<`.``
  [f ind]
  (sort ind (fn [x y] (< (f x) (f y)))))

(defn sorted
  ``Returns a new sorted array without modifying the old one.
  If a `before?` comparator function is provided, sorts elements using that,
  otherwise uses `<`.``
  [ind &opt before?]
  (sort (array/slice ind) before?))

(defn sorted-by
  ``Returns a new sorted array that compares elements by invoking
  a function `f` on each element and comparing the result with `<`.``
  [f ind]
  (sorted ind (fn [x y] (< (f x) (f y)))))

(defn reduce
  ``Reduce, also know as fold-left in many languages, transforms
  an indexed type (array, tuple) with a function to produce a value by applying `f` to
  each element in order. `f` is a function of 2 arguments, `(f accum el)`, where
  `accum` is the initial value and `el` is the next value in the indexed type `ind`.
  `f` returns a value that will be used as `accum` in the next call to `f`. `reduce`
  returns the value of the final call to `f`.``
  [f init ind]
  (var accum init)
  (each el ind (set accum (f accum el)))
  accum)

(defn reduce2
  ``The 2-argument version of `reduce` that does not take an initialization value.
  Instead, the first element of the array is used for initialization.``
  [f ind]
  (var k (next ind))
  (if (= nil k) (break nil))
  (var res (in ind k))
  (set k (next ind k))
  (while (not= nil k)
    (set res (f res (in ind k)))
    (set k (next ind k)))
  res)

(defn accumulate
  ``Similar to `reduce`, but accumulates intermediate values into an array.
  The last element in the array is what would be the return value from `reduce`.
  The `init` value is not added to the array (the return value will have the same
  number of elements as `ind`).
  Returns a new array.``
  [f init ind]
  (var res init)
  (def ret @[])
  (each x ind (array/push ret (set res (f res x))))
  ret)

(defn accumulate2
  ``The 2-argument version of `accumulate` that does not take an initialization value.
  The first value in `ind` will be added to the array as is, so the length of the
  return value will be `(length ind)`.``
  [f ind]
  (var k (next ind))
  (def ret @[])
  (if (= nil k) (break ret))
  (var res (in ind k))
  (array/push ret res)
  (set k (next ind k))
  (while (not= nil k)
    (set res (f res (in ind k)))
    (array/push ret res)
    (set k (next ind k)))
  ret)

(defn map
  `Map a function over every value in a data structure and
  return an array of the results.`
  [f & inds]
  (def ninds (length inds))
  (if (= 0 ninds) (error "expected at least 1 indexed collection"))
  (def res @[])
  (def [i1 i2 i3 i4] inds)
  (case ninds
    1 (each x i1 (array/push res (f x)))
    2 (do
        (var k1 nil)
        (var k2 nil)
        (while true
          (if (= nil (set k1 (next i1 k1))) (break))
          (if (= nil (set k2 (next i2 k2))) (break))
          (array/push res (f (in i1 k1) (in i2 k2)))))
    3 (do
        (var k1 nil)
        (var k2 nil)
        (var k3 nil)
        (while true
          (if (= nil (set k1 (next i1 k1))) (break))
          (if (= nil (set k2 (next i2 k2))) (break))
          (if (= nil (set k3 (next i3 k3))) (break))
          (array/push res (f (in i1 k1) (in i2 k2) (in i3 k3)))))
    4 (do
        (var k1 nil)
        (var k2 nil)
        (var k3 nil)
        (var k4 nil)
        (while true
          (if (= nil (set k1 (next i1 k1))) (break))
          (if (= nil (set k2 (next i2 k2))) (break))
          (if (= nil (set k3 (next i3 k3))) (break))
          (if (= nil (set k4 (next i4 k4))) (break))
          (array/push res (f (in i1 k1) (in i2 k2) (in i3 k3) (in i4 k4)))))
    (do
      (def iterkeys (array/new-filled ninds))
      (var done false)
      (def call-buffer @[])
      (while true
        (forv i 0 ninds
          (let [old-key (in iterkeys i)
                ii (in inds i)
                new-key (next ii old-key)]
            (if (= nil new-key)
              (do (set done true) (break))
              (do (set (iterkeys i) new-key) (array/push call-buffer (in ii new-key))))))
        (if done (break))
        (array/push res (f ;call-buffer))
        (array/clear call-buffer))))
  res)

(defn mapcat
  ``Map a function over every element in an array or tuple and
  use `array/concat` to concatenate the results.``
  [f ind]
  (def res @[])
  (each x ind
    (array/concat res (f x)))
  res)

(defn filter
  ``Given a predicate, take only elements from an array or tuple for
  which `(pred element)` is truthy. Returns a new array.``
  [pred ind]
  (def res @[])
  (each item ind
    (if (pred item)
      (array/push res item)))
  res)

(defn count
  ``Count the number of items in `ind` for which `(pred item)`
  is true.``
  [pred ind]
  (var counter 0)
  (each item ind
    (if (pred item)
      (++ counter)))
  counter)

(defn keep
  ``Given a predicate `pred`, return a new array containing the truthy results
  of applying `pred` to each element in the indexed collection `ind`. This is
  different from `filter` which returns an array of the original elements where
  the predicate is truthy.``
  [pred ind]
  (def res @[])
  (each item ind
    (if-let [y (pred item)]
      (array/push res y)))
  res)

(defn range
  `Create an array of values [start, end) with a given step.
  With one argument, returns a range [0, end). With two arguments, returns
  a range [start, end). With three, returns a range with optional step size.`
  [& args]
  (case (length args)
    1 (do
        (def [n] args)
        (def arr (array/new n))
        (forv i 0 n (put arr i i))
        arr)
    2 (do
        (def [n m] args)
        (def arr (array/new (- m n)))
        (forv i n m (put arr (- i n) i))
        arr)
    3 (do
        (def [n m s] args)
        (cond
          (zero? s) @[]
          (neg? s) (seq [i :down [n m (- s)]] i)
          (seq [i :range [n m s]] i)))
    (error "expected 1 to 3 arguments to range")))

(defn find-index
  ``Find the index of indexed type for which `pred` is true. Returns `dflt` if not found.``
  [pred ind &opt dflt]
  (var k nil)
  (var ret dflt)
  (while true
    (set k (next ind k))
    (if (= k nil) (break))
    (def item (in ind k))
    (when (pred item)
      (set ret k)
      (break)))
  ret)

(defn find
  ``Find the first value in an indexed collection that satisfies a predicate. Returns
  `dflt` if not found.``
  [pred ind &opt dflt]
  (var k nil)
  (var ret dflt)
  (while true
    (set k (next ind k))
    (if (= k nil) (break))
    (def item (in ind k))
    (when (pred item)
      (set ret item)
      (break)))
  ret)

(defn index-of
  ``Find the first key associated with a value x in a data structure, acting like a reverse lookup.
  Will not look at table prototypes.
  Returns `dflt` if not found.``
  [x ind &opt dflt]
  (var k (next ind nil))
  (var ret dflt)
  (while (not= nil k)
    (when (= (in ind k) x) (set ret k) (break))
    (set k (next ind k)))
  ret)

(defn- take-n-fallback
  [n xs]
  (def res @[])
  (when (> n 0)
    (var left n)
    (each x xs
      (array/push res x)
      (-- left)
      (if (= 0 left) (break))))
  res)

(defn- take-until-fallback
  [pred xs]
  (def res @[])
  (each x xs
    (if (pred x) (break))
    (array/push res x))
  res)

(defn- slice-n
  [f n ind]
  (def len (length ind))
  # make sure end is in [0, len]
  (def m (if (> n 0) n 0))
  (def end (if (> m len) len m))
  (f ind 0 end))

(defn take
  "Take the first n elements of a fiber, indexed or bytes type. Returns a new array, tuple or string, respectively."
  [n ind]
  (cond
    (bytes? ind) (slice-n string/slice n ind)
    (indexed? ind) (slice-n tuple/slice n ind)
    (take-n-fallback n ind)))

(defn- slice-until
  [f pred ind]
  (def len (length ind))
  (def i (find-index pred ind))
  (def end (if (nil? i) len i))
  (f ind 0 end))

(defn take-until
  "Same as `(take-while (complement pred) ind)`."
  [pred ind]
  (cond
    (bytes? ind) (slice-until string/slice pred ind)
    (indexed? ind) (slice-until tuple/slice pred ind)
    (take-until-fallback pred ind)))

(defn take-while
  `Given a predicate, take only elements from a fiber, indexed, or bytes type that satisfy
  the predicate, and abort on first failure. Returns a new array, tuple, or string, respectively.`
  [pred ind]
  (take-until (complement pred) ind))

(defn drop
  ``Drop the first n elements in an indexed or bytes type. Returns a new tuple or string
  instance, respectively.``
  [n ind]
  (def use-str (bytes? ind))
  (def f (if use-str string/slice tuple/slice))
  (def len (length ind))
  # make sure start is in [0, len]
  (def m (if (> n 0) n 0))
  (def start (if (> m len) len m))
  (f ind start -1))

(defn drop-until
  "Same as `(drop-while (complement pred) ind)`."
  [pred ind]
  (def use-str (bytes? ind))
  (def f (if use-str string/slice tuple/slice))
  (def i (find-index pred ind))
  (def len (length ind))
  (def start (if (nil? i) len i))
  (f ind start))

(defn drop-while
  `Given a predicate, remove elements from an indexed or bytes type that satisfy
  the predicate, and abort on first failure. Returns a new tuple or string, respectively.`
  [pred ind]
  (drop-until (complement pred) ind))

(defn juxt*
  ``Returns the juxtaposition of functions. In other words,
  `((juxt* a b c) x)` evaluates to `[(a x) (b x) (c x)]`.``
  [& funs]
  (fn [& args]
    (def ret @[])
    (each f funs
      (array/push ret (f ;args)))
    (tuple/slice ret 0)))

(defmacro juxt
  "Macro form of `juxt*`. Same behavior but more efficient."
  [& funs]
  (def parts @['tuple])
  (def $args (gensym))
  (each f funs
    (array/push parts (tuple apply f $args)))
  (tuple 'fn (tuple '& $args) (tuple/slice parts 0)))

(defmacro defdyn
  ``Define an alias for a keyword that is used as a dynamic binding. The
  alias is a normal, lexically scoped binding that can be used instead of
  a keyword to prevent typos. `defdyn` does not set dynamic bindings or otherwise
  replace `dyn` and `setdyn`. The alias _must_ start and end with the `*` character, usually
  called "earmuffs".``
  [alias & more]
  (assert (symbol? alias) "alias must be a symbol")
  (assert (and (> (length alias) 2) (= 42 (first alias) (last alias))) "name must have leading and trailing '*' characters")
  (def prefix (dyn :defdyn-prefix))
  (def kw (keyword prefix (slice alias 1 -2)))
  ~(def ,alias :dyn ,;more ,kw))

(defdyn *defdyn-prefix* ``Optional namespace prefix to add to keywords declared with `defdyn`.
  Use this to prevent keyword collisions between dynamic bindings.``)
(defdyn *out* "Where normal print functions print output to.")
(defdyn *err* "Where error printing prints output to.")
(defdyn *redef* "When set, allow dynamically rebinding top level defs. Will slow generated code and is intended to be used for development.")
(defdyn *debug* "Enables a built in debugger on errors and other useful features for debugging in a repl.")
(defdyn *exit* "When set, will cause the current context to complete. Can be set to exit from repl (or file), for example.")
(defdyn *exit-value* "Set the return value from `run-context` upon an exit. By default, `run-context` will return nil.")

(defdyn *macro-form*
  "Inside a macro, is bound to the source form that invoked the macro")

(defdyn *lint-error*
  "The current lint error level. The error level is the lint level at which compilation will exit with an error and not continue.")

(defdyn *lint-warn*
  "The current lint warning level. The warning level is the lint level at which and error will be printed but compilation will continue as normal.")

(defdyn *lint-levels*
  "A table of keyword alias to numbers denoting a lint level. Can be used to provided custom aliases for numeric lint levels.")

(defdyn *current-file*
  "Bound to the name of the currently compiling file.")

(defmacro tracev
  `Print to stderr a value and a description of the form that produced that value.
  Evaluates to x.`
  [x]
  (def [l c] (tuple/sourcemap (dyn *macro-form* ())))
  (def cf (dyn *current-file*))
  (def fmt-1 (if cf (string/format "trace [%s]" cf) "trace"))
  (def fmt-2 (if (or (neg? l) (neg? c)) ":" (string/format " on line %d, column %d:" l c)))
  (def fmt (string fmt-1 fmt-2 " %j is "))
  (def s (gensym))
  ~(upscope
     (def ,s ,x)
     (,eprinf ,fmt ',x)
     (,eprintf (,dyn :pretty-format "%q") ,s)
     ,s))

(defmacro ->
  ``Threading macro. Inserts x as the second value in the first form
  in `forms`, and inserts the modified first form into the second form
  in the same manner, and so on. Useful for expressing pipelines of data.``
  [x & forms]
  (defn fop [last n]
    (def [h t] (if (= :tuple (type n))
                 (tuple (in n 0) (array/slice n 1))
                 (tuple n @[])))
    (def parts (array/concat @[h last] t))
    (tuple/slice parts 0))
  (reduce fop x forms))

(defmacro ->>
  ``Threading macro. Inserts x as the last value in the first form
  in `forms`, and inserts the modified first form into the second form
  in the same manner, and so on. Useful for expressing pipelines of data.``
  [x & forms]
  (defn fop [last n]
    (def [h t] (if (= :tuple (type n))
                 (tuple (in n 0) (array/slice n 1))
                 (tuple n @[])))
    (def parts (array/concat @[h] t @[last]))
    (tuple/slice parts 0))
  (reduce fop x forms))

(defmacro -?>
  ``Short circuit threading macro. Inserts x as the second value in the first form
  in `forms`, and inserts the modified first form into the second form
  in the same manner, and so on. The pipeline will return nil
  if an intermediate value is nil.
  Useful for expressing pipelines of data.``
  [x & forms]
  (defn fop [last n]
    (def [h t] (if (= :tuple (type n))
                 (tuple (in n 0) (array/slice n 1))
                 (tuple n @[])))
    (def sym (gensym))
    (def parts (array/concat @[h sym] t))
    ~(let [,sym ,last] (if ,sym ,(tuple/slice parts 0))))
  (reduce fop x forms))

(defmacro -?>>
  ``Short circuit threading macro. Inserts x as the last value in the first form
  in `forms`, and inserts the modified first form into the second form
  in the same manner, and so on. The pipeline will return nil
  if an intermediate value is nil.
  Useful for expressing pipelines of data.``
  [x & forms]
  (defn fop [last n]
    (def [h t] (if (= :tuple (type n))
                 (tuple (in n 0) (array/slice n 1))
                 (tuple n @[])))
    (def sym (gensym))
    (def parts (array/concat @[h] t @[sym]))
    ~(let [,sym ,last] (if ,sym ,(tuple/slice parts 0))))
  (reduce fop x forms))

(defn- walk-ind [f form]
  (def ret @[])
  (each x form (array/push ret (f x)))
  ret)

(defn- walk-dict [f form]
  (def ret @{})
  (loop [k :keys form]
    (put ret (f k) (f (in form k))))
  ret)

(defn walk
  ``Iterate over the values in ast and apply `f`
  to them. Collect the results in a data structure. If ast is not a
  table, struct, array, or tuple,
  returns form.``
  [f form]
  (case (type form)
    :table (walk-dict f form)
    :struct (table/to-struct (walk-dict f form))
    :array (walk-ind f form)
    :tuple (let [x (walk-ind f form)]
             (if (= :parens (tuple/type form))
               (tuple/slice x)
               (tuple/brackets ;x)))
    form))

(defn postwalk
  ``Do a post-order traversal of a data structure and call `(f x)`
  on every visitation.``
  [f form]
  (f (walk (fn [x] (postwalk f x)) form)))

(defn prewalk
  "Similar to `postwalk`, but do pre-order traversal."
  [f form]
  (walk (fn [x] (prewalk f x)) (f form)))

(defmacro as->
  ``Thread forms together, replacing `as` in `forms` with the value
  of the previous form. The first form is the value x. Returns the
  last value.``
  [x as & forms]
  (var prev x)
  (each form forms
    (def sym (gensym))
    (def next-prev (postwalk (fn [y] (if (= y as) sym y)) form))
    (set prev ~(let [,sym ,prev] ,next-prev)))
  prev)

(defmacro as?->
  ``Thread forms together, replacing `as` in `forms` with the value
  of the previous form. The first form is the value x. If any
  intermediate values are falsey, return nil; otherwise, returns the
  last value.``
  [x as & forms]
  (var prev x)
  (each form forms
    (def sym (gensym))
    (def next-prev (postwalk (fn [y] (if (= y as) sym y)) form))
    (set prev ~(if-let [,sym ,prev] ,next-prev)))
  prev)

(defmacro with-dyns
  `Run a block of code in a new fiber that has some
  dynamic bindings set. The fiber will not mask errors
  or signals, but the dynamic bindings will be properly
  unset, as dynamic bindings are fiber-local.`
  [bindings & body]
  (def dyn-forms
    (seq [i :range [0 (length bindings) 2]]
      ~(setdyn ,(bindings i) ,(bindings (+ i 1)))))
  ~(,resume (,fiber/new (fn [] ,;dyn-forms ,;body) :p)))

(defmacro with-vars
  ``Evaluates `body` with each var in `vars` temporarily bound. Similar signature to
  `let`, but each binding must be a var.``
  [vars & body]
  (def len (length vars))
  (unless (even? len) (error "expected even number of argument to vars"))
  (def temp (seq [i :range [0 len 2]] (gensym)))
  (def saveold (seq [i :range [0 len 2]] ['def (temp (/ i 2)) (vars i)]))
  (def setnew (seq [i :range [0 len 2]] ['set (vars i) (vars (+ i 1))]))
  (def restoreold (seq [i :range [0 len 2]] ['set (vars i) (temp (/ i 2))]))
  (with-syms [ret f s]
    ~(do
       ,;saveold
       (def ,f (,fiber/new (fn [] ,;setnew ,;body) :ti))
       (def ,ret (,resume ,f))
       ,;restoreold
       (if (= (,fiber/status ,f) :dead) ,ret (,propagate ,ret ,f)))))

(defn partial
  "Partial function application."
  [f & more]
  (if (zero? (length more)) f
    (fn [& r] (f ;more ;r))))

(defn every?
  ``Returns true if each value in `ind` is truthy, otherwise returns the first
  falsey value.``
  [ind]
  (var res true)
  (loop [x :in ind :while res]
    (if x nil (set res x)))
  res)

(defn any?
  ``Returns the first truthy value in `ind`, otherwise nil.``
  [ind]
  (var res nil)
  (loop [x :in ind :until res]
    (if x (set res x)))
  res)

(defn reverse!
  `Reverses the order of the elements in a given array or buffer and returns it
  mutated.`
  [t]
  (def len-1 (- (length t) 1))
  (def half (/ len-1 2))
  (forv i 0 half
    (def j (- len-1 i))
    (def l (in t i))
    (def r (in t j))
    (put t i r)
    (put t j l))
  t)

(defn reverse
  `Reverses the order of the elements in a given array or tuple and returns
  a new array. If a string or buffer is provided, returns an array of its
  byte values, reversed.`
  [t]
  (def len (length t))
  (var n (- len 1))
  (def ret (array/new len))
  (while (>= n 0)
    (array/push ret (in t n))
    (-- n))
  ret)

(defn invert
  ``Given an associative data structure `ds`, returns a new table where the
  keys of `ds` are the values, and the values are the keys. If multiple keys
  in `ds` are mapped to the same value, only one of those values will
  become a key in the returned table.``
  [ds]
  (def ret @{})
  (loop [k :keys ds]
    (put ret (in ds k) k))
  ret)

(defn zipcoll
  `Creates a table from two arrays/tuples.
  Returns a new table.`
  [ks vs]
  (def res @{})
  (var kk nil)
  (var vk nil)
  (while true
    (set kk (next ks kk))
    (if (= nil kk) (break))
    (set vk (next vs vk))
    (if (= nil vk) (break))
    (put res (in ks kk) (in vs vk)))
  res)

(defn get-in
  ``Access a value in a nested data structure. Looks into the data structure via
  a sequence of keys. If value is not found, and `dflt` is provided, returns `dflt`.``
  [ds ks &opt dflt]
  (var d ds)
  (loop [k :in ks :while (not (nil? d))] (set d (get d k)))
  (if (= nil d) dflt d))

(defn update-in
  ``Update a value in a nested data structure `ds`. Looks into `ds` via a sequence of keys,
  and replaces the value found there with `f` applied to that value.
  Missing data structures will be replaced with tables. Returns
  the modified, original data structure.``
  [ds ks f & args]
  (var d ds)
  (def len-1 (- (length ks) 1))
  (if (< len-1 0) (error "expected at least 1 key in ks"))
  (forv i 0 len-1
    (def k (get ks i))
    (def v (get d k))
    (if (= nil v)
      (let [newv (table)]
        (put d k newv)
        (set d newv))
      (set d v)))
  (def last-key (get ks len-1))
  (def last-val (get d last-key))
  (put d last-key (f last-val ;args))
  ds)

(defn put-in
  ``Put a value into a nested data structure `ds`. Looks into `ds` via
  a sequence of keys. Missing data structures will be replaced with tables. Returns
  the modified, original data structure.``
  [ds ks v]
  (var d ds)
  (def len-1 (- (length ks) 1))
  (if (< len-1 0) (error "expected at least 1 key in ks"))
  (forv i 0 len-1
    (def k (get ks i))
    (def v (get d k))
    (if (= nil v)
      (let [newv (table)]
        (put d k newv)
        (set d newv))
      (set d v)))
  (def last-key (get ks len-1))
  (def last-val (get d last-key))
  (put d last-key v)
  ds)

(defn update
  ``For a given key in data structure `ds`, replace its corresponding value with the
  result of calling `func` on that value. If `args` are provided, they will be passed
  along to `func` as well. Returns `ds`, updated.``
  [ds key func & args]
  (def old (get ds key))
  (put ds key (func old ;args)))

(defn merge-into
  ``Merges multiple tables/structs into table `tab`. If a key appears in more than one
  collection in `colls`, then later values replace any previous ones. Returns `tab`.``
  [tab & colls]
  (loop [c :in colls
         key :keys c]
    (put tab key (in c key)))
  tab)

(defn merge
  ``Merges multiple tables/structs into one new table. If a key appears in more than one
  collection in `colls`, then later values replace any previous ones.
  Returns the new table.``
  [& colls]
  (def container @{})
  (loop [c :in colls
         key :keys c]
    (put container key (in c key)))
  container)

(defn keys
  "Get the keys of an associative data structure."
  [x]
  (def arr @[])
  (var k (next x nil))
  (while (not= nil k)
    (array/push arr k)
    (set k (next x k)))
  arr)

(defn values
  "Get the values of an associative data structure."
  [x]
  (def arr @[])
  (var k (next x nil))
  (while (not= nil k)
    (array/push arr (in x k))
    (set k (next x k)))
  arr)

(defn pairs
  "Get the key-value pairs of an associative data structure."
  [x]
  (def arr @[])
  (var k (next x nil))
  (while (not= nil k)
    (array/push arr (tuple k (in x k)))
    (set k (next x k)))
  arr)

(defn frequencies
  "Get the number of occurrences of each value in an indexed data structure."
  [ind]
  (def freqs @{})
  (each x ind
    (def n (in freqs x))
    (set (freqs x) (if n (+ 1 n) 1)))
  freqs)

(defn group-by
  ``Group elements of `ind` by a function `f` and put the results into a new table. The keys of
  the table are the distinct return values from calling `f` on the elements of `ind`. The values
  of the table are arrays of all elements of `ind` for which `f` called on the element equals
  that corresponding key.``
  [f ind]
  (def ret @{})
  (each x ind
    (def y (f x))
    (if-let [arr (get ret y)]
      (array/push arr x)
      (put ret y @[x])))
  ret)

(defn partition-by
  ``Partition elements of a sequential data structure by a representative function `f`. Partitions
  split when `(f x)` changes values when iterating to the next element `x` of `ind`. Returns a new array
  of arrays.``
  [f ind]
  (def ret @[])
  (var span nil)
  (var category nil)
  (var is-new true)
  (each x ind
    (def y (f x))
    (cond
      is-new (do (set is-new false) (set category y) (set span @[x]) (array/push ret span))
      (= y category) (array/push span x)
      (do (set category y) (set span @[x]) (array/push ret span))))
  ret)

(defn interleave
  "Returns an array of the first elements of each col, then the second elements, etc."
  [& cols]
  (def res @[])
  (def ncol (length cols))
  (when (> ncol 0)
    (def len (min ;(map length cols)))
    (loop [i :range [0 len]
           ci :range [0 ncol]]
      (array/push res (in (in cols ci) i))))
  res)

(defn distinct
  "Returns an array of the deduplicated values in `xs`."
  [xs]
  (def ret @[])
  (def seen @{})
  (each x xs (if (in seen x) nil (do (put seen x true) (array/push ret x))))
  ret)

(defn flatten-into
  ``Takes a nested array (tree) `xs` and appends the depth first traversal of
  `xs` to array `into`. Returns `into`.``
  [into xs]
  (each x xs
    (if (indexed? x)
      (flatten-into into x)
      (array/push into x)))
  into)

(defn flatten
  ``Takes a nested array (tree) `xs` and returns the depth first traversal of
  it. Returns a new array.``
  [xs]
  (flatten-into @[] xs))

(defn kvs
  ``Takes a table or struct and returns and array of key value pairs
  like `@[k v k v ...]`. Returns a new array.``
  [dict]
  (def ret @[])
  (loop [k :keys dict] (array/push ret k (in dict k)))
  ret)

(defn from-pairs
  ``Takes a sequence of pairs and creates a table from each pair. It is the inverse of
  `pairs` on a table. Returns a new table.``
  [ps]
  (def ret @{})
  (each [k v] ps
    (put ret k v))
  ret)

(defn interpose
  ``Returns a sequence of the elements of `ind` separated by
  `sep`. Returns a new array.``
  [sep ind]
  (def len (length ind))
  (def ret (array/new (- (* 2 len) 1)))
  (if (> len 0) (put ret 0 (in ind 0)))
  (var i 1)
  (while (< i len)
    (array/push ret sep (in ind i))
    (++ i))
  ret)

(defn partition
  ``Partition an indexed data structure `ind` into tuples
  of size `n`. Returns a new array.``
  [n ind]
  (var i 0) (var nextn n)
  (def len (length ind))
  (def ret (array/new (math/ceil (/ len n))))
  (def slicer (if (bytes? ind) string/slice tuple/slice))
  (while (<= nextn len)
    (array/push ret (slicer ind i nextn))
    (set i nextn)
    (+= nextn n))
  (if (not= i len) (array/push ret (slicer ind i)))
  ret)

###
###
### IO Helpers
###
###

(defn slurp
  ``Read all data from a file with name `path` and then close the file.``
  [path]
  (def f (file/open path :rb))
  (if-not f (error (string "could not open file " path)))
  (def contents (file/read f :all))
  (file/close f)
  contents)

(defn spit
  ``Write `contents` to a file at `path`. Can optionally append to the file.``
  [path contents &opt mode]
  (default mode :wb)
  (def f (file/open path mode))
  (if-not f (error (string "could not open file " path " with mode " mode)))
  (file/write f contents)
  (file/close f)
  nil)

(defdyn *pretty-format*
  "Format specifier for the `pp` function")

(defn pp
  ``Pretty-print to stdout or `(dyn *out*)`. The format string used is `(dyn *pretty-format* "%q")`.``
  [x]
  (printf (dyn *pretty-format* "%q") x)
  (flush))

###
###
### Pattern Matching
###
###

(defmacro match
  ```
  Pattern matching. Match an expression `x` against any number of cases.
  Each case is a pattern to match against, followed by an expression to
  evaluate to if that case is matched.  Legal patterns are:

  * symbol -- a pattern that is a symbol will match anything, binding `x`'s
    value to that symbol.

  * array or bracket tuple -- an array or bracket tuple will match only if
    all of its elements match the corresponding elements in `x`.
    Use `& rest` at the end of an array or bracketed tuple to bind all remaining values to `rest`.

  * table or struct -- a table or struct will match if all values match with
    the corresponding values in `x`.

  * tuple -- a tuple pattern will match if its first element matches, and the
    following elements are treated as predicates and are true.

  * `\_` symbol -- the last special case is the `\_` symbol, which is a wildcard
    that will match any value without creating a binding.

  While a symbol pattern will ordinarily match any value, the pattern `(@ <sym>)`,
  where <sym> is any symbol, will attempt to match `x` against a value
  already bound to `<sym>`, rather than matching and rebinding it.

  Any other value pattern will only match if it is equal to `x`.
  Quoting a pattern with `'` will also treat the value as a literal value to match against.

  ```
  [x & cases]

  # Partition body into sections.
  (def oddlen (odd? (length cases)))
  (def else (if oddlen (last cases)))
  (def patterns (partition 2 (if oddlen (slice cases 0 -2) cases)))

  # Keep an array for accumulating the compilation output
  (def x-sym (if (idempotent? x) x (gensym)))
  (def accum @[])
  (if (not= x x-sym) (array/push accum ['def x-sym x]))

  # Table of gensyms
  (def symbols @{[nil nil] x-sym})
  (def length-symbols @{})

  (defn emit [x] (array/push accum x))
  (defn emit-branch [condition result] (array/push accum :branch condition result))

  (defn get-sym
    [parent-sym key]
    (def symbol-key [parent-sym key])
    (or (get symbols symbol-key)
        (let [s (gensym)]
          (put symbols symbol-key s)
          (emit ['def s [get parent-sym key]])
          s)))

  (defn get-length-sym
    [parent-sym]
    (or (get length-symbols parent-sym)
        (let [s (gensym)]
          (put length-symbols parent-sym s)
          (emit ['def s ['if [indexed? parent-sym] [length parent-sym]]])
          s)))

  (defn visit-pattern-1
    [b2g parent-sym key pattern]
    (if (= pattern '_) (break))
    (def s (get-sym parent-sym key))
    (def t (type pattern))
    (def isarr (or (= t :array) (and (= t :tuple) (= (tuple/type pattern) :brackets))))
    (cond

      # match local binding
      (= t :symbol)
      (if-let [x (in b2g pattern)]
        (array/push x s)
        (put b2g pattern @[s]))

      # match quoted literal
      (and (= t :tuple) (= 2 (length pattern)) (= 'quote (pattern 0)))
      (break)

      # match data structure template
      (or (= t :struct) (= t :table))
      (eachp [i sub-pattern] pattern
        (visit-pattern-1 b2g s i sub-pattern))

      isarr
      (do
        (get-length-sym s)
        (eachp [i sub-pattern] pattern
          (when (= sub-pattern '&)
            (when (<= (length pattern) (inc i))
              (errorf "expected symbol following & in pattern"))

            (when (< (+ i 2) (length pattern))
              (errorf "expected a single symbol follow '& in pattern, found %q" (slice pattern (inc i))))

            (when (not= (type (pattern (inc i))) :symbol)
              (errorf "expected symbol following & in pattern, found %q" (pattern (inc i))))

            (put b2g (pattern (inc i)) @[[slice s i]])
            (break))
          (visit-pattern-1 b2g s i sub-pattern)))

      # match global unification
      (and (= t :tuple) (= 2 (length pattern)) (= '@ (pattern 0)))
      (break)

      # match predicated binding
      (and (= t :tuple) (>= (length pattern) 2))
      (do
        (visit-pattern-1 b2g parent-sym key (pattern 0)))))

  (defn visit-pattern-2
    [anda gun preds parent-sym key pattern]
    (if (= pattern '_) (break))
    (def s (get-sym parent-sym key))
    (def t (type pattern))
    (def isarr (or (= t :array) (and (= t :tuple) (= (tuple/type pattern) :brackets))))
    (when isarr
      (array/push anda (get-length-sym s))
      (def pattern-len
        (if-let [rest-idx (find-index (fn [x] (= x '&)) pattern)]
          rest-idx
          (length pattern)))
      (array/push anda [<= pattern-len (get-length-sym s)]))
    (cond

      # match data structure template
      (or (= t :struct) (= t :table))
      (eachp [i sub-pattern] pattern
        (array/push anda [not= nil (get-sym s i)])
        (visit-pattern-2 anda gun preds s i sub-pattern))

      isarr
      (eachp [i sub-pattern] pattern
        # stop recursing to sub-patterns if the rest sigil is found
        (when (= sub-pattern '&)
          (break))
        (visit-pattern-2 anda gun preds s i sub-pattern))

      # match local binding
      (= t :symbol) (break)

      # match quoted literal
      (and (= t :tuple) (= 2 (length pattern)) (= 'quote (pattern 0)))
      (array/push anda ['= s pattern])

      # match global unification
      (and (= t :tuple) (= 2 (length pattern)) (= '@ (pattern 0)))
      (if-let [x (in gun (pattern 1))]
        (array/push x s)
        (put gun (pattern 1) @[s]))

      # match predicated binding
      (and (= t :tuple) (>= (length pattern) 2))
      (do
        (array/push preds ;(slice pattern 1))
        (visit-pattern-2 anda gun preds parent-sym key (pattern 0)))

      # match literal
      (array/push anda ['= s pattern])))

  # Compile the patterns
  (each [pattern expression] patterns
    (def b2g @{})
    (def gun @{})
    (def preds @[])
    (visit-pattern-1 b2g nil nil pattern)
    (def anda @['and])
    (visit-pattern-2 anda gun preds nil nil pattern)
    # Local unification
    (def unify @[])
    (each syms b2g
      (when (< 1 (length syms))
        (array/push unify [= ;syms])))
    # Global unification
    (eachp [binding syms] gun
      (array/push unify [= binding ;syms]))
    (sort unify)
    (array/concat anda unify)
    # Final binding
    (def defs (seq [[k v] :in (sort (pairs b2g))] ['def k (first v)]))
    # Predicates
    (unless (empty? preds)
      (def pred-join ~(do ,;defs (and ,;preds)))
      (array/push anda pred-join))
    (emit-branch (tuple/slice anda) ['do ;defs expression]))

  # Expand branches
  (def stack @[else])
  (each el (reverse accum)
    (if (= :branch el)
      (let [condition (array/pop stack)
            truthy (array/pop stack)
            if-form ~(if ,condition ,truthy
                       ,(case (length stack)
                          0 nil
                          1 (stack 0)
                          ~(do ,;(reverse stack))))]
        (array/remove stack 0 (length stack))
        (array/push stack if-form))
      (array/push stack el)))

  ~(do ,;(reverse stack)))

###
###
### Macro Expansion
###
###

(defdyn *macro-lints*
  ``Bound to an array of lint messages that will be reported by the compiler inside a macro.
  To indicate an error or warning, a macro author should use `maclintf`.``)

(defn maclintf
  ``When inside a macro, call this function to add a linter warning. Takes
  a `fmt` argument like `string/format`, which is used to format the message.``
  [level fmt & args]
  (def lints (dyn *macro-lints*))
  (when lints
    (def form (dyn *macro-form*))
    (def [l c] (if (tuple? form) (tuple/sourcemap form) [nil nil]))
    (def l (if-not (= -1 l) l))
    (def c (if-not (= -1 c) c))
    (def msg (string/format fmt ;args))
    (array/push lints [level l c msg]))
  nil)

(defn macex1
  ``Expand macros in a form, but do not recursively expand macros.
  See `macex` docs for info on `on-binding`.``
  [x &opt on-binding]

  (when on-binding
    (when (symbol? x)
      (break (on-binding x))))

  (defn recur [y] (macex1 y on-binding))

  (defn dotable [t on-value]
    (def newt @{})
    (var key (next t nil))
    (while (not= nil key)
      (put newt (recur key) (on-value (in t key)))
      (set key (next t key)))
    newt)

  (defn expand-bindings [x]
    (case (type x)
      :array (map expand-bindings x)
      :tuple (tuple/slice (map expand-bindings x))
      :table (dotable x expand-bindings)
      :struct (table/to-struct (dotable x expand-bindings))
      (recur x)))

  (defn expanddef [t]
    (def last (in t (- (length t) 1)))
    (def bound (in t 1))
    (tuple/slice
      (array/concat
        @[(in t 0) (expand-bindings bound)]
        (tuple/slice t 2 -2)
        @[(recur last)])))

  (defn expandall [t]
    (def args (map recur (tuple/slice t 1)))
    (tuple (in t 0) ;args))

  (defn expandfn [t]
    (def t1 (in t 1))
    (if (symbol? t1)
      (do
        (def args (map recur (tuple/slice t 3)))
        (tuple 'fn t1 (in t 2) ;args))
      (do
        (def args (map recur (tuple/slice t 2)))
        (tuple 'fn t1 ;args))))

  (defn expandqq [t]
    (defn qq [x]
      (case (type x)
        :tuple (if (= :brackets (tuple/type x))
                 ~[,;(map qq x)]
                 (do
                   (def x0 (get x 0))
                   (if (= 'unquote x0)
                     (tuple x0 (recur (get x 1)))
                     (tuple/slice (map qq x)))))
        :array (map qq x)
        :table (table ;(map qq (kvs x)))
        :struct (struct ;(map qq (kvs x)))
        x))
    (tuple (in t 0) (qq (in t 1))))

  (def specs
    {'set expanddef
     'def expanddef
     'do expandall
     'fn expandfn
     'if expandall
     'quote identity
     'quasiquote expandqq
     'var expanddef
     'while expandall
     'break expandall
     'upscope expandall})

  (defn dotup [t]
    (def h (in t 0))
    (def s (in specs h))
    (def entry (or (dyn h) {}))
    (def m (do (def r (get entry :ref)) (if r (in r 0) (get entry :value))))
    (def m? (in entry :macro))
    (cond
      s (s t)
      m? (do (setdyn *macro-form* t) (m ;(tuple/slice t 1)))
      (tuple/slice (map recur t))))

  (def ret
    (case (type x)
      :tuple (if (= (tuple/type x) :brackets)
               (tuple/brackets ;(map recur x))
               (dotup x))
      :array (map recur x)
      :struct (table/to-struct (dotable x recur))
      :table (dotable x recur)
      x))
  ret)

(defn all
  ``Returns true if all `xs` are truthy, otherwise the result of first
  falsey predicate value, `(pred x)`.``
  [pred xs]
  (var ret true)
  (loop [x :in xs :while ret] (set ret (pred x)))
  ret)

(defn some
  ``Returns nil if all `xs` are false or nil, otherwise returns the result of the
  first truthy predicate, `(pred x)`.``
  [pred xs]
  (var ret nil)
  (loop [x :in xs :while (not ret)] (if-let [y (pred x)] (set ret y)))
  ret)

(defn deep-not=
  ``Like `not=`, but mutable types (arrays, tables, buffers) are considered
  equal if they have identical structure. Much slower than `not=`.``
  [x y]
  (def tx (type x))
  (or
    (not= tx (type y))
    (case tx
      :tuple (or (not= (length x) (length y)) (some identity (map deep-not= x y)))
      :array (or (not= (length x) (length y)) (some identity (map deep-not= x y)))
      :struct (deep-not= (kvs x) (kvs y))
      :table (deep-not= (table/to-struct x) (table/to-struct y))
      :buffer (not= (string x) (string y))
      (not= x y))))

(defn deep=
  ``Like `=`, but mutable types (arrays, tables, buffers) are considered
  equal if they have identical structure. Much slower than `=`.``
  [x y]
  (not (deep-not= x y)))

(defn freeze
  `Freeze an object (make it immutable) and do a deep copy, making
  child values also immutable. Closures, fibers, and abstract types
  will not be recursively frozen, but all other types will.`
  [x]
  (case (type x)
    :array (tuple/slice (map freeze x))
    :tuple (tuple/slice (map freeze x))
    :table (if-let [p (table/getproto x)]
             (freeze (merge (table/clone p) x))
             (struct ;(map freeze (kvs x))))
    :struct (struct ;(map freeze (kvs x)))
    :buffer (string x)
    x))

(defn macex
  ``Expand macros completely.
  `on-binding` is an optional callback for whenever a normal symbolic binding
  is encountered. This allows macros to easily see all bindings used by their
  arguments by calling `macex` on their contents. The binding itself is also
  replaced by the value returned by `on-binding` within the expanded macro.``
  [x &opt on-binding]
  (var previous x)
  (var current (macex1 x on-binding))
  (var counter 0)
  (while (deep-not= current previous)
    (if (> (++ counter) 200)
      (error "macro expansion too nested"))
    (set previous current)
    (set current (macex1 current on-binding)))
  current)

(defmacro varfn
  ``Create a function that can be rebound. `varfn` has the same signature
  as `defn`, but defines functions in the environment as vars. If a var `name`
  already exists in the environment, it is rebound to the new function. Returns
  a function.``
  [name & body]
  (def expansion (apply defn name body))
  (def fbody (last expansion))
  (def modifiers (tuple/slice expansion 2 -2))
  (def metadata @{})
  (each m modifiers
    (cond
      (keyword? m) (put metadata m true)
      (string? m) (put metadata :doc m)
      (error (string "invalid metadata " m))))
  (with-syms [entry old-entry f]
    ~(let [,old-entry (,dyn ',name)]
       (def ,entry (or ,old-entry @{:ref @[nil]}))
       (,setdyn ',name ,entry)
       (def ,f ,fbody)
       (,put-in ,entry [:ref 0] ,f)
       (,merge-into ,entry ',metadata)
       ,f)))

###
###
### Function shorthand
###
###

(defmacro short-fn
  ```
  Shorthand for `fn`. Arguments are given as `$n`, where `n` is the 0-indexed
  argument of the function. `$` is also an alias for the first (index 0) argument.
  The `$&` symbol will make the anonymous function variadic if it appears in the
  body of the function, and can be combined with positional arguments.

  Example usage:

      (short-fn (+ $ $)) # A function that doubles its arguments.
      (short-fn (string $0 $1)) # accepting multiple args.
      |(+ $ $) # use pipe reader macro for terse function literals.
      |(+ $&)  # variadic functions
  ```
  [arg &opt name]
  (var max-param-seen -1)
  (var vararg false)
  (defn saw-special-arg
    [num]
    (set max-param-seen (max max-param-seen num)))
  (defn on-binding
    [x]
    (if (string/has-prefix? '$ x)
      (cond
        (= '$ x)
        (do
          (saw-special-arg 0)
          '$0)
        (= '$& x)
        (do
          (set vararg true)
          x)
        :else
        (do
          (def num (scan-number (string/slice x 1)))
          (if (nat? num)
            (saw-special-arg num))
          x))
      x))
  (def expanded (macex arg on-binding))
  (def name-splice (if name [name] []))
  (def fn-args (seq [i :range [0 (+ 1 max-param-seen)]] (symbol '$ i)))
  ~(fn ,;name-splice [,;fn-args ,;(if vararg ['& '$&] [])] ,expanded))

###
###
### Default PEG patterns
###
###

(defdyn *peg-grammar*
  ``The implicit base grammar used when compiling PEGs. Any undefined keywords
  found when compiling a peg will use lookup in this table (if defined).``)

(def default-peg-grammar
  `The default grammar used for pegs. This grammar defines several common patterns
  that should make it easier to write more complex patterns.`
  ~@{:d (range "09")
     :a (range "az" "AZ")
     :s (set " \t\r\n\0\f\v")
     :w (range "az" "AZ" "09")
     :h (range "09" "af" "AF")
     :S (if-not :s 1)
     :W (if-not :w 1)
     :A (if-not :a 1)
     :D (if-not :d 1)
     :H (if-not :h 1)
     :d+ (some :d)
     :a+ (some :a)
     :s+ (some :s)
     :w+ (some :w)
     :h+ (some :h)
     :d* (any :d)
     :a* (any :a)
     :w* (any :w)
     :s* (any :s)
     :h* (any :h)})

(setdyn *peg-grammar* default-peg-grammar)

###
###
### Evaluation and Compilation
###
###

(defdyn *syspath*
  "Path of directory to load system modules from.")

# Initialize syspath
(each [k v] (partition 2 (tuple/slice boot/args 2))
  (case k
    "JANET_PATH" (setdyn *syspath* v)))

(defn make-env
  `Create a new environment table. The new environment
  will inherit bindings from the parent environment, but new
  bindings will not pollute the parent environment.`
  [&opt parent]
  (def parent (if parent parent root-env))
  (def newenv (table/setproto @{} parent))
  newenv)

(defdyn *err-color*
  "Whether or not to turn on error coloring in stacktraces and other error messages.")

(defn bad-parse
  "Default handler for a parse error."
  [p where]
  (def ec (dyn *err-color*))
  (def [line col] (:where p))
  (eprint
    (if ec "\e[31m" "")
    where
    ":"
    line
    ":"
    col
    ": parse error: "
    (:error p)
    (if ec "\e[0m" ""))
  (eflush))

(defn- print-line-col
  ``Print the source code at a line, column in a source file. If unable to open
  the file, prints nothing.``
  [where line col]
  (if-not line (break))
  (unless (string? where) (break))
  (when-with [f (file/open where :r)]
    (def source-code (file/read f :all))
    (var index 0)
    (repeat (dec line)
      (if-not index (break))
      (set index (string/find "\n" source-code index))
      (if index (++ index)))
    (when index
      (def line-end (string/find "\n" source-code index))
      (eprint "  " (string/slice source-code index line-end))
      (when col
        (+= index col)
        (eprint (string/repeat " " (inc col)) "^")))))

(defn warn-compile
  "Default handler for a compile warning."
  [msg level where &opt line col]
  (def ec (dyn *err-color*))
  (eprin
    (if ec "\e[33m" "")
    where
    ":"
    line
    ":"
    col
    ": compile warning (" level "): ")
  (eprint msg)
  (when ec
    (print-line-col where line col)
    (eprin "\e[0m"))
  (eflush))

(defn bad-compile
  "Default handler for a compile error."
  [msg macrof where &opt line col]
  (def ec (dyn *err-color*))
  (eprin
    (if ec "\e[31m" "")
    where
    ":"
    line
    ":"
    col
    ": compile error: ")
  (if macrof
    (debug/stacktrace macrof msg "")
    (eprint msg))
  (when ec
    (print-line-col where line col)
    (eprin "\e[0m"))
  (eflush))

(defn curenv
  ``Get the current environment table. Same as `(fiber/getenv (fiber/current))`. If `n`
  is provided, gets the nth prototype of the environment table.``
  [&opt n]
  (var e (fiber/getenv (fiber/current)))
  (if n (repeat n (if (= nil e) (break)) (set e (table/getproto e))))
  e)

(def- lint-levels
  {:none 0
   :relaxed 1
   :normal 2
   :strict 3
   :all math/inf})

(defn run-context
  ```
  Run a context. This evaluates expressions in an environment,
  and encapsulates the parsing, compilation, and evaluation.
  Returns `(in environment :exit-value environment)` when complete.
  `opts` is a table or struct of options. The options are as follows:

    * `:chunks` -- callback to read into a buffer - default is getline

    * `:on-parse-error` -- callback when parsing fails - default is bad-parse

    * `:env` -- the environment to compile against - default is the current env

    * `:source` -- source path for better errors (use keywords for non-paths) - default
      is :<anonymous>

    * `:on-compile-error` -- callback when compilation fails - default is bad-compile

    * `:on-compile-warning` -- callback for any linting error - default is warn-compile

    * `:evaluator` -- callback that executes thunks. Signature is (evaluator thunk source
      env where)

    * `:on-status` -- callback when a value is evaluated - default is debug/stacktrace.

    * `:fiber-flags` -- what flags to wrap the compilation fiber with. Default is :ia.

    * `:expander` -- an optional function that is called on each top level form before
      being compiled.

    * `:parser` -- provide a custom parser that implements the same interface as Janet's
      built-in parser.

    * `:read` -- optional function to get the next form, called like `(read env source)`.
      Overrides all parsing.
  ```
  [opts]

  (def {:env env
        :chunks chunks
        :on-status onstatus
        :on-compile-error on-compile-error
        :on-compile-warning on-compile-warning
        :on-parse-error on-parse-error
        :fiber-flags guard
        :evaluator evaluator
        :source default-where
        :parser parser
        :read read
        :expander expand} opts)
  (default env (or (fiber/getenv (fiber/current)) @{}))
  (default chunks (fn [buf p] (getline "" buf env)))
  (default onstatus debug/stacktrace)
  (default on-compile-error bad-compile)
  (default on-compile-warning warn-compile)
  (default on-parse-error bad-parse)
  (default evaluator (fn evaluate [x &] (x)))
  (default default-where :<anonymous>)
  (default guard :ydt)

  (var where default-where)

  (if (string? where)
    (put env *current-file* where))

  # Evaluate 1 source form in a protected manner
  (def lints @[])
  (defn eval1 [source &opt l c]
    (def source (if expand (expand source) source))
    (var good true)
    (var resumeval nil)
    (def f
      (fiber/new
        (fn []
          (array/clear lints)
          (def res (compile source env where lints))
          (unless (empty? lints)
            # Convert lint levels to numbers.
            (def levels (get env *lint-levels* lint-levels))
            (def lint-error (get env *lint-error*))
            (def lint-warning (get env *lint-warn*))
            (def lint-error (or (get levels lint-error lint-error) 0))
            (def lint-warning (or (get levels lint-warning lint-warning) 2))
            (each [level line col msg] lints
              (def lvl (get lint-levels level 0))
              (cond
                (<= lvl lint-error) (do
                                      (set good false)
                                      (on-compile-error msg nil where (or line l) (or col c)))
                (<= lvl lint-warning) (on-compile-warning msg level where (or line l) (or col c)))))
          (when good
            (if (= (type res) :function)
              (evaluator res source env where)
              (do
                (set good false)
                (def {:error err :line line :column column :fiber errf} res)
                (on-compile-error err errf where (or line l) (or column c))))))
        guard))
    (fiber/setenv f env)
    (while (fiber/can-resume? f)
      (def res (resume f resumeval))
      (when good (set resumeval (onstatus f res)))))

  # Reader version
  (when read
    (forever
      (if (in env :exit) (break))
      (eval1 (read env where)))
    (break (in env :exit-value env)))

  # The parser object
  (def p (or parser (parser/new)))
  (def p-consume (p :consume))
  (def p-produce (p :produce))
  (def p-status (p :status))
  (def p-has-more (p :has-more))

  (defn parse-err
    "Handle parser error in the correct environment"
    [p where]
    (def f (coro (on-parse-error p where)))
    (fiber/setenv f env)
    (resume f))

  (defn produce []
    (def tup (p-produce p true))
    [(in tup 0) ;(tuple/sourcemap tup)])

  # Loop
  (def buf @"")
  (var parser-not-done true)
  (while parser-not-done
    (if (env :exit) (break))
    (buffer/clear buf)
    (match (chunks buf p)
      :cancel
      (do
        # A :cancel chunk represents a cancelled form in the REPL, so reset.
        (:flush p)
        (buffer/clear buf))

      [:source new-where]
      (do
        (set where new-where)
        (if (string? new-where)
          (put env *current-file* new-where)))

      (do
        (var pindex 0)
        (var pstatus nil)
        (def len (length buf))
        (when (= len 0)
          (:eof p)
          (set parser-not-done false))
        (while (> len pindex)
          (+= pindex (p-consume p buf pindex))
          (while (p-has-more p)
            (eval1 ;(produce))
            (if (env :exit) (break)))
          (when (= (p-status p) :error)
            (parse-err p where)
            (if (env :exit) (break)))))))

  # Check final parser state
  (unless (env :exit)
    (while (p-has-more p)
      (eval1 ;(produce))
      (if (env :exit) (break)))
    (when (= (p-status p) :error)
      (parse-err p where)))

  (put env :exit nil)
  (in env :exit-value env))

(defn quit
  ``Tries to exit from the current repl or context. Does not always exit the application.
  Works by setting the :exit dynamic binding to true. Passing a non-nil `value` here will cause the outer
  run-context to return that value.``
  [&opt value]
  (setdyn :exit true)
  (setdyn :exit-value value)
  nil)

(defn eval-string
  ``Evaluates a string in the current environment. If more control over the
  environment is needed, use `run-context`.``
  [str]
  (var state (string str))
  (defn chunks [buf _]
    (def ret state)
    (set state nil)
    (when ret
      (buffer/push-string buf str)
      (buffer/push-string buf "\n")))
  (var returnval nil)
  (run-context {:chunks chunks
                :on-compile-error (fn compile-error [msg errf &]
                                    (error (string "compile error: " msg)))
                :on-parse-error (fn parse-error [p x]
                                  (error (string "parse error: " (:error p))))
                :fiber-flags :i
                :on-status (fn on-status [f val]
                             (if-not (= (fiber/status f) :dead)
                               (error val))
                             (set returnval val))
                :source :eval-string})
  returnval)

(defn eval
  ``Evaluates a form in the current environment. If more control over the
  environment is needed, use `run-context`.``
  [form]
  (def res (compile form (fiber/getenv (fiber/current)) :eval))
  (if (= (type res) :function)
    (res)
    (error (get res :error))))

(defn parse
  `Parse a string and return the first value. For complex parsing, such as for a repl with error handling,
  use the parser api.`
  [str]
  (let [p (parser/new)]
    (parser/consume p str)
    (parser/eof p)
    (if (parser/has-more p)
      (parser/produce p)
      (if (= :error (parser/status p))
        (error (parser/error p))
        (error "no value")))))

(defn parse-all
  `Parse a string and return all parsed values. For complex parsing, such as for a repl with error handling,
  use the parser api.`
  [str]
  (let [p (parser/new)
        ret @[]]
    (parser/consume p str)
    (parser/eof p)
    (while (parser/has-more p)
      (array/push ret (parser/produce p)))
    (if (= :error (parser/status p))
      (error (parser/error p))
      ret)))

(def load-image-dict
  ``A table used in combination with `unmarshal` to unmarshal byte sequences created
  by `make-image`, such that `(load-image bytes)` is the same as `(unmarshal bytes load-image-dict)`.``
  @{})

(def make-image-dict
  ``A table used in combination with `marshal` to marshal code (images), such that
  `(make-image x)` is the same as `(marshal x make-image-dict)`.``
  @{})

(defmacro comptime
  "Evals x at compile time and returns the result. Similar to a top level unquote."
  [x]
  (eval x))

(defmacro compif
  "Check the condition `cnd` at compile time -- if truthy, compile `tru`, else compile `fals`."
  [cnd tru &opt fals]
  (if (eval cnd)
    tru
    fals))

(defmacro compwhen
  "Check the condition `cnd` at compile time -- if truthy, compile `(upscope ;body)`, else compile nil."
  [cnd & body]
  (if (eval cnd)
    ~(upscope ,;body)))

(defn make-image
  ``Create an image from an environment returned by `require`.
  Returns the image source as a string.``
  [env]
  (marshal env make-image-dict))

(defn load-image
  "The inverse operation to `make-image`. Returns an environment."
  [image]
  (unmarshal image load-image-dict))

(defn- check-relative [x] (if (string/has-prefix? "." x) x))
(defn- check-not-relative [x] (if-not (string/has-prefix? "." x) x))
(defn- check-is-dep [x] (unless (or (string/has-prefix? "/" x) (string/has-prefix? "." x)) x))
(defn- check-project-relative [x] (if (string/has-prefix? "/" x) x))

(def module/cache
  "A table, mapping loaded module identifiers to their environments."
  @{})

(def module/paths
  ```
  The list of paths to look for modules, templated for `module/expand-path`.
  Each element is a two-element tuple, containing the path
  template and a keyword :source, :native, or :image indicating how
  `require` should load files found at these paths.

  A tuple can also
  contain a third element, specifying a filter that prevents `module/find`
  from searching that path template if the filter doesn't match the input
  path. The filter can be a string or a predicate function, and
  is often a file extension, including the period.
  ```
  @[])

(defn module/add-paths
  ```
  Add paths to `module/paths` for a given loader such that
  the generated paths behave like other module types, including
  relative imports and syspath imports. `ext` is the file extension
  to associate with this module type, including the dot. `loader` is the
  keyword name of a loader in `module/loaders`. Returns the modified `module/paths`.
  ```
  [ext loader]
  (defn- find-prefix
    [pre]
    (or (find-index |(and (string? ($ 0)) (string/has-prefix? pre ($ 0))) module/paths) 0))
  (def all-index (find-prefix ".:all:"))
  (array/insert module/paths all-index [(string ".:all:" ext) loader check-project-relative])
  (def sys-index (find-prefix ":sys:"))
  (array/insert module/paths sys-index [(string ":sys:/:all:" ext) loader check-is-dep])
  (def curall-index (find-prefix ":cur:/:all:"))
  (array/insert module/paths curall-index [(string ":cur:/:all:" ext) loader check-relative])
  module/paths)

(module/add-paths ":native:" :native)
(module/add-paths "/init.janet" :source)
(module/add-paths ".janet" :source)
(module/add-paths ".jimage" :image)
(array/insert module/paths 0 [(fn is-cached [path] (if (in module/cache path) path)) :preload check-not-relative])

# Version of fexists that works even with a reduced OS
(defn- fexists
  [path]
  (compif (dyn 'os/stat)
    (= :file (os/stat path :mode))
    (when-let [f (file/open path :rb)]
      (def res
        (try (do (file/read f 1) true)
          ([err] nil)))
      (file/close f)
      res)))

(defn- mod-filter
  [x path]
  (case (type x)
    :nil path
    :string (string/has-suffix? x path)
    (x path)))

(defn module/find
  ```
  Try to match a module or path name from the patterns in `module/paths`.
  Returns a tuple (fullpath kind) where the kind is one of :source, :native,
  or :image if the module is found, otherwise a tuple with nil followed by
  an error message.
  ```
  [path]
  (var ret nil)
  (each [p mod-kind checker] module/paths
    (when (mod-filter checker path)
      (if (function? p)
        (when-let [res (p path)]
          (set ret [res mod-kind])
          (break))
        (do
          (def fullpath (string (module/expand-path path p)))
          (when (fexists fullpath)
            (set ret [fullpath mod-kind])
            (break))))))
  (if ret ret
    (let [expander (fn [[t _ chk]]
                     (when (string? t)
                       (when (mod-filter chk path)
                         (module/expand-path path t))))
          paths (filter identity (map expander module/paths))
          str-parts (interpose "\n    " paths)]
      [nil (string "could not find module " path ":\n    " ;str-parts)])))

(def module/loading
  `A table, mapping currently loading modules to true. Used to prevent
  circular dependencies.`
  @{})

(defn module/value
  ``Given a module table, get the value bound to a symbol `sym`. If `private` is
  truthy, will also resolve private module symbols. If no binding is found, will return
  nil.``
  [module sym &opt private]
  (def entry (get module sym))
  (if entry
    (let [v (in entry :value)
          r (in entry :ref)
          p (in entry :private)]
      (if p (if private nil (break)))
      (if (and r (array? r))
        (get r 0)
        v))))

(def debugger-env
  "An environment that contains dot prefixed functions for debugging."
  @{})

(var- debugger-on-status-var nil)

(defn debugger
  "Run a repl-based debugger on a fiber. Optionally pass in a level
  to differentiate nested debuggers."
  [fiber &opt level]
  (default level 1)
  (def nextenv (make-env (fiber/getenv fiber)))
  (put nextenv :fiber fiber)
  (put nextenv :debug-level level)
  (put nextenv :signal (fiber/last-value fiber))
  (merge-into nextenv debugger-env)
  (defn debugger-chunks [buf p]
    (def status (:state p :delimiters))
    (def c ((:where p) 0))
    (def prpt (string "debug[" level "]:" c ":" status "> "))
    (getline prpt buf nextenv))
  (eprint "entering debug[" level "] - (quit) to exit")
  (flush)
  (run-context
    {:chunks debugger-chunks
     :on-status (debugger-on-status-var nextenv (+ 1 level) true)
     :env nextenv})
  (eprint "exiting debug[" level "]")
  (flush)
  (nextenv :resume-value))

(defn debugger-on-status
  "Create a function that can be passed to `run-context`'s `:on-status`
  argument that will drop into a debugger on errors. The debugger will
  only start on abnormal signals if the env table has the `:debug` dyn
  set to a truthy value."
  [env &opt level is-repl]
  (default level 1)
  (fn [f x]
    (def fs (fiber/status f))
    (if (= :dead fs)
      (when is-repl
        (put env '_ @{:value x})
        (printf (get env :pretty-format "%q") x)
        (flush))
      (do
        (debug/stacktrace f x "")
        (eflush)
        (if (get env :debug) (debugger f level))))))

(set debugger-on-status-var debugger-on-status)

(defn dofile
  ``Evaluate a file, file path, or stream and return the resulting environment. :env, :expander,
  :source, :evaluator, :read, and :parser are passed through to the underlying
  `run-context` call. If `exit` is true, any top level errors will trigger a
  call to `(os/exit 1)` after printing the error.``
  [path &named exit env source expander evaluator read parser]
  (def f (case (type path)
           :core/file path
           :core/stream path
           (file/open path :rb)))
  (def path-is-file (= f path))
  (default env (make-env))
  (def spath (string path))
  (put env :source (or source (if-not path-is-file spath path)))
  (var exit-error nil)
  (var exit-fiber nil)
  (defn chunks [buf _] (:read f 4096 buf))
  (defn bp [&opt x y]
    (when exit
      (bad-parse x y)
      (os/exit 1))
    (put env :exit true)
    (def buf @"")
    (with-dyns [*err* buf *err-color* false]
      (bad-parse x y))
    (set exit-error (string/slice buf 0 -2)))
  (defn bc [&opt x y z a b]
    (when exit
      (bad-compile x y z a b)
      (os/exit 1))
    (put env :exit true)
    (def buf @"")
    (with-dyns [*err* buf *err-color* false]
      (bad-compile x nil z a b))
    (set exit-error (string/slice buf 0 -2))
    (set exit-fiber y))
  (unless f
    (error (string "could not find file " path)))
  (def nenv
    (run-context {:env env
                  :chunks chunks
                  :on-parse-error bp
                  :on-compile-error bc
                  :on-status (fn [f x]
                               (when (not= (fiber/status f) :dead)
                                 (when exit
                                   (debug/stacktrace f x "")
                                   (eflush)
                                   (os/exit 1))
                                 (if (get env :debug)
                                   ((debugger-on-status env) f x)
                                   (do
                                     (put env :exit true)
                                     (set exit-error x)
                                     (set exit-fiber f)))))
                  :evaluator evaluator
                  :expander expander
                  :read read
                  :parser parser
                  :source (or source (if path-is-file :<anonymous> spath))}))
  (if-not path-is-file (:close f))
  (when exit-error
    (if exit-fiber
      (propagate exit-error exit-fiber)
      (error exit-error)))
  nenv)

(def module/loaders
  ``A table of loading method names to loading functions.
  This table lets `require` and `import` load many different kinds
  of files as modules.``
  @{:native (fn native-loader [path &] (native path (make-env)))
    :source (fn source-loader [path args]
              (put module/loading path true)
              (defer (put module/loading path nil)
                (dofile path ;args)))
    :preload (fn preload-loader [path & args]
               (when-let [m (in module/cache path)]
                 (if (function? m)
                   (set (module/cache path) (m path ;args))
                   m)))
    :image (fn image-loader [path &] (load-image (slurp path)))})

(defn- require-1
  [path args kargs]
  (def [fullpath mod-kind] (module/find path))
  (unless fullpath (error mod-kind))
  (if-let [check (if-not (kargs :fresh) (in module/cache fullpath))]
    check
    (if (module/loading fullpath)
      (error (string "circular dependency " fullpath " detected"))
      (do
        (def loader (if (keyword? mod-kind) (module/loaders mod-kind) mod-kind))
        (unless loader (error (string "module type " mod-kind " unknown")))
        (def env (loader fullpath args))
        (put module/cache fullpath env)
        env))))

(defn require
  ``Require a module with the given name. Will search all of the paths in
  `module/paths`. Returns the new environment
  returned from compiling and running the file.``
  [path & args]
  (require-1 path args (struct ;args)))

(defn merge-module
  ``Merge a module source into the `target` environment with a `prefix`, as with the `import` macro.
  This lets users emulate the behavior of `import` with a custom module table.
  If `export` is truthy, then merged functions are not marked as private. Returns
  the modified target environment.``
  [target source &opt prefix export]
  (loop [[k v] :pairs source :when (symbol? k) :when (not (v :private))]
    (def newv (table/setproto @{:private (not export)} v))
    (put target (symbol prefix k) newv))
  target)

(defn import*
  ``Function form of `import`. Same parameters, but the path
  and other symbol parameters should be strings instead.``
  [path & args]
  (def env (curenv))
  (def kargs (table ;args))
  (def {:as as
        :prefix prefix
        :export ep} kargs)
  (def newenv (require-1 path args kargs))
  (def prefix (or
                (and as (string as "/"))
                prefix
                (string (last (string/split "/" path)) "/")))
  (merge-module env newenv prefix ep))

(defmacro import
  ``Import a module. First requires the module, and then merges its
  symbols into the current environment, prepending a given prefix as needed.
  (use the :as or :prefix option to set a prefix). If no prefix is provided,
  use the name of the module as a prefix. One can also use "`:export true`"
  to re-export the imported symbols. If "`:exit true`" is given as an argument,
  any errors encountered at the top level in the module will cause `(os/exit 1)`
  to be called. Dynamic bindings will NOT be imported. Use :fresh to bypass the
  module cache.``
  [path & args]
  (def ps (partition 2 args))
  (def argm (mapcat (fn [[k v]] [k (if (= k :as) (string v) v)]) ps))
  (tuple import* (string path) ;argm))

(defmacro use
  ``Similar to `import`, but imported bindings are not prefixed with a module
  identifier. Can also import multiple modules in one shot.``
  [& modules]
  ~(do ,;(map |~(,import* ,(string $) :prefix "") modules)))

###
###
### Documentation
###
###

(defn- env-walk
  [pred &opt env local]
  (default env (fiber/getenv (fiber/current)))
  (def envs @[])
  (do (var e env) (while e (array/push envs e) (set e (table/getproto e)) (if local (break))))
  (def ret-set @{})
  (loop [envi :in envs
         k :keys envi
         :when (pred k)]
    (put ret-set k true))
  (sort (keys ret-set)))

(defn all-bindings
  ``Get all symbols available in an environment. Defaults to the current
  fiber's environment. If `local` is truthy, will not show inherited bindings
  (from prototype tables).``
  [&opt env local]
  (env-walk symbol? env local))

(defn all-dynamics
  ``Get all dynamic bindings in an environment. Defaults to the current
  fiber's environment. If `local` is truthy, will not show inherited bindings
  (from prototype tables).``
  [&opt env local]
  (env-walk keyword? env local))

(defdyn *doc-width*
  "Width in columns to print documentation printed with `doc-format`.")

(defdyn *doc-color*
  "Whether or not to colorize documentation printed with `doc-format`.")

(defn doc-format
  `Reformat a docstring to wrap a certain width. Docstrings can either be plaintext
  or a subset of markdown. This allows a long single line of prose or formatted text to be
  a well-formed docstring. Returns a buffer containing the formatted text.`
  [str &opt width indent colorize]

  (default indent 4)
  (def max-width (- (or width (dyn *doc-width* 80)) 8))
  (def has-color (if (not= nil colorize)
                   colorize
                   (dyn *doc-color*)))

  # Terminal codes for emission/tokenization
  (def delimiters
    (if has-color
      {:underline ["\e[4m" "\e[24m"]
       :code ["\e[97m" "\e[39m"]
       :italics ["\e[4m" "\e[24m"]
       :bold ["\e[1m" "\e[22m"]}
      {:underline ["_" "_"]
       :code ["`" "`"]
       :italics ["*" "*"]
       :bold ["**" "**"]}))
  (def modes @{})
  (defn toggle-mode [mode]
    (def active (get modes mode))
    (def delims (get delimiters mode))
    (put modes mode (not active))
    (delims (if active 1 0)))

  # Parse state
  (var cursor 0) # indexes into string for parsing
  (var stack @[]) # return value for this block.

  # Traversal helpers
  (defn c [] (get str cursor))
  (defn cn [n] (get str (+ n cursor)))
  (defn c++ [] (let [ret (get str cursor)] (++ cursor) ret))
  (defn c+=n [n] (let [ret (get str cursor)] (+= cursor n) ret))
  # skip* functions return number of characters matched and advance the cursor.
  (defn skipwhite []
    (def x cursor)
    (while (= (c) (chr " ")) (++ cursor))
    (- cursor x))
  (defn skipline []
    (def x cursor)
    (while (let [y (c)] (and y (not= y (chr "\n")))) (++ cursor))
    (c++)
    (- cursor x))

  # Detection helpers - return number of characters matched
  (defn ul? []
    (let [x (c) x1 (cn 1)]
      (and
        (= x1 (chr " "))
        (or (= x (chr "*")) (= x (chr "-")))
        2)))
  (defn ol? []
    (def old cursor)
    (while (and (>= (c) (chr "0")) (<= (c) (chr "9"))) (c++))
    (let [c1 (c) c2 (cn 1) c* cursor]
      (set cursor old)
      (if (and (= c1 (chr ".")) (= c2 (chr " ")))
        (- c* cursor -2))))
  (defn fcb? [] (if (= (chr "`") (c) (cn 1) (cn 2)) 3))
  (defn nl? [] (= (chr "\n") (c)))

  # Parse helper
  # parse-* functions push nodes to `stack`, and return
  # the indentation they leave the cursor on.

  (var parse-blocks nil) # mutual recursion
  (defn getslice [from to]
    (def to (min to (length str)))
    (string/slice str from to))
  (defn push [x] (array/push stack x))

  (defn parse-list [bullet-check initial indent]
    (def temp-stack @[initial])
    (def old-stack stack)
    (set stack temp-stack)
    (var current-indent indent)
    (while (and (c) (>= current-indent indent))
      (def item-indent
        (when-let [x (bullet-check)]
          (c+=n x)
          (+ indent (skipwhite) x)))
      (unless item-indent
        (set current-indent (skipwhite))
        (break))
      (def item-stack @[])
      (set stack item-stack)
      (set current-indent (parse-blocks item-indent))
      (set stack temp-stack)
      (push item-stack))
    (set stack old-stack)
    (push temp-stack)
    current-indent)

  (defn add-codeblock [indent start end]
    (def replace-chunk (string "\n" (string/repeat " " indent)))
    (push @[:cb (string/replace-all replace-chunk "\n" (getslice start end))])
    (skipline)
    (skipwhite))

  (defn parse-fcb [indent]
    (c+=n 3)
    (skipline)
    (c+=n indent)
    (def start cursor)
    (var end cursor)
    (while (c)
      (if (fcb?) (break))
      (skipline)
      (set end cursor)
      (skipwhite))
    (add-codeblock indent start end))

  (defn parse-icb [indent]
    (var current-indent indent)
    (def start cursor)
    (var end cursor)
    (while (c)
      (skipline)
      (set end cursor)
      (set current-indent (skipwhite))
      (if (< current-indent indent) (break)))
    (add-codeblock indent start end))

  (defn tokenize-line [line]
    (def tokens @[])
    (def token @"")
    (var token-length 0)
    (defn delim [mode]
      (def d (toggle-mode mode))
      (if-not has-color (+= token-length (length d)))
      (buffer/push token d))
    (defn endtoken []
      (if (first token) (array/push tokens [(string token) token-length]))
      (buffer/clear token)
      (set token-length 0))
    (forv i 0 (length line)
      (def b (get line i))
      (cond
        (or (= b (chr "\n")) (= b (chr " "))) (endtoken)
        (= b (chr "`")) (delim :code)
        (not (modes :code)) (cond
          (= b (chr `\`)) (do
                            (++ token-length)
                            (buffer/push token (get line (++ i))))
          (= b (chr "_")) (delim :underline)
          (= b (chr "*"))
            (if (= (chr "*") (get line (+ i 1)))
              (do (++ i)
                (delim :bold))
              (delim :italics))
          (do (++ token-length) (buffer/push token b)))
        (do (++ token-length) (buffer/push token b))))
    (endtoken)
    (tuple/slice tokens))

  (set
    parse-blocks
    (fn parse-blocks [indent]
      (var new-indent indent)
      (var p-start nil)
      (var p-end nil)
      (defn p-line []
        (unless p-start
          (set p-start cursor))
        (skipline)
        (set p-end cursor)
        (set new-indent (skipwhite)))
      (defn finish-p []
        (when (and p-start (> p-end p-start))
          (push (tokenize-line (getslice p-start p-end)))
          (set p-start nil)))
      (while (and (c) (>= new-indent indent))
        (cond
          (nl?) (do (finish-p) (c++) (set new-indent (skipwhite)))
          (ul?) (do (finish-p) (set new-indent (parse-list ul? :ul new-indent)))
          (ol?) (do (finish-p) (set new-indent (parse-list ol? :ol new-indent)))
          (fcb?) (do (finish-p) (set new-indent (parse-fcb new-indent)))
          (>= new-indent (+ 4 indent)) (do (finish-p) (set new-indent (parse-icb new-indent)))
          (p-line)))
      (finish-p)
      new-indent))

  # Handle first line specially for defn, defmacro, etc.
  (when (= (chr "(") (in str 0))
    (skipline)
    (def first-line (string/slice str 0 (- cursor 1)))
    (def fl-open (if has-color "\e[97m" ""))
    (def fl-close (if has-color "\e[39m" ""))
    (push [[(string fl-open first-line fl-close) (length first-line)]]))

  (parse-blocks 0)

  # Emission state
  (def buf @"")
  (var current-column 0)

  # Emission
  (defn emit-indent [indent]
    (def delta (- indent current-column))
    (when (< 0 delta)
      (buffer/push buf (string/repeat " " delta))
      (set current-column indent)))

  (defn emit-nl [&opt indent]
    (buffer/push buf "\n")
    (set current-column 0))

  (defn emit-word [word indent &opt len]
    (def last-byte (last buf))
    (when (and
            last-byte
            (not= last-byte (chr "\n"))
            (not= last-byte (chr " ")))
      (buffer/push buf " ")
      (++ current-column))
    (default len (length word))
    (when (and indent (> (+ 1 current-column len) max-width))
      (emit-nl)
      (emit-indent indent))
    (buffer/push buf word)
    (+= current-column len))

  (defn emit-code
    [code indent]
    (def replacement (string "\n" (string/repeat " " (+ 4 indent))))
    (emit-indent (+ 4 indent))
    (buffer/push buf (string/replace-all "\n" replacement code))
    (if (= (chr "\n") (last code))
      (set current-column 0)
      (emit-nl)))

  (defn emit-node
    [el indent]
    (emit-indent indent)
    (if (tuple? el)
      (let [rep (string "\n" (string/repeat " " indent))]
        (each [word len] el
          (emit-word
            (string/replace-all "\n" rep word)
            indent
            len))
        (emit-nl))
      (case (first el)
        :ul (for i 1 (length el)
              (if (> i 1) (emit-indent indent))
              (emit-word "* " nil)
              (each subel (get el i) (emit-node subel (+ 2 indent))))
        :ol (for i 1 (length el)
              (if (> i 1) (emit-indent indent))
              (def lab (string/format "%d. " i))
              (emit-word lab nil)
              (each subel (get el i) (emit-node subel (+ (length lab) indent))))
        :cb (emit-code (get el 1) indent))))

  (each el stack
    (emit-nl)
    (emit-node el indent))

  buf)

(defn- print-index
  "Print bindings in the current environment given a filter function."
  [fltr]
  (def bindings (filter fltr (all-bindings)))
  (def dynamics (map describe (filter fltr (all-dynamics))))
  (print)
  (print (doc-format (string "Bindings:\n\n" (string/join bindings " ")) nil nil false))
  (print)
  (print (doc-format (string "Dynamics:\n\n" (string/join dynamics " ")) nil nil false))
  (print "\n    Use (doc sym) for more information on a binding.\n"))

(defn- print-module-entry
  [x]
  (def bind-type
    (string "    "
            (cond
              (x :redef) (type (in (x :ref) 0))
              (x :ref) (string :var " (" (type (in (x :ref) 0)) ")")
              (x :macro) :macro
              (x :module) (string :module " (" (x :kind) ")")
              (type (x :value)))
            "\n"))
  (def sm (x :source-map))
  (def d (x :doc))
  (print "\n\n"
         bind-type
         (when-let [[path line col] sm]
           (string "    " path (when (and line col) (string " on line " line ", column " col))))
         (when sm "\n")
         (if d (doc-format d) "\n    no documentation found.\n")
         "\n"))

(defn- print-special-form-entry
  [x]
  (print "\n\n"
         (string "    special form\n\n")
         (string "    (" x " ...)\n\n")
         (string "    See https://janet-lang.org/docs/specials.html\n\n")))

(defn doc*
  "Get the documentation for a symbol in a given environment. Function form of `doc`."
  [&opt sym]

  (cond
    (string? sym)
    (print-index (fn [x] (string/find sym x)))

    sym
    (do
      (def x (dyn sym))
      (if (not x)
        (if (index-of sym '[break def do fn if quasiquote quote
                            set splice unquote upscope var while])
          (print-special-form-entry sym)
          (do
            (def [fullpath mod-kind] (module/find (string sym)))
            (if-let [mod-env (in module/cache fullpath)]
              (print-module-entry {:module true
                                   :kind mod-kind
                                   :source-map [fullpath nil nil]
                                   :doc (in mod-env :doc)})
              (print "symbol " sym " not found."))))
        (print-module-entry x)))

    # else
    (print-index identity)))

(defmacro doc
  ``Shows documentation for the given symbol, or can show a list of available bindings.
  If `sym` is a symbol, will look for documentation for that symbol. If `sym` is a string
  or is not provided, will show all lexical and dynamic bindings in the current environment
  containing that string (all bindings will be shown if no string is given).``
  [&opt sym]
  ~(,doc* ',sym))

(defn doc-of
  `Searches all loaded modules in module/cache for a given binding and prints out its documentation.
  This does a search by value instead of by name. Returns nil.`
  [x]
  (var found false)
  (loop [module-set :in [[root-env] module/cache]
         module :in module-set
         value :in module]
    (let [check (or (get value :ref) (get value :value))]
      (when (= check x)
        (print-module-entry value)
        (set found true)
        (break))))
  (if-not found
    (print "documentation for value " x " not found.")))

###
###
### Debugger
###
###

(defn .fiber
  "Get the current fiber being debugged."
  []
  (dyn :fiber))

(defn .signal
  "Get the current signal being debugged."
  []
  (dyn :signal))

(defn .stack
  "Print the current fiber stack."
  []
  (print)
  (with-dyns [*err-color* false] (debug/stacktrace (.fiber) (.signal) ""))
  (print))

(defn .frame
  "Show a stack frame."
  [&opt n]
  (def stack (debug/stack (.fiber)))
  (in stack (or n 0)))

(defn .fn
  "Get the current function."
  [&opt n]
  (in (.frame n) :function))

(defn .slots
  "Get an array of slots in a stack frame."
  [&opt n]
  (in (.frame n) :slots))

(defn .slot
  "Get the value of the nth slot."
  [&opt nth frame-idx]
  (in (.slots frame-idx) (or nth 0)))

# Conditional compilation for disasm
(compwhen (dyn 'disasm)

  (defn .disasm
    "Gets the assembly for the current function."
    [&opt n]
    (def frame (.frame n))
    (def func (frame :function))
    (disasm func))

  (defn .bytecode
    "Get the bytecode for the current function."
    [&opt n]
    ((.disasm n) :bytecode))

  (defn .ppasm
    "Pretty prints the assembly for the current function."
    [&opt n]
    (def frame (.frame n))
    (def func (frame :function))
    (def dasm (disasm func))
    (def bytecode (in dasm :bytecode))
    (def pc (frame :pc))
    (def sourcemap (in dasm :sourcemap))
    (var last-loc [-2 -2])
    (eprint "\n  signal:     " (.signal))
    (eprint "  status:     " (fiber/status (.fiber)))
    (eprint "  function:   " (get dasm :name "<anonymous>") " [" (in dasm :source "") "]")
    (when-let [constants (dasm :constants)]
      (eprintf "  constants:  %.4q" constants))
    (eprintf "  slots:      %.4q\n" (frame :slots))
    (def padding (string/repeat " " 20))
    (loop [i :range [0 (length bytecode)]
           :let [instr (bytecode i)]]
      (eprin (if (= (tuple/type instr) :brackets) "*" " "))
      (eprin (if (= i pc) "> " "  "))
      (eprinf "%.20s" (string (string/join (map string instr) " ") padding))
      (when sourcemap
        (let [[sl sc] (sourcemap i)
              loc [sl sc]]
          (when (not= loc last-loc)
            (set last-loc loc)
            (eprin " # line " sl ", column " sc))))
      (eprint))
    (eprint))

  (defn .breakall
    "Set breakpoints on all instructions in the current function."
    [&opt n]
    (def fun (.fn n))
    (def bytecode (.bytecode n))
    (forv i 0 (length bytecode)
      (debug/fbreak fun i))
    (eprint "set " (length bytecode) " breakpoints in " fun))

  (defn .clearall
    "Clear all breakpoints on the current function."
    [&opt n]
    (def fun (.fn n))
    (def bytecode (.bytecode n))
    (forv i 0 (length bytecode)
      (debug/unfbreak fun i))
    (eprint "cleared " (length bytecode) " breakpoints in " fun)))

(defn .source
  "Show the source code for the function being debugged."
  [&opt n]
  (def frame (.frame n))
  (def s (frame :source))
  (def all-source (slurp s))
  (eprint "\n" all-source "\n"))

(defn .break
  "Set breakpoint at the current pc."
  []
  (def frame (.frame))
  (def fun (frame :function))
  (def pc (frame :pc))
  (debug/fbreak fun pc)
  (eprint "set breakpoint in " fun " at pc=" pc))

(defn .clear
  "Clear the current breakpoint."
  []
  (def frame (.frame))
  (def fun (frame :function))
  (def pc (frame :pc))
  (debug/unfbreak fun pc)
  (eprint "cleared breakpoint in " fun " at pc=" pc))

(defn .next
  "Go to the next breakpoint."
  [&opt n]
  (var res nil)
  (forv i 0 (or n 1)
    (set res (resume (.fiber))))
  res)

(defn .nextc
  "Go to the next breakpoint, clearing the current breakpoint."
  [&opt n]
  (.clear)
  (.next n))

(defn .step
  "Execute the next n instructions."
  [&opt n]
  (var res nil)
  (forv i 0 (or n 1)
    (set res (debug/step (.fiber))))
  res)

(def- debugger-keys (filter (partial string/has-prefix? ".") (keys root-env)))
(each k debugger-keys (put debugger-env k (root-env k)) (put root-env k nil))

###
###
### REPL
###
###

(defn repl
  ``Run a repl. The first parameter is an optional function to call to
  get a chunk of source code that should return nil for end of file.
  The second parameter is a function that is called when a signal is
  caught. One can provide an optional environment table to run
  the repl in, as well as an optional parser or read function to pass
  to `run-context`.``
  [&opt chunks onsignal env parser read]
  (default env (make-env))
  (default chunks
    (fn [buf p]
      (getline
        (string
          "repl:"
          ((:where p) 0)
          ":"
          (:state p :delimiters) "> ")
        buf env)))
  (run-context {:env env
                :chunks chunks
                :on-status (or onsignal (debugger-on-status env 1 true))
                :parser parser
                :read read
                :source :repl}))

###
###
### Extras
###
###

(compwhen (dyn 'ev/go)

  (defn net/close "Alias for `ev/close`." [stream] (ev/close stream))

  (defn ev/call
    ```
    Call a function asynchronously.
    Returns a fiber that is scheduled to run the function.
    ```
    [f & args]
    (ev/go (fn _call [&] (f ;args))))

  (defmacro ev/spawn
    "Run some code in a new fiber. This is shorthand for `(ev/call (fn [] ;body))`."
    [& body]
    ~(,ev/go (fn _spawn [&] ,;body)))

  (defmacro ev/do-thread
    ``Run some code in a new thread. Suspends the current fiber until the thread is complete, and
    evaluates to nil.``
    [& body]
    ~(,ev/thread (fn _do-thread [&] ,;body)))

  (defmacro ev/spawn-thread
    ``Run some code in a new thread. Like `ev/do-thread`, but returns nil immediately.``
    [& body]
    ~(,ev/thread (fn _spawn-thread [&] ,;body) nil :n))

  (defmacro ev/with-deadline
    `Run a body of code with a deadline, such that if the code does not complete before
    the deadline is up, it will be canceled.`
    [deadline & body]
    (with-syms [f]
      ~(let [,f (coro ,;body)]
         (,ev/deadline ,deadline nil ,f)
         (,resume ,f))))

  (defn- wait-for-fibers
    [chan fibers]
    (repeat (length fibers)
      (def [sig fiber] (ev/take chan))
      (unless (= sig :ok)
        (each f fibers (ev/cancel f "sibling canceled"))
        (propagate (fiber/last-value fiber) fiber))))

  (defmacro ev/gather
    ``
    Run a number of fibers in parallel on the event loop, and join when they complete.
    Returns the gathered results in an array.
    ``
    [& bodies]
    (with-syms [chan res]
      ~(do
         (def ,chan (,ev/chan))
         (def ,res @[])
         (,wait-for-fibers ,chan
                           ,(seq [[i body] :pairs bodies]
                              ~(,ev/go (fn [] (put ,res ,i ,body)) nil ,chan)))
         ,res))))

(compwhen (dyn 'net/listen)
  (defn net/server
    "Start a server asynchronously with `net/listen` and `net/accept-loop`. Returns the new server stream."
    [host port &opt handler type]
    (def s (net/listen host port type))
    (if handler
      (ev/call (fn [] (net/accept-loop s handler))))
    s))

###
###
### FFI Extra
###
###

(defmacro delay
  "Lazily evaluate a series of expressions. Returns a function that
  returns the result of the last expression. Will only evaluate the
  body once, and then memoizes the result."
  [& forms]
  (def state (gensym))
  (def loaded (gensym))
  ~((fn []
      (var ,state nil)
      (var ,loaded nil)
      (fn []
        (if ,loaded
          ,state
          (do
            (set ,loaded true)
            (set ,state (do ,;forms))))))))

(compwhen (dyn 'ffi/native)

  (defdyn *ffi-context* " Current native library for ffi/bind and other settings")

  (defn- default-mangle
    [name &]
    (string/replace-all "-" "_" name))

  (defn ffi/context
    "Set the path of the dynamic library to implictly bind, as well
     as other global state for ease of creating native bindings."
    [&opt native-path &named map-symbols lazy]
    (default map-symbols default-mangle)
    (def lib (if lazy nil (ffi/native native-path)))
    (def lazy-lib (if lazy (delay (ffi/native native-path))))
    (setdyn *ffi-context*
            @{:native-path native-path
              :native lib
              :native-lazy lazy-lib
              :lazy lazy
              :map-symbols map-symbols}))

  (defmacro ffi/defbind
    "Generate bindings for native functions in a convenient manner."
    [name ret-type & body]
    (def real-ret-type (eval ret-type))
    (def meta (slice body 0 -2))
    (def arg-pairs (partition 2 (last body)))
    (def formal-args (map 0 arg-pairs))
    (def type-args (map 1 arg-pairs))
    (def computed-type-args (eval ~[,;type-args]))
    (def {:native lib
          :lazy lazy
          :native-lazy llib
          :map-symbols ms} (assert (dyn *ffi-context*) "no ffi context found"))
    (def raw-symbol (ms name))
    (defn make-sig []
      (ffi/signature :default real-ret-type ;computed-type-args))
    (defn make-ptr []
      (assert (ffi/lookup (if lazy (llib) lib) raw-symbol) (string "failed to find ffi symbol " raw-symbol)))
    (if lazy
        ~(defn ,name ,;meta [,;formal-args]
           (,ffi/call (,(delay (make-ptr))) (,(delay (make-sig))) ,;formal-args))
        ~(defn ,name ,;meta [,;formal-args]
           (,ffi/call ,(make-ptr) ,(make-sig) ,;formal-args)))))

###
###
### Flychecking
###
###

(defn- no-side-effects
  `Check if form may have side effects. If returns true, then the src
  must not have side effects, such as calling a C function.`
  [src]
  (cond
    (tuple? src)
    (if (= (tuple/type src) :brackets)
      (all no-side-effects src))
    (array? src)
    (all no-side-effects src)
    (dictionary? src)
    (and (all no-side-effects (keys src))
         (all no-side-effects (values src)))
    true))

(defn- is-safe-def [x] (no-side-effects (last x)))

(def- safe-forms {'defn true 'varfn true 'defn- true 'defmacro true 'defmacro- true
                  'def is-safe-def 'var is-safe-def 'def- is-safe-def 'var- is-safe-def
                  'defglobal is-safe-def 'varglobal is-safe-def})

(def- importers {'import true 'import* true 'dofile true 'require true})
(defn- use-2 [evaluator args]
  (each a args (import* (string a) :prefix "" :evaluator evaluator)))

(defn- flycheck-evaluator
  ``An evaluator function that is passed to `run-context` that lints (flychecks) code.
  This means code will parsed and compiled, macros executed, but the code will not be run.
  Used by `flycheck`.``
  [thunk source env where]
  (when (tuple? source)
    (def head (source 0))
    (def safe-check
      (or
        (safe-forms head)
        (if (symbol? head)
          (if (string/has-prefix? "define-" head) is-safe-def))))
    (cond
      # Sometimes safe form
      (function? safe-check)
      (if (safe-check source) (thunk))
      # Always safe form
      safe-check
      (thunk)
      # Use
      (= 'use head)
      (use-2 flycheck-evaluator (tuple/slice source 1))
      # Import-like form
      (importers head)
      (let [[l c] (tuple/sourcemap source)
            newtup (tuple/setmap (tuple ;source :evaluator flycheck-evaluator) l c)]
        ((compile newtup env where))))))

(defn flycheck
  ``Check a file for errors without running the file. Found errors will be printed to stderr
  in the usual format. Macros will still be executed, however, so
  arbitrary execution is possible. Other arguments are the same as `dofile`. `path` can also be
  a file value such as stdin. Returns nil.``
  [path &keys kwargs]
  (def old-modcache (table/clone module/cache))
  (table/clear module/cache)
  (try
    (dofile path :evaluator flycheck-evaluator ;(kvs kwargs))
    ([e f]
      (debug/stacktrace f e "")))
  (table/clear module/cache)
  (merge-into module/cache old-modcache)
  nil)


###
###
### CLI Tool Main
###
###

# conditional compilation for reduced os
(def- getenv-alias (if-let [entry (in root-env 'os/getenv)] (entry :value) (fn [&])))

(defn- run-main
  [env subargs arg]
  (when-let [entry (in env 'main)
             main (or (get entry :value) (in (get entry :ref) 0))]
    (def guard (if (get env :debug) :ydt :y))
    (defn wrap-main [&]
      (main ;subargs))
    (def f (fiber/new wrap-main guard))
    (fiber/setenv f env)
    (var res nil)
    (while (fiber/can-resume? f)
      (set res (resume f res))
      (when (not= :dead (fiber/status f))
        ((debugger-on-status env) f res)))))

(defdyn *args*
  "Dynamic bindings that will contain command line arguments at program start.")

(defdyn *executable*
  ``Name of the interpreter executable used to execute this program. Corresponds to `argv[0]` in the call to
    `int main(int argc, char **argv);`.``)

(defdyn *profilepath*
  "Path to profile file loaded when starting up the repl.")

(defn cli-main
  `Entrance for the Janet CLI tool. Call this function with the command line
  arguments as an array or tuple of strings to invoke the CLI interface.`
  [args]

  (setdyn *args* args)

  (var should-repl false)
  (var no-file true)
  (var quiet false)
  (var raw-stdin false)
  (var handleopts true)
  (var exit-on-error true)
  (var colorize true)
  (var debug-flag false)
  (var compile-only false)
  (var warn-level nil)
  (var error-level nil)
  (var expect-image false)

  (if-let [jp (getenv-alias "JANET_PATH")] (setdyn *syspath* jp))
  (if-let [jprofile (getenv-alias "JANET_PROFILE")] (setdyn *profilepath* jprofile))

  (defn- get-lint-level
    [i]
    (def x (in args (+ i 1)))
    (or (scan-number x) (keyword x)))

  # Flag handlers
  (def handlers
    {"h" (fn [&]
           (print "usage: " (dyn *executable* "janet") " [options] script args...")
           (print
             ```
             Options are:
               -h : Show this help
               -v : Print the version string
               -s : Use raw stdin instead of getline like functionality
               -e code : Execute a string of janet
               -E code arguments... : Evaluate  an expression as a short-fn with arguments
               -d : Set the debug flag in the REPL
               -r : Enter the REPL after running all scripts
               -R : Disables loading profile.janet when JANET_PROFILE is present
               -p : Keep on executing if there is a top-level error (persistent)
               -q : Hide logo (quiet)
               -k : Compile scripts but do not execute (flycheck)
               -m syspath : Set system path for loading global modules
               -c source output : Compile janet source code into an image
               -i : Load the script argument as an image file instead of source code
               -n : Disable ANSI color output in the REPL
               -l lib : Use a module before processing more arguments
               -w level : Set the lint warning level - default is "normal"
               -x level : Set the lint error level - default is "none"
               -- : Stop handling options
             ```)
           (os/exit 0)
           1)
     "v" (fn [&] (print janet/version "-" janet/build) (os/exit 0) 1)
     "s" (fn [&] (set raw-stdin true) (set should-repl true) 1)
     "r" (fn [&] (set should-repl true) 1)
     "p" (fn [&] (set exit-on-error false) 1)
     "q" (fn [&] (set quiet true) 1)
     "i" (fn [&] (set expect-image true) 1)
     "k" (fn [&] (set compile-only true) (set exit-on-error false) 1)
     "n" (fn [&] (set colorize false) 1)
     "m" (fn [i &] (setdyn *syspath* (in args (+ i 1))) 2)
     "c" (fn c-switch [i &]
           (def path (in args (+ i 1)))
           (def e (dofile path))
           (spit (in args (+ i 2)) (make-image e))
           (set no-file false)
           3)
     "-" (fn [&] (set handleopts false) 1)
     "l" (fn l-switch [i &]
           (import* (in args (+ i 1))
                    :prefix "" :exit exit-on-error)
           2)
     "e" (fn e-switch [i &]
           (set no-file false)
           (eval-string (in args (+ i 1)))
           2)
     "E" (fn E-switch [i &]
           (set no-file false)
           (def subargs (array/slice args (+ i 2)))
           (def src ~(short-fn ,(parse (in args (+ i 1))) E-expression))
           (def thunk (compile src))
           (if (function? thunk)
             ((thunk) ;subargs)
             (error (get thunk :error)))
           math/inf)
     "d" (fn [&] (set debug-flag true) 1)
     "w" (fn [i &] (set warn-level (get-lint-level i)) 2)
     "x" (fn [i &] (set error-level (get-lint-level i)) 2)
     "R" (fn [&] (setdyn *profilepath* nil) 1)})

  (defn- dohandler [n i &]
    (def h (in handlers n))
    (if h (h i) (do (print "unknown flag -" n) ((in handlers "h")))))

  # Process arguments
  (var i 0)
  (def lenargs (length args))
  (while (< i lenargs)
    (def arg (in args i))
    (if (and handleopts (= "-" (string/slice arg 0 1)))
      (+= i (dohandler (string/slice arg 1) i))
      (do
        (def subargs (array/slice args i))
        (set no-file false)
        (if expect-image
          (do
            (def env (load-image (slurp arg)))
            (put env *args* subargs)
            (put env *lint-error* error-level)
            (put env *lint-warn* warn-level)
            (when debug-flag
              (put env *debug* true)
              (put env *redef* true))
            (run-main env subargs arg))
          (do
            (def env (make-env))
            (put env *args* subargs)
            (put env *lint-error* error-level)
            (put env *lint-warn* warn-level)
            (when debug-flag
              (put env *debug* true)
              (put env *redef* true))
            (if compile-only
              (flycheck arg :exit exit-on-error :env env)
              (do
                (dofile arg :exit exit-on-error :env env)
                (run-main env subargs arg)))))
        (set i lenargs))))

  (if (or should-repl no-file)
    (if
      compile-only (flycheck stdin :source :stdin :exit exit-on-error)
      (do
        (if-not quiet
          (print "Janet " janet/version "-" janet/build " " (os/which) "/" (os/arch) " - '(doc)' for help"))
        (flush)
        (defn getprompt [p]
          (def [line] (parser/where p))
          (string "repl:" line ":" (parser/state p :delimiters) "> "))
        (defn getstdin [prompt buf _]
          (file/write stdout prompt)
          (file/flush stdout)
          (file/read stdin :line buf))
        (def env (make-env))
        (when-let [profile.janet (dyn *profilepath*)]
          (def new-env (dofile profile.janet :exit true))
          (merge-module env new-env "" false))
        (when debug-flag
          (put env *debug* true)
          (put env *redef* true))
        (def getter (if raw-stdin getstdin getline))
        (defn getchunk [buf p]
          (getter (getprompt p) buf env))
        (setdyn *pretty-format* (if colorize "%.20Q" "%.20q"))
        (setdyn *err-color* (if colorize true))
        (setdyn *doc-color* (if colorize true))
        (setdyn *lint-error* error-level)
        (setdyn *lint-warn* error-level)
        (repl getchunk nil env)))))

###
###
### Bootstrap
###
###

(do

  # Modify root-env to remove private symbols and
  # flatten nested tables.
  (loop [[k v] :in (pairs root-env)
         :when (symbol? k)]
    (def flat (table/proto-flatten v))
    (when (boot/config :no-docstrings)
      (put flat :doc nil))
    (when (boot/config :no-sourcemaps)
      (put flat :source-map nil))
    # Fix directory separators on windows to make image identical between windows and non-windows
    (when-let [sm (get flat :source-map)]
      (put flat :source-map [(string/replace-all "\\" "/" (sm 0)) (sm 1) (sm 2)]))
    (if (v :private)
      (put root-env k nil)
      (put root-env k flat)))
  (put root-env 'boot/config nil)
  (put root-env 'boot/args nil)

  # Build dictionary for loading images
  (def load-dict (env-lookup root-env))
  (each [k v] (pairs load-dict)
    (if (number? v) (put load-dict k nil)))
  (merge-into load-image-dict load-dict)

  (def image
    (let [env-pairs (pairs (env-lookup root-env))
          essential-pairs (filter (fn [[k v]] (or (cfunction? v) (abstract? v))) env-pairs)
          lookup (table ;(mapcat identity essential-pairs))
          reverse-lookup (invert lookup)]
      # Check no duplicate values
      (def temp @{})
      (eachp [k v] lookup
        (if (in temp v) (errorf "duplicate value: %v" v))
        (put temp v k))
      (marshal root-env reverse-lookup)))

  # Create amalgamation

  (def feature-header "src/core/features.h")

  (def local-headers
    ["src/core/state.h"
     "src/core/util.h"
     "src/core/gc.h"
     "src/core/vector.h"
     "src/core/fiber.h"
     "src/core/regalloc.h"
     "src/core/compile.h"
     "src/core/emit.h"
     "src/core/symcache.h"])

  (def core-sources
    ["src/core/abstract.c"
     "src/core/array.c"
     "src/core/asm.c"
     "src/core/buffer.c"
     "src/core/bytecode.c"
     "src/core/capi.c"
     "src/core/cfuns.c"
     "src/core/compile.c"
     "src/core/corelib.c"
     "src/core/debug.c"
     "src/core/emit.c"
     "src/core/ev.c"
     "src/core/ffi.c"
     "src/core/fiber.c"
     "src/core/gc.c"
     "src/core/inttypes.c"
     "src/core/io.c"
     "src/core/marsh.c"
     "src/core/math.c"
     "src/core/net.c"
     "src/core/os.c"
     "src/core/parse.c"
     "src/core/peg.c"
     "src/core/pp.c"
     "src/core/regalloc.c"
     "src/core/run.c"
     "src/core/specials.c"
     "src/core/state.c"
     "src/core/string.c"
     "src/core/strtod.c"
     "src/core/struct.c"
     "src/core/symcache.c"
     "src/core/table.c"
     "src/core/tuple.c"
     "src/core/util.c"
     "src/core/value.c"
     "src/core/vector.c"
     "src/core/vm.c"
     "src/core/wrap.c"])

  # Print janet.c to stdout
  (print "/* Amalgamated build - DO NOT EDIT */")
  (print "/* Generated from janet version " janet/version "-" janet/build " */")
  (print "#define JANET_BUILD \"" janet/build "\"")
  (print ```#define JANET_AMALG```)

  (defn do-one-file
    [fname]
    (print "\n/* " fname " */")
    (print "#line 0 \"" fname "\"\n")
    (def source (slurp fname))
    (print (string/replace-all "\r" "" source)))

  (do-one-file feature-header)

  (print ```#include "janet.h"```)

  (each h local-headers
    (do-one-file h))

  # windows.h should not be included in any of the external or internal headers - only in .c files.
  (print)
  (print "/* Windows work around - winsock2 must be included before windows.h, especially in amalgamated build */")
  (print "#if defined(JANET_WINDOWS) && defined(JANET_NET)")
  (print "#include <winsock2.h>")
  (print "#endif")
  (print)

  (each s core-sources
    (do-one-file s))

  # Create C source file that contains images a uint8_t buffer. This
  # can be compiled and linked statically into the main janet library
  # and example client.
  (print "static const unsigned char janet_core_image_bytes[] = {")
  (loop [line :in (partition 16 image)]
    (prin "  ")
    (each b line
      (prinf "0x%.2X, " b))
    (print))
  (print "  0\n};\n")
  (print "const unsigned char *janet_core_image = janet_core_image_bytes;")
  (print "size_t janet_core_image_size = sizeof(janet_core_image_bytes);"))
