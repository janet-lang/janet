# The core janet library
# Copyright 2020 © Calvin Rose

###
###
### Macros and Basic Functions
###
###

(def defn :macro
  "(defn name & more)\n\nDefine a function. Equivalent to (def name (fn name [args] ...))."
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
  (apply defn name :macro more))

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
(defn nan? "Check if x is NaN" [x] (not= x x))
(defn even? "Check if x is even." [x] (= 0 (mod x 2)))
(defn odd? "Check if x is odd." [x] (= 1 (mod x 2)))
(defn zero? "Check if x is zero." [x] (= x 0))
(defn pos? "Check if x is greater than 0." [x] (> x 0))
(defn neg? "Check if x is less than 0." [x] (< x 0))
(defn one? "Check if x is equal to 1." [x] (= x 1))
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
(defn dictionary? "Check if x a table or struct." [x]
  (def t (type x))
  (if (= t :table) true (= t :struct)))
(defn indexed? "Check if x is an array or tuple." [x]
  (def t (type x))
  (if (= t :array) true (= t :tuple)))
(defn truthy? "Check if x is truthy." [x] (if x true false))
(defn true? "Check if x is true." [x] (= x true))
(defn false? "Check if x is false." [x] (= x false))
(defn nil? "Check if x is nil." [x] (= x nil))
(defn empty? "Check if xs is empty." [xs] (= (length xs) 0))

(def idempotent?
  "(idempotent? x)\n\nCheck if x is a value that evaluates to itself when compiled."
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
(defmacro *= "Shorthand for (set x (* x n))." [x n] ~(set ,x (,* ,x ,n)))
(defmacro /= "Shorthand for (set x (/ x n))." [x n] ~(set ,x (,/ ,x ,n)))
(defmacro %= "Shorthand for (set x (% x n))." [x n] ~(set ,x (,% ,x ,n)))

(defn assert
  "Throw an error if x is not truthy."
  [x &opt err]
  (if x x (error (if err err "assert failure"))))

(defn errorf
  "A combination of error and string/format. Equivalent to (error (string/format fmt ;args))"
  [fmt & args]
  (error (string/format fmt ;args)))

(defmacro default
  "Define a default value for an optional argument.
  Expands to (def sym (if (= nil sym) val sym))"
  [sym val]
  ~(def ,sym (if (= nil ,sym) ,val ,sym)))

(defmacro comment
  "Ignores the body of the comment."
  [&])

(defmacro if-not
  "Shorthand for (if (not condition) else then)."
  [condition then &opt else]
  ~(if ,condition ,else ,then))

(defmacro when
  "Evaluates the body when the condition is true. Otherwise returns nil."
  [condition & body]
  ~(if ,condition (do ,;body)))

(defmacro unless
  "Shorthand for (when (not condition) ;body). "
  [condition & body]
  ~(if ,condition nil (do ,;body)))

(defmacro cond
  "Evaluates conditions sequentially until the first true condition
  is found, and then executes the corresponding body. If there are an
  odd number of forms, the last expression is executed if no forms
  are matched. If there are no matches, return nil."
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
  "Select the body that equals the dispatch value. When pairs
  has an odd number of arguments, the last is the default expression.
  If no match is found, returns nil."
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
  "Create a scope and bind values to symbols. Each pair in bindings is
  assigned as if with def, and the body of the let form returns the last
  value."
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
  "Try something and catch errors. Body is any expression,
  and catch should be a form with the first element a tuple. This tuple
  should contain a binding for errors and an optional binding for
  the fiber wrapping the body. Returns the result of body if no error,
  or the result of catch if an error."
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
  "Evaluate expressions, while capturing any errors. Evaluates to a tuple
  of two elements. The first element is true if successful, false if an
  error, and the second is the return value or error."
  [& body]
  (let [f (gensym) r (gensym)]
    ~(let [,f (,fiber/new (fn [] ,;body) :ie)
           ,r (,resume ,f)]
       [(,not= :error (,fiber/status ,f)) ,r])))

(defmacro and
  "Evaluates to the last argument if all preceding elements are truthy, otherwise
  evaluates to the first falsey argument."
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
  "Evaluates to the last argument if all preceding elements are falsey, otherwise
  evaluates to the first truthy element."
  [& forms]
  (def len (length forms))
  (var i (- len 1))
  (var ret (in forms i))
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
  "Evaluates body with each symbol in syms bound to a generated, unique symbol."
  [syms & body]
  (var i 0)
  (def len (length syms))
  (def accum @[])
  (while (< i len)
    (array/push accum (in syms i) [gensym])
    (++ i))
  ~(let (,;accum) ,;body))

(defmacro defer
  "Run form unconditionally after body, even if the body throws an error.
  Will also run form if a user signal 0-4 is received."
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
  "Run form after body in the case that body terminates abnormally (an error or user signal 0-4).
  Otherwise, return last form in body."
  [form & body]
  (with-syms [f r]
    ~(do
       (def ,f (,fiber/new (fn [] ,;body) :ti))
       (def ,r (,resume ,f))
       (if (= (,fiber/status ,f) :dead)
         ,r
         (do ,form (,propagate ,r ,f))))))

(defmacro prompt
  "Set up a checkpoint that can be returned to. Tag should be a value
  that is used in a return statement, like a keyword."
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
  "Convert a string of length 1 to its byte (ascii) value at compile time."
  [c]
  (unless (and (string? c) (= (length c) 1))
    (error (string/format "expected string of length 1, got %v" c)))
  (c 0))

(defmacro label
  "Set a label point that is lexically scoped. Name should be a symbol
  that will be bound to the label."
  [name & body]
  ~(do
     (def ,name @"")
     ,(apply prompt name body)))

(defn return
  "Return to a prompt point."
  [to &opt value]
  (signal 0 [to value]))

(defmacro with
  "Evaluate body with some resource, which will be automatically cleaned up
  if there is an error in body. binding is bound to the expression ctor, and
  dtor is a function or callable that is passed the binding. If no destructor
  (dtor) is given, will call :close on the resource."
  [[binding ctor dtor] & body]
  ~(do
     (def ,binding ,ctor)
     ,(apply defer [(or dtor :close) binding] body)))

(defmacro when-with
  "Similar to with, but if binding is false or nil, returns
  nil without evaluating the body. Otherwise, the same as with."
  [[binding ctor dtor] & body]
  ~(if-let [,binding ,ctor]
     ,(apply defer [(or dtor :close) binding] body)))

(defmacro if-with
  "Similar to with, but if binding is false or nil, evaluates
  the falsey path. Otherwise, evaluates the truthy path. In both cases,
  ctor is bound to binding."
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

(defn- loop-fiber-template
  [binding expr body]
  (with-syms [f s]
    (def ds (if (idempotent? binding) binding (gensym)))
    ~(let [,f ,expr]
       (while true
         (def ,ds (,resume ,f))
         (if (= :dead (,fiber/status ,f)) (break))
         ,;(if (= ds binding) [] [~(def ,binding ,ds)])
         ,;body))))

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
      :generate (loop-fiber-template binding object [rest])
      (error (string "unexpected loop verb " verb)))))

(defmacro forv
  "Do a c style for loop for side effects. The iteration variable i
  can be mutated in the loop, unlike normal for. Returns nil."
  [i start stop & body]
  (for-var-template i start stop 1 < + body))

(defmacro for
  "Do a c style for loop for side effects. Returns nil."
  [i start stop & body]
  (for-template i start stop 1 < + body))

(defmacro eachk
  "Loop over each key in ds. Returns nil."
  [x ds & body]
  (each-template x ds :keys body))

(defmacro eachp
  "Loop over each (key, value) pair in ds. Returns nil."
  [x ds & body]
  (each-template x ds :pairs body))

(defmacro eachy
  "Resume a fiber in a loop until it has errored or died. Evaluate the body
  of the loop with binding set to the yielded value."
  [x fiber & body]
  (loop-fiber-template x fiber body))

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
  "Loop over each value in ds. Returns nil."
  [x ds & body]
  (each-template x ds :each body))

(defmacro loop
  "A general purpose loop macro. This macro is similar to the Common Lisp
  loop macro, although intentionally much smaller in scope.
  The head of the loop should be a tuple that contains a sequence of
  either bindings or conditionals. A binding is a sequence of three values
  that define something to loop over. They are formatted like:\n\n
  \tbinding :verb object/expression\n\n
  Where binding is a binding as passed to def, :verb is one of a set of keywords,
  and object is any expression. The available verbs are:\n\n
  \t:iterate - repeatedly evaluate and bind to the expression while it is truthy.\n
  \t:range - loop over a range. The object should be two element tuple with a start
  and end value, and an optional positive step. The range is half open, [start, end).\n
  \t:range-to - same as :range, but the range is inclusive [start, end].\n
  \t:down - loop over a range, stepping downwards. The object should be two element tuple
  with a start and (exclusive) end value, and an optional (positive!) step size.\n
  \t:down-to - same :as down, but the range is inclusive [start, end].\n
  \t:keys - iterate over the keys in a data structure.\n
  \t:pairs - iterate over the keys value pairs as tuples in a data structure.\n
  \t:in - iterate over the values in a data structure.\n
  \t:generate - iterate over values yielded from a fiber. Can be paired with the generator
  function for the producer/consumer pattern.\n\n
  loop also accepts conditionals to refine the looping further. Conditionals are of
  the form:\n\n
  \t:modifier argument\n\n
  where :modifier is one of a set of keywords, and argument is keyword dependent.
  :modifier can be one of:\n\n
  \t:while expression - breaks from the loop if expression is falsey.\n
  \t:until expression - breaks from the loop if expression is truthy.\n
  \t:let bindings - defines bindings inside the loop as passed to the let macro.\n
  \t:before form - evaluates a form for a side effect before of the next inner loop.\n
  \t:after form - same as :before, but the side effect happens after the next inner loop.\n
  \t:repeat n - repeats the next inner loop n times.\n
  \t:when condition - only evaluates the loop body when condition is true.\n\n
  The loop macro always evaluates to nil."
  [head & body]
  (loop1 body head 0))

(put _env 'loop1 nil)
(put _env 'check-indexed nil)
(put _env 'for-template nil)
(put _env 'for-var-template nil)
(put _env 'iterate-template nil)
(put _env 'each-template nil)
(put _env 'range-template nil)
(put _env 'loop-fiber-template nil)

(defmacro seq
  "Similar to loop, but accumulates the loop body into an array and returns that.
  See loop for details."
  [head & body]
  (def $accum (gensym))
  ~(do (def ,$accum @[]) (loop ,head (array/push ,$accum (do ,;body))) ,$accum))

(defmacro generate
  "Create a generator expression using the loop syntax. Returns a fiber
  that yields all values inside the loop in order. See loop for details."
  [head & body]
  ~(fiber/new (fn [] (loop ,head (yield (do ,;body)))) :yi))

(defmacro coro
  "A wrapper for making fibers. Same as (fiber/new (fn [] ;body) :yi)."
  [& body]
  (tuple fiber/new (tuple 'fn '[] ;body) :yi))

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
  "Make multiple bindings, and if all are truthy,
  evaluate the tru form. If any are false or nil, evaluate
  the fal form. Bindings have the same syntax as the let macro."
  [bindings tru &opt fal]
  (def len (length bindings))
  (if (zero? len) (error "expected at least 1 binding"))
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
  "Same as (if-let bindings (do ;body))."
  [bindings & body]
  ~(if-let ,bindings (do ,;body)))

(defn comp
  "Takes multiple functions and returns a function that is the composition
  of those functions."
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
  "A function that returns its first argument."
  [x]
  x)

(defn complement
  "Returns a function that is the complement to the argument."
  [f]
  (fn [x] (not (f x))))

(defn extreme
  "Returns the most extreme value in args based on the function order.
  order should take two values and return true or false (a comparison).
  Returns nil if args is empty."
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
  "Polymorphic compare. Returns -1, 0, 1 for x < y, x = y, x > y respectively.
   Differs from the primitive comparators in that it first checks to
   see whether either x or y implement a 'compare' method which can
   compare x and y. If so it uses that compare method. If not, it
   delegates to the primitive comparators."
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
  "Equivalent of '=' but using compare function instead of primitive comparator"
  [& xs]
  (compare-reduce = xs))

(defn compare<
  "Equivalent of '<' but using compare function instead of primitive comparator"
  [& xs]
  (compare-reduce < xs))

(defn compare<=
  "Equivalent of '<=' but using compare function instead of primitive comparator"
  [& xs]
  (compare-reduce <= xs))

(defn compare>
  "Equivalent of '>' but using compare function instead of primitive comparator"
  [& xs]
  (compare-reduce > xs))

(defn compare>=
  "Equivalent of '>=' but using compare function instead of primitive comparator"
  [& xs]
  (compare-reduce >= xs))

(put _env 'compare-reduce nil)

###
###
### Indexed Combinators
###
###

(defn- sort-part
  [a lo hi by]
  (def pivot (in a hi))
  (var i lo)
  (forv j lo hi
    (def aj (in a j))
    (when (by aj pivot)
      (def ai (in a i))
      (set (a i) aj)
      (set (a j) ai)
      (++ i)))
  (set (a hi) (in a i))
  (set (a i) pivot)
  i)

(defn- sort-help
  [a lo hi by]
  (when (> hi lo)
    (def piv (sort-part a lo hi by))
    (sort-help a lo (- piv 1) by)
    (sort-help a (+ piv 1) hi by))
  a)

(defn sort
  "Sort an array in-place. Uses quick-sort and is not a stable sort."
  [a &opt by]
  (sort-help a 0 (- (length a) 1) (or by <)))

(put _env 'sort-part nil)
(put _env 'sort-help nil)

(defn sort-by
  "Returns a new sorted array that compares elements by invoking
  a function on each element and comparing the result with <."
  [f ind]
  (sort ind (fn [x y] (< (f x) (f y)))))

(defn sorted
  "Returns a new sorted array without modifying the old one."
  [ind &opt by]
  (sort (array/slice ind) by))

(defn sorted-by
  "Returns a new sorted array that compares elements by invoking
  a function on each element and comparing the result with <."
  [f ind]
  (sorted ind (fn [x y] (< (f x) (f y)))))

(defn reduce
  "Reduce, also know as fold-left in many languages, transforms
  an indexed type (array, tuple) with a function to produce a value."
  [f init ind]
  (var res init)
  (each x ind (set res (f res x)))
  res)

(defn reduce2
  "The 2 argument version of reduce that does not take an initialization value.
  Instead the first element of the array is used for initialization."
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
  "Similar to reduce, but accumulates intermediate values into an array.
  The last element in the array is what would be the return value from reduce.
  The init value is not added to the array.
  Returns a new array."
  [f init ind]
  (var res init)
  (def ret (array/new (length ind)))
  (each x ind (array/push ret (set res (f res x))))
  ret)

(defn accumulate2
  "The 2 argument version of accumulate that does not take an initialization value."
  [f ind]
  (var k (next ind))
  (def ret (array/new (length ind)))
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
  "Map a function over every element in an indexed data structure and
  return an array of the results."
  [f & inds]
  (def ninds (length inds))
  (if (= 0 ninds) (error "expected at least 1 indexed collection"))
  (var limit (length (in inds 0)))
  (forv i 0 ninds
    (def l (length (in inds i)))
    (if (< l limit) (set limit l)))
  (def [i1 i2 i3 i4] inds)
  (def res (array/new limit))
  (case ninds
    1 (forv i 0 limit (set (res i) (f (in i1 i))))
    2 (forv i 0 limit (set (res i) (f (in i1 i) (in i2 i))))
    3 (forv i 0 limit (set (res i) (f (in i1 i) (in i2 i) (in i3 i))))
    4 (forv i 0 limit (set (res i) (f (in i1 i) (in i2 i) (in i3 i) (in i4 i))))
    (forv i 0 limit
      (def args (array/new ninds))
      (forv j 0 ninds (set (args j) (in (in inds j) i)))
      (set (res i) (f ;args))))
  res)

(defn mapcat
  "Map a function over every element in an array or tuple and
  use array to concatenate the results."
  [f ind]
  (def res @[])
  (each x ind
    (array/concat res (f x)))
  res)

(defn filter
  "Given a predicate, take only elements from an array or tuple for
  which (pred element) is truthy. Returns a new array."
  [pred ind]
  (def res @[])
  (each item ind
    (if (pred item)
      (array/push res item)))
  res)

(defn count
  "Count the number of items in ind for which (pred item)
  is true."
  [pred ind]
  (var counter 0)
  (each item ind
    (if (pred item)
      (++ counter)))
  counter)

(defn keep
  "Given a predicate, take only elements from an array or tuple for
  which (pred element) is truthy. Returns a new array of truthy predicate results."
  [pred ind]
  (def res @[])
  (each item ind
    (if-let [y (pred item)]
      (array/push res y)))
  res)

(defn range
  "Create an array of values [start, end) with a given step.
  With one argument returns a range [0, end). With two arguments, returns
  a range [start, end). With three, returns a range with optional step size."
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
  "Find the index of indexed type for which pred is true. Returns nil if not found."
  [pred ind]
  (def len (length ind))
  (var i 0)
  (var going true)
  (while (if (< i len) going)
    (def item (in ind i))
    (if (pred item) (set going false) (++ i)))
  (if going nil i))

(defn find
  "Find the first value in an indexed collection that satisfies a predicate. Returns
  nil if not found. Note there is no way to differentiate a nil from the indexed collection
  and a not found. Consider find-index if this is an issue."
  [pred ind]
  (def i (find-index pred ind))
  (if (= i nil) nil (in ind i)))

(defn take
  "Take first n elements in an indexed type. Returns new indexed instance."
  [n ind]
  (def use-str (bytes? ind))
  (def f (if use-str string/slice tuple/slice))
  (def len (length ind))
  # make sure end is in [0, len]
  (def m (if (> n 0) n 0))
  (def end (if (> m len) len m))
  (f ind 0 end))

(defn take-until
  "Same as (take-while (complement pred) ind)."
  [pred ind]
  (def use-str (bytes? ind))
  (def f (if use-str string/slice tuple/slice))
  (def len (length ind))
  (def i (find-index pred ind))
  (def end (if (nil? i) len i))
  (f ind 0 end))

(defn take-while
  "Given a predicate, take only elements from an indexed type that satisfy
  the predicate, and abort on first failure. Returns a new array."
  [pred ind]
  (take-until (complement pred) ind))

(defn drop
  "Drop first n elements in an indexed type. Returns new indexed instance."
  [n ind]
  (def use-str (bytes? ind))
  (def f (if use-str string/slice tuple/slice))
  (def len (length ind))
  # make sure start is in [0, len]
  (def m (if (> n 0) n 0))
  (def start (if (> m len) len m))
  (f ind start -1))

(defn drop-until
  "Same as (drop-while (complement pred) ind)."
  [pred ind]
  (def use-str (bytes? ind))
  (def f (if use-str string/slice tuple/slice))
  (def i (find-index pred ind))
  (def len (length ind))
  (def start (if (nil? i) len i))
  (f ind start))

(defn drop-while
  "Given a predicate, remove elements from an indexed type that satisfy
  the predicate, and abort on first failure. Returns a new array."
  [pred ind]
  (drop-until (complement pred) ind))

(defn juxt*
  "Returns the juxtaposition of functions. In other words,
  ((juxt* a b c) x) evaluates to [(a x) (b x) (c x)]."
  [& funs]
  (fn [& args]
    (def ret @[])
    (each f funs
      (array/push ret (f ;args)))
    (tuple/slice ret 0)))

(defmacro juxt
  "Macro form of juxt*. Same behavior but more efficient."
  [& funs]
  (def parts @['tuple])
  (def $args (gensym))
  (each f funs
    (array/push parts (tuple apply f $args)))
  (tuple 'fn (tuple '& $args) (tuple/slice parts 0)))

(defmacro tracev
  "Print a value and a description of the form that produced that value to
  stderr. Evaluates to x."
  [x]
  (def [l c] (tuple/sourcemap (dyn :macro-form ())))
  (def cf (dyn :current-file))
  (def fmt-1 (if cf (string/format "trace [%s]" cf) "trace"))
  (def fmt-2 (if (or (neg? l) (neg? c)) ":" (string/format " on line %d, column %d:" l c)))
  (def fmt (string fmt-1 fmt-2 " %j is "))
  (def s (gensym))
  ~(let [,s ,x]
     (,eprinf ,fmt ',x)
     (,eprintf (,dyn :pretty-format "%q") ,s)
     ,s))

(defmacro ->
  "Threading macro. Inserts x as the second value in the first form
  in forms, and inserts the modified first form into the second form
  in the same manner, and so on. Useful for expressing pipelines of data."
  [x & forms]
  (defn fop [last n]
    (def [h t] (if (= :tuple (type n))
                 (tuple (in n 0) (array/slice n 1))
                 (tuple n @[])))
    (def parts (array/concat @[h last] t))
    (tuple/slice parts 0))
  (reduce fop x forms))

(defmacro ->>
  "Threading macro. Inserts x as the last value in the first form
  in forms, and inserts the modified first form into the second form
  in the same manner, and so on. Useful for expressing pipelines of data."
  [x & forms]
  (defn fop [last n]
    (def [h t] (if (= :tuple (type n))
                 (tuple (in n 0) (array/slice n 1))
                 (tuple n @[])))
    (def parts (array/concat @[h] t @[last]))
    (tuple/slice parts 0))
  (reduce fop x forms))

(defmacro -?>
  "Short circuit threading macro. Inserts x as the second value in the first form
  in forms, and inserts the modified first form into the second form
  in the same manner, and so on. The pipeline will return nil
  if an intermediate value is nil.
  Useful for expressing pipelines of data."
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
  "Short circuit threading macro. Inserts x as the last value in the first form
  in forms, and inserts the modified first form into the second form
  in the same manner, and so on. The pipeline will return nil
  if an intermediate value is nil.
  Useful for expressing pipelines of data."
  [x & forms]
  (defn fop [last n]
    (def [h t] (if (= :tuple (type n))
                 (tuple (in n 0) (array/slice n 1))
                 (tuple n @[])))
    (def sym (gensym))
    (def parts (array/concat @[h] t @[sym]))
    ~(let [,sym ,last] (if ,sym ,(tuple/slice parts 0))))
  (reduce fop x forms))

(defn walk-ind [f form]
  (def len (length form))
  (def ret (array/new len))
  (each x form (array/push ret (f x)))
  ret)

(defn walk-dict [f form]
  (def ret @{})
  (loop [k :keys form]
    (put ret (f k) (f (in form k))))
  ret)

(defn walk
  "Iterate over the values in ast and apply f
  to them. Collect the results in a data structure. If ast is not a
  table, struct, array, or tuple,
  returns form."
  [f form]
  (case (type form)
    :table (walk-dict f form)
    :struct (table/to-struct (walk-dict f form))
    :array (walk-ind f form)
    :tuple (tuple/slice (walk-ind f form))
    form))

(put _env 'walk-ind nil)
(put _env 'walk-dict nil)

(defn postwalk
  "Do a post-order traversal of a data structure and call (f x)
  on every visitation."
  [f form]
  (f (walk (fn [x] (postwalk f x)) form)))

(defn prewalk
  "Similar to postwalk, but do pre-order traversal."
  [f form]
  (walk (fn [x] (prewalk f x)) (f form)))

(defmacro as->
  "Thread forms together, replacing as in forms with the value
  of the previous form. The first for is the value x. Returns the
  last value."
  [x as & forms]
  (var prev x)
  (each form forms
    (def sym (gensym))
    (def next-prev (postwalk (fn [y] (if (= y as) sym y)) form))
    (set prev ~(let [,sym ,prev] ,next-prev)))
  prev)

(defmacro as?->
  "Thread forms together, replacing as in forms with the value
  of the previous form. The first for is the value x. If any
  intermediate values are falsey, return nil; otherwise, returns the
  last value."
  [x as & forms]
  (var prev x)
  (each form forms
    (def sym (gensym))
    (def next-prev (postwalk (fn [y] (if (= y as) sym y)) form))
    (set prev ~(if-let [,sym ,prev] ,next-prev)))
  prev)

(defmacro with-dyns
  "Run a block of code in a new fiber that has some
  dynamic bindings set. The fiber will not mask errors
  or signals, but the dynamic bindings will be properly
  unset, as dynamic bindings are fiber local."
  [bindings & body]
  (def dyn-forms
    (seq [i :range [0 (length bindings) 2]]
      ~(setdyn ,(bindings i) ,(bindings (+ i 1)))))
  ~(,resume (,fiber/new (fn [] ,;dyn-forms ,;body) :p)))

(defmacro with-vars
  "Evaluates body with each var in vars temporarily bound. Similar signature to
  let, but each binding must be a var."
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
  "Returns true if each value in is truthy, otherwise the first
  falsey value."
  [ind]
  (var res true)
  (loop [x :in ind :while res]
    (if x nil (set res x)))
  res)

(defn any?
  "Returns the first truthy valye in ind, otherwise nil.
  falsey value."
  [ind]
  (var res nil)
  (loop [x :in ind :until res]
    (if x (set res x)))
  res)

(defn reverse!
  "Reverses the order of the elements in a given array or buffer and returns it
  mutated."
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
  "Reverses the order of the elements in a given array or tuple and returns
  a new array. If string or buffer is provided function returns array of chars reversed."
  [t]
  (def len (length t))
  (var n (- len 1))
  (def ret (array/new len))
  (while (>= n 0)
    (array/push ret (in t n))
    (-- n))
  ret)

(defn invert
  "Returns a table where the keys of an associative data structure
  are the values, and the values of the keys. If multiple keys have the same
  value, one key will be ignored."
  [ds]
  (def ret @{})
  (loop [k :keys ds]
    (put ret (in ds k) k))
  ret)

(defn zipcoll
  "Creates a table from two arrays/tuples.
  Returns a new table."
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
  "Access a value in a nested data structure. Looks into the data structure via
  a sequence of keys."
  [ds ks &opt dflt]
  (var d ds)
  (loop [k :in ks :while d] (set d (get d k)))
  (if (= nil d) dflt d))

(defn update-in
  "Update a value in a nested data structure by applying f to the current value.
  Looks into the data structure via
  a sequence of keys. Missing data structures will be replaced with tables. Returns
  the modified, original data structure."
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
  "Put a value into a nested data structure.
  Looks into the data structure via
  a sequence of keys. Missing data structures will be replaced with tables. Returns
  the modified, original data structure."
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
  "Accepts a key argument and passes its associated value to a function.
  The key is the re-associated to the function's return value. Returns the updated
  data structure ds."
  [ds key func & args]
  (def old (get ds key))
  (put ds key (func old ;args)))

(defn merge-into
  "Merges multiple tables/structs into a table. If a key appears in more than one
  collection, then later values replace any previous ones.
  Returns the original table."
  [tab & colls]
  (loop [c :in colls
         key :keys c]
    (set (tab key) (in c key)))
  tab)

(defn merge
  "Merges multiple tables/structs to one. If a key appears in more than one
  collection, then later values replace any previous ones.
  Returns a new table."
  [& colls]
  (def container @{})
  (loop [c :in colls
         key :keys c]
    (set (container key) (in c key)))
  container)

(defn keys
  "Get the keys of an associative data structure."
  [x]
  (def arr (array/new (length x)))
  (var k (next x nil))
  (while (not= nil k)
    (array/push arr k)
    (set k (next x k)))
  arr)

(defn values
  "Get the values of an associative data structure."
  [x]
  (def arr (array/new (length x)))
  (var k (next x nil))
  (while (not= nil k)
    (array/push arr (in x k))
    (set k (next x k)))
  arr)

(defn pairs
  "Get the values of an associative data structure."
  [x]
  (def arr (array/new (length x)))
  (var k (next x nil))
  (while (not= nil k)
    (array/push arr (tuple k (in x k)))
    (set k (next x k)))
  arr)

(defn frequencies
  "Get the number of occurrences of each value in a indexed structure."
  [ind]
  (def freqs @{})
  (each x ind
    (def n (in freqs x))
    (set (freqs x) (if n (+ 1 n) 1)))
  freqs)

(defn interleave
  "Returns an array of the first elements of each col,
  then the second, etc."
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
  "Returns an array of the deduplicated values in xs."
  [xs]
  (def ret @[])
  (def seen @{})
  (each x xs (if (in seen x) nil (do (put seen x true) (array/push ret x))))
  ret)

(defn flatten-into
  "Takes a nested array (tree), and appends the depth first traversal of
  that array to an array 'into'. Returns array into."
  [into xs]
  (each x xs
    (if (indexed? x)
      (flatten-into into x)
      (array/push into x)))
  into)

(defn flatten
  "Takes a nested array (tree), and returns the depth first traversal of
  that array. Returns a new array."
  [xs]
  (flatten-into @[] xs))

(defn kvs
  "Takes a table or struct and returns and array of key value pairs
  like @[k v k v ...]. Returns a new array."
  [dict]
  (def ret (array/new (* 2 (length dict))))
  (loop [k :keys dict] (array/push ret k (in dict k)))
  ret)

(defn interpose
  "Returns a sequence of the elements of ind separated by
  sep. Returns a new array."
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
  "Partition an indexed data structure into tuples
  of size n. Returns a new array."
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
  "Read all data from a file with name path
  and then close the file."
  [path]
  (def f (file/open path :rb))
  (if-not f (error (string "could not open file " path)))
  (def contents (file/read f :all))
  (file/close f)
  contents)

(defn spit
  "Write contents to a file at path.
  Can optionally append to the file."
  [path contents &opt mode]
  (default mode :wb)
  (def f (file/open path mode))
  (if-not f (error (string "could not open file " path " with mode " mode)))
  (file/write f contents)
  (file/close f)
  nil)

(defn pp
  `Pretty print to stdout or (dyn :out). The format string used is (dyn :pretty-format "%q").`
  [x]
  (printf (dyn :pretty-format "%q") x)
  (flush))

###
###
### Pattern Matching
###
###

(defmacro- with-idemp
  "Return janet code body that has been prepended
  with a binding of form to atom. If form is a non-idempotent
  form (a function call, etc.), make sure the resulting
  code will only evaluate once, even if body contains multiple
  copies of binding. In body, use binding instead of form."
  [binding form & body]
  (def $result (gensym))
  (def $form (gensym))
  ~(do
     (def ,$form ,form)
     (def ,binding (if (idempotent? ,$form) ,$form (gensym)))
     (def ,$result (do ,;body))
     (if (= ,$form ,binding)
       ,$result
       (tuple 'do (tuple 'def ,binding ,$form) ,$result))))


# Sentinel value for mismatches
(def- sentinel ~',(gensym))

(defn- match-1
  [pattern expr onmatch seen]
  (cond

    (= '_ pattern)
    (onmatch)

    (symbol? pattern)
    (if (in seen pattern)
      ~(if (= ,pattern ,expr) ,(onmatch) ,sentinel)
      (do
        (put seen pattern true)
        ~(do (def ,pattern ,expr) ,(onmatch))))

    (and (tuple? pattern) (= :parens (tuple/type pattern)))
    (if (= (get pattern 0) '@)
      # Unification with external values
      ~(if (= ,(get pattern 1) ,expr) ,(onmatch) ,sentinel)
      (match-1
        (in pattern 0) expr
        (fn []
          ~(if (and ,;(tuple/slice pattern 1)) ,(onmatch) ,sentinel)) seen))

    (indexed? pattern)
    (do
      (def len (length pattern))
      (var i -1)
      (with-idemp
        $arr expr
        ~(if (,indexed? ,$arr)
           (if (< (,length ,$arr) ,len)
             ,sentinel
             ,((fn aux []
                 (++ i)
                 (if (= i len)
                   (onmatch)
                   (match-1 (in pattern i) (tuple in $arr i) aux seen)))))
           ,sentinel)))

    (dictionary? pattern)
    (do
      (var key nil)
      (with-idemp
        $dict expr
        ~(if (,dictionary? ,$dict)
           ,((fn aux []
               (set key (next pattern key))
               (def $val (gensym))
               (if (= key nil)
                 (onmatch)
                 ~(do (def ,$val (,get ,$dict ,key))
                    ,(match-1 [(in pattern key) [not= nil $val]] $val aux seen)))))
           ,sentinel)))

    :else ~(if (= ,pattern ,expr) ,(onmatch) ,sentinel)))

(defmacro match
  "Pattern matching. Match an expression x against
  any number of cases. Each case is a pattern to match against, followed
  by an expression to evaluate to if that case is matched. A pattern that is
  a symbol will match anything, binding x's value to that symbol. An array
  will match only if all of it's elements match the corresponding elements in
  x. A table or struct will match if all values match with the corresponding
  values in x. A tuple pattern will match if it's first element matches, and the following
  elements are treated as predicates and are true. The last special case is
  the '_ symbol, which is a wildcard that will match any value without creating a binding.
  Any other value pattern will only match if it is equal to x."
  [x & cases]
  (with-idemp $x x
    (def len (length cases))
    (def len-1 (dec len))
    ((fn aux [i]
       (cond
         (= i len-1) (in cases i)
         (< i len-1) (with-syms [$res]
                       ~(if (= ,sentinel (def ,$res ,(match-1 (in cases i) $x (fn [] (in cases (inc i))) @{})))
                          ,(aux (+ 2 i))
                          ,$res)))) 0)))

(put _env 'sentinel nil)
(put _env 'match-1 nil)
(put _env 'with-idemp nil)

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
  "Get all symbols available in an environment. Defaults to the current
  fiber's environment. If local is truthy, will not show inherited bindings
  (from prototype tables)."
  [&opt env local]
  (env-walk symbol? env local))

(defn all-dynamics
  "Get all dynamic bindings in an environment. Defaults to the current
  fiber's environment. If local is truthy, will not show inherited bindings
  (from prototype tables)."
  [&opt env local]
  (env-walk keyword? env local))

(defn doc-format
  "Reformat text to wrap at a given line."
  [text &opt width]

  (def maxcol (- (or width (dyn :doc-width 80)) 8))
  (var buf @"    ")
  (var word @"")
  (var current 0)

  (defn pushword
    []
    (def oldcur current)
    (def spacer
      (if (<= maxcol (+ current (length word) 1))
        (do (set current 0) "\n    ")
        (do (++ current) " ")))
    (+= current (length word))
    (if (> oldcur 0)
      (buffer/push-string buf spacer))
    (buffer/push-string buf word)
    (buffer/clear word))

  (each b text
    (if (and (not= b 10) (not= b 32))
      (if (= b 9)
        (buffer/push-string word "  ")
        (buffer/push-byte word b))
      (do
        (if (> (length word) 0) (pushword))
        (when (= b 10)
          (buffer/push-string buf "\n    ")
          (set current 0)))))

  # Last word
  (pushword)

  buf)

(defn- print-index
  "Print bindings in the current environment given a filter function"
  [fltr]
  (def bindings (filter fltr (all-bindings)))
  (def dynamics (map describe (filter fltr (all-dynamics))))
  (print)
  (print (doc-format (string "Bindings:\n\n" (string/join bindings " "))))
  (print)
  (print (doc-format (string "Dynamics:\n\n" (string/join dynamics " "))))
  (print))

(defn doc*
  "Get the documentation for a symbol in a given environment. Function form of doc."
  [&opt sym]

  (cond
    (string? sym)
    (print-index (fn [x] (string/find sym x)))

    sym
    (do
      (def x (dyn sym))
      (if (not x)
        (print "symbol " sym " not found.")
        (do
          (def bind-type
            (string "    "
                    (cond
                      (x :ref) (string :var " (" (type (in (x :ref) 0)) ")")
                      (x :macro) :macro
                      (type (x :value)))
                    "\n"))
          (def sm (x :source-map))
          (def d (x :doc))
          (print "\n\n"
                 (if d bind-type "")
                 (if-let [[path line col] sm]
                   (string "    " path " on line " line ", column " col "\n") "")
                 (if (or d sm) "\n" "")
                 (if d (doc-format d) "    no documentation found.")
                 "\n\n"))))

    # else
    (print-index identity)))

(defmacro doc
  "Shows documentation for the given symbol, or can show a list of available bindings.
  If sym is a symbol, will look for documentation for that symbol. If sym is a string
  or is not provided, will show all lexical and dynamic bindings in the current environment with
  that prefix (all bindings will be shown if no prefix is given)."
  [&opt sym]
  ~(,doc* ',sym))

(put _env 'env-walk nil)
(put _env 'print-index nil)

###
###
### Macro Expansion
###
###

(defn macex1
  "Expand macros in a form, but do not recursively expand macros.
  See macex docs for info on on-binding."
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
        :tuple (do
                 (def x0 (in x 0))
                 (if (or (= 'unquote x0) (= 'unquote-splicing x0))
                   (tuple x0 (recur (in x 1)))
                   (tuple/slice (map qq x))))
        :array (map qq x)
        :table (table (map qq (kvs x)))
        :struct (struct (map qq (kvs x)))
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
     'break expandall})

  (defn dotup [t]
    (def h (in t 0))
    (def s (in specs h))
    (def entry (or (dyn h) {}))
    (def m (entry :value))
    (def m? (entry :macro))
    (cond
      s (s t)
      m? (do (setdyn :macro-form t) (m ;(tuple/slice t 1)))
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
  "Returns true if all xs are truthy, otherwise the result of first
  falsey predicate value, (pred x)."
  [pred xs]
  (var ret true)
  (loop [x :in xs :while ret] (set ret (pred x)))
  ret)

(defn some
  "Returns nil if all xs are false or nil, otherwise returns the result of the
  first truthy predicate, (pred x)."
  [pred xs]
  (var ret nil)
  (loop [x :in xs :while (not ret)] (if-let [y (pred x)] (set ret y)))
  ret)

(defn deep-not=
  "Like not=, but mutable types (arrays, tables, buffers) are considered
  equal if they have identical structure. Much slower than not=."
  [x y]
  (def tx (type x))
  (or
    (not= tx (type y))
    (case tx
      :tuple (or (not= (length x) (length y)) (some identity (map deep-not= x y)))
      :array (or (not= (length x) (length y)) (some identity (map deep-not= x y)))
      :struct (deep-not= (pairs x) (pairs y))
      :table (deep-not= (table/to-struct x) (table/to-struct y))
      :buffer (not= (string x) (string y))
      (not= x y))))

(defn deep=
  "Like =, but mutable types (arrays, tables, buffers) are considered
  equal if they have identical structure. Much slower than =."
  [x y]
  (not (deep-not= x y)))

(defn freeze
  "Freeze an object (make it immutable) and do a deep copy, making
  child values also immutable. Closures, fibers, and abstract types
  will not be recursively frozen, but all other types will."
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
  "Expand macros completely.
  on-binding is an optional callback whenever a normal symbolic binding
  is encounter. This allows macros to easily see all bindings use by their
  arguments by calling macex on their contents. The binding itself is also
  replaced by the value returned by on-binding within the expand macro."
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
  "Create a function that can be rebound. varfn has the same signature
  as defn, but defines functions in the environment as vars. If a var 'name'
  already exists in the environment, it is rebound to the new function. Returns
  a function."
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
  "fn shorthand.\n\n
  usage:\n\n
  \t(short-fn (+ $ $)) - A function that double's its arguments.\n
  \t(short-fn (string $0 $1)) - accepting multiple args\n
  \t|(+ $ $) - use pipe reader macro for terse function literals\n
  \t|(+ $&) - variadic functions"
  [arg]
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
  (def fn-args (seq [i :range [0 (+ 1 max-param-seen)]] (symbol '$ i)))
  ~(fn [,;fn-args ,;(if vararg ['& '$&] [])] ,expanded))

###
###
### Default PEG patterns
###
###

(def default-peg-grammar
  "The default grammar used for pegs. This grammar defines several common patterns
  that should make it easier to write more complex patterns."
  ~@{:d (range "09")
     :a (range "az" "AZ")
     :s (set " \t\r\n\0\f\v")
     :w (range "az" "AZ" "09")
     :h (range "09" "af")
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

###
###
### Evaluation and Compilation
###
###

# Get boot options
(def- boot/opts @{})
(each [k v] (partition 2 (tuple/slice boot/args 2))
  (put boot/opts k v))

(defn make-env
  "Create a new environment table. The new environment
  will inherit bindings from the parent environment, but new
  bindings will not pollute the parent environment."
  [&opt parent]
  (def parent (if parent parent _env))
  (def newenv (table/setproto @{} parent))
  newenv)

(defn bad-parse
  "Default handler for a parse error."
  [p where]
  (def ec (dyn :err-color))
  (def [line col] (parser/where p))
  (eprint
    (if ec "\e[31m" "")
    "parse error in "
    where
    " around line "
    (string line)
    ", column "
    (string col)
    ": "
    (parser/error p)
    (if ec "\e[0m" ""))
  (eflush))

(defn bad-compile
  "Default handler for a compile error."
  [msg macrof where]
  (def ec (dyn :err-color))
  (if macrof
    (debug/stacktrace macrof (string msg " while compiling " where))
    (eprint
      (if ec "\e[31m" "")
      "compile error: "
      msg
      " while compiling "
      where
      (if ec "\e[0m" "")))
  (eflush))

(defn run-context
  "Run a context. This evaluates expressions in an environment,
  and is encapsulates the parsing, compilation, and evaluation.
  Returns (in environment :exit-value environment) when complete.
  opts is a table or struct of options. The options are as follows:\n\n\t
  :chunks - callback to read into a buffer - default is getline\n\t
  :on-parse-error - callback when parsing fails - default is bad-parse\n\t
  :env - the environment to compile against - default is the current env\n\t
  :source - string path of source for better errors - default is \"<anonymous>\"\n\t
  :on-compile-error - callback when compilation fails - default is bad-compile\n\t
  :evaluator - callback that executes thunks. Signature is (evaluator thunk source env where)\n\t
  :on-status - callback when a value is evaluated - default is debug/stacktrace\n\t
  :fiber-flags - what flags to wrap the compilation fiber with. Default is :ia.\n\t
  :expander - an optional function that is called on each top level form before being compiled."
  [opts]

  (def {:env env
        :chunks chunks
        :on-status onstatus
        :on-compile-error on-compile-error
        :on-parse-error on-parse-error
        :fiber-flags guard
        :evaluator evaluator
        :source where
        :expander expand} opts)
  (default env (fiber/getenv (fiber/current)))
  (default chunks (fn [buf p] (getline "" buf env)))
  (default onstatus debug/stacktrace)
  (default on-compile-error bad-compile)
  (default on-parse-error bad-parse)
  (default evaluator (fn evaluate [x &] (x)))
  (default where "<anonymous>")
  (default guard :ydt)

  # Are we done yet?
  (var going true)

  # The parser object
  (def p (parser/new))

  # Evaluate 1 source form in a protected manner
  (defn eval1 [source]
    (def source (if expand (expand source) source))
    (var good true)
    (var resumeval nil)
    (def f
      (fiber/new
        (fn []
          (def res (compile source env where))
          (if (= (type res) :function)
            (evaluator res source env where)
            (do
              (set good false)
              (def {:error err :line line :column column :fiber errf} res)
              (def msg
                (if (<= 0 line)
                  (string err " on line " line ", column " column)
                  err))
              (on-compile-error msg errf where))))
        guard))
    (fiber/setenv f env)
    (while (fiber/can-resume? f)
      (def res (resume f resumeval))
      (when good (when going (set resumeval (onstatus f res))))))

  (defn parse-err
    "Handle parser error in the correct environment"
    [p where]
    (def f (coro (on-parse-error p where)))
    (fiber/setenv f env)
    (resume f))

  # Loop
  (def buf @"")
  (while going
    (if (env :exit) (break))
    (buffer/clear buf)
    (if (= (chunks buf p)
           :cancel)
      (do
        # A :cancel chunk represents a cancelled form in the REPL, so reset.
        (parser/flush p)
        (buffer/clear buf))
      (do
        (var pindex 0)
        (var pstatus nil)
        (def len (length buf))
        (when (= len 0)
          (parser/eof p)
          (set going false))
        (while (> len pindex)
          (+= pindex (parser/consume p buf pindex))
          (while (parser/has-more p)
            (eval1 (parser/produce p)))
          (when (= (parser/status p) :error)
            (parse-err p where))))))

  # Check final parser state
  (while (parser/has-more p)
    (eval1 (parser/produce p)))
  (when (= (parser/status p) :error)
    (parse-err p where))

  (in env :exit-value env))

(defn quit
  "Tries to exit from the current repl or context. Does not always exit the application.
  Works by setting the :exit dynamic binding to true. Passing a non-nil value here will cause the outer
  run-context to return that value."
  [&opt value]
  (setdyn :exit true)
  (setdyn :exit-value value)
  nil)

(defn eval-string
  "Evaluates a string in the current environment. If more control over the
  environment is needed, use run-context."
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
                                  (error (string "parse error: " (parser/error p))))
                :fiber-flags :i
                :on-status (fn on-status [f val]
                             (if-not (= (fiber/status f) :dead)
                               (error val))
                             (set returnval val))
                :source "eval-string"})
  returnval)

(defn eval
  "Evaluates a form in the current environment. If more control over the
  environment is needed, use run-context."
  [form]
  (def res (compile form (fiber/getenv (fiber/current)) "eval"))
  (if (= (type res) :function)
    (res)
    (error (res :error))))

(defn parse
  "Parse a string and return the first value. For complex parsing, such as for a repl with error handling,
  use the parser api."
  [str]
  (let [p (parser/new)]
    (parser/consume p str)
    (parser/eof p)
    (if (parser/has-more p)
      (parser/produce p)
      (if (= :error (parser/status p))
        (error (parser/error p))
        (error "no value")))))

(def make-image-dict
  "A table used in combination with marshal to marshal code (images), such that
  (make-image x) is the same as (marshal x make-image-dict)."
  @{})

(def load-image-dict
  "A table used in combination with unmarshal to unmarshal byte sequences created
  by make-image, such that (load-image bytes) is the same as (unmarshal bytes load-image-dict)."
  @{})

(def comptime
  "(comptime x)\n\n
  Evals x at compile time and returns the result. Similar to a top level unquote."
  :macro eval)

(defn make-image
  "Create an image from an environment returned by require.
  Returns the image source as a string."
  [env]
  (marshal env make-image-dict))

(defn load-image
  "The inverse operation to make-image. Returns an environment."
  [image]
  (unmarshal image load-image-dict))

(defn- check-. [x] (if (string/has-prefix? "." x) x))
(defn- not-check-. [x] (unless (string/has-prefix? "." x) x))

(def module/paths
  "The list of paths to look for modules, templated for module/expand-path.
  Each element is a two element tuple, containing the path
  template and a keyword :source, :native, or :image indicating how
  require should load files found at these paths.\n\nA tuple can also
  contain a third element, specifying a filter that prevents module/find
  from searching that path template if the filter doesn't match the input
  path. The filter can be a string or a predicate function, and
  is often a file extension, including the period."
  @[])

(setdyn :syspath (boot/opts "JANET_PATH"))
(setdyn :headerpath (boot/opts "JANET_HEADERPATH"))

(defn module/add-paths
  "Add paths to module/paths for a given loader such that
  the generated paths behave like other module types, including
  relative imports and syspath imports. ext is the file extension
  to associate with this module type, including the dot. loader is the
  keyword name of a loader that is module/loaders. Returns the modified module/paths."
  [ext loader]
  (defn- find-prefix
    [pre]
    (or (find-index |(and (string? ($ 0)) (string/has-prefix? pre ($ 0))) module/paths) 0))
  (def all-index (find-prefix ":all:"))
  (array/insert module/paths all-index [(string ":all:" ext) loader not-check-.])
  (def sys-index (find-prefix ":sys:"))
  (array/insert module/paths sys-index [(string ":sys:/:all:" ext) loader not-check-.])
  (def curall-index (find-prefix ":cur:/:all:"))
  (array/insert module/paths curall-index [(string ":cur:/:all:" ext) loader check-.])
  module/paths)

(module/add-paths ":native:" :native)
(module/add-paths "/init.janet" :source)
(module/add-paths ".janet" :source)
(module/add-paths ".jimage" :image)

# Version of fexists that works even with a reduced OS
(if-let [has-stat (_env 'os/stat)]
  (let [stat (has-stat :value)]
    (defglobal "fexists" (fn fexists [path] (= :file (stat path :mode)))))
  (defglobal "fexists"
    (fn fexists [path]
      (def f (file/open path :rb))
      (when f
        (def res
          (try (do (file/read f 1) true)
            ([err] nil)))
        (file/close f)
        res))))

(defn- mod-filter
  [x path]
  (case (type x)
    :nil path
    :string (string/has-suffix? x path)
    (x path)))

(defn module/find
  "Try to match a module or path name from the patterns in module/paths.
  Returns a tuple (fullpath kind) where the kind is one of :source, :native,
  or image if the module is found, otherwise a tuple with nil followed by
  an error message."
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

(put _env 'fexists nil)
(put _env 'mod-filter nil)
(put _env 'check-. nil)
(put _env 'not-check-. nil)

(def module/cache
  "Table mapping loaded module identifiers to their environments."
  @{})

(def module/loading
  "Table mapping currently loading modules to true. Used to prevent
  circular dependencies."
  @{})

(defn dofile
  "Evaluate a file and return the resulting environment. :env, :expander, and
  :evaluator are passed through to the underlying run-context call.
  If exit is true, any top level errors will trigger a call to (os/exit 1)
  after printing the error."
  [path &keys
   {:exit exit
    :env env
    :source src
    :expander expander
    :evaluator evaluator}]
  (def f (if (= (type path) :core/file)
           path
           (file/open path :rb)))
  (def path-is-file (= f path))
  (default env (make-env))
  (def spath (string path))
  (put env :current-file (or src (if-not path-is-file spath)))
  (put env :source (or src (if-not path-is-file spath path)))
  (defn chunks [buf _] (file/read f 2048 buf))
  (defn bp [&opt x y]
    (def ret (bad-parse x y))
    (if exit (os/exit 1))
    ret)
  (defn bc [&opt x y z]
    (def ret (bad-compile x y z))
    (if exit (os/exit 1))
    ret)
  (unless f
    (error (string "could not find file " path)))
  (def nenv
    (run-context {:env env
                  :chunks chunks
                  :on-parse-error bp
                  :on-compile-error bc
                  :on-status (fn [f x]
                               (when (not= (fiber/status f) :dead)
                                 (debug/stacktrace f x)
                                 (if exit (os/exit 1) (eflush))))
                  :evaluator evaluator
                  :expander expander
                  :source (or src (if path-is-file "<anonymous>" spath))}))
  (if-not path-is-file (file/close f))
  nenv)

(def module/loaders
  "A table of loading method names to loading functions.
  This table lets require and import load many different kinds
  of files as module."
  @{:native (fn [path &] (native path (make-env)))
    :source (fn [path args]
              (put module/loading path true)
              (def newenv (dofile path ;args))
              (put module/loading path nil)
              newenv)
    :image (fn [path &] (load-image (slurp path)))})

(defn require-1
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
  "Require a module with the given name. Will search all of the paths in
  module/paths. Returns the new environment
  returned from compiling and running the file."
  [path & args]
  (require-1 path args (struct ;args)))

(defn import*
  "Function form of import. Same parameters, but the path
  and other symbol parameters should be strings instead."
  [path & args]
  (def env (fiber/getenv (fiber/current)))
  (def kargs (table ;args))
  (def {:as as
        :prefix prefix
        :export ep} kargs)
  (def newenv (require-1 path args kargs))
  (def prefix (or
                (and as (string as "/"))
                prefix
                (string (last (string/split "/" path)) "/")))
  (loop [[k v] :pairs newenv :when (symbol? k) :when (not (v :private))]
    (def newv (table/setproto @{:private (not ep)} v))
    (put env (symbol prefix k) newv)))

(put _env 'require-1 nil)

(defmacro import
  "Import a module. First requires the module, and then merges its
  symbols into the current environment, prepending a given prefix as needed.
  (use the :as or :prefix option to set a prefix). If no prefix is provided,
  use the name of the module as a prefix. One can also use :export true
  to re-export the imported symbols. If :exit true is given as an argument,
  any errors encountered at the top level in the module will cause (os/exit 1)
  to be called. Dynamic bindings will NOT be imported. Use :fresh to bypass the
  module cache."
  [path & args]
  (def argm (map |(if (keyword? $) $ (string $)) args))
  (tuple import* (string path) ;argm))

(defmacro use
  "Similar to import, but imported bindings are not prefixed with a namespace
  identifier. Can also import multiple modules in one shot."
  [& modules]
  ~(do ,;(map |~(,import* ,(string $) :prefix "") modules)))

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
  "Print the current fiber stack"
  []
  (print)
  (with-dyns [:err-color false] (debug/stacktrace (.fiber) (.signal)))
  (print))

(defn .frame
  "Show a stack frame"
  [&opt n]
  (def stack (debug/stack (.fiber)))
  (in stack (or n 0)))

(defn .fn
  "Get the current function"
  [&opt n]
  (in (.frame n) :function))

(defn .slots
  "Get an array of slots in a stack frame"
  [&opt n]
  (in (.frame n) :slots))

(defn .slot
  "Get the value of the nth slot."
  [&opt nth frame-idx]
  (in (.slots frame-idx) (or nth 0)))

# Conditional compilation for disasm
(def disasm-alias (if-let [x (_env 'disasm)] (x :value)))

(defn .disasm
  "Gets the assembly for the current function."
  [&opt n]
  (def frame (.frame n))
  (def func (frame :function))
  (disasm-alias func))

(defn .bytecode
  "Get the bytecode for the current function."
  [&opt n]
  ((.disasm n) :bytecode))

(defn .ppasm
  "Pretty prints the assembly for the current function"
  [&opt n]
  (def frame (.frame n))
  (def func (frame :function))
  (def dasm (disasm-alias func))
  (def bytecode (in dasm :bytecode))
  (def pc (frame :pc))
  (def sourcemap (in dasm :sourcemap))
  (var last-loc [-2 -2])
  (print "\n  signal: " (.signal))
  (print "  function:   " (dasm :name) " [" (in dasm :source "") "]")
  (when-let [constants (dasm :constants)]
    (printf "  constants:  %.4q" constants))
  (printf "  slots:      %.4q\n" (frame :slots))
  (def padding (string/repeat " " 20))
  (loop [i :range [0 (length bytecode)]
         :let [instr (bytecode i)]]
    (prin (if (= (tuple/type instr) :brackets) "*" " "))
    (prin (if (= i pc) "> " "  "))
    (prinf "%.20s" (string (string/join (map string instr) " ") padding))
    (when sourcemap
      (let [[sl sc] (sourcemap i)
            loc [sl sc]]
        (when (not= loc last-loc)
          (set last-loc loc)
          (prin " # line " sl ", column " sc))))
    (print))
  (print))

(defn .breakall
  "Set breakpoints on all instructions in the current function."
  [&opt n]
  (def fun (.fn n))
  (def bytecode (.bytecode n))
  (forv i 0 (length bytecode)
    (debug/fbreak fun i))
  (print "Set " (length bytecode) " breakpoints in " fun))

(defn .clearall
  "Clear all breakpoints on the current function."
  [&opt n]
  (def fun (.fn n))
  (def bytecode (.bytecode n))
  (forv i 0 (length bytecode)
    (debug/unfbreak fun i))
  (print "Cleared " (length bytecode) " breakpoints in " fun))

(unless (get _env 'disasm)
  (put _env '.disasm nil)
  (put _env '.bytecode nil)
  (put _env '.breakall nil)
  (put _env '.clearall nil)
  (put _env '.ppasm nil))
(put _env 'disasm-alias nil)

(defn .source
  "Show the source code for the function being debugged."
  [&opt n]
  (def frame (.frame n))
  (def s (frame :source))
  (def all-source (slurp s))
  (print "\n" all-source "\n"))

(defn .break
  "Set breakpoint at the current pc."
  []
  (def frame (.frame))
  (def fun (frame :function))
  (def pc (frame :pc))
  (debug/fbreak fun pc)
  (print "Set breakpoint in " fun " at pc=" pc))

(defn .clear
  "Clear the current breakpoint"
  []
  (def frame (.frame))
  (def fun (frame :function))
  (def pc (frame :pc))
  (debug/unfbreak fun pc)
  (print "Cleared breakpoint in " fun " at pc=" pc))

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

(def debugger-env
  "An environment that contains dot prefixed functions for debugging."
  @{})

(def- debugger-keys (filter (partial string/has-prefix? ".") (keys _env)))
(each k debugger-keys (put debugger-env k (_env k)) (put _env k nil))
(put _env 'debugger-keys nil)

###
###
### REPL
###
###

(defn repl
  "Run a repl. The first parameter is an optional function to call to
  get a chunk of source code that should return nil for end of file.
  The second parameter is a function that is called when a signal is
  caught. One can provide an optional environment table to run
  the repl in."
  [&opt chunks onsignal env]
  (default env (make-env))
  (default chunks
    (fn [buf p]
      (getline
        (string
          "repl:"
          ((parser/where p) 0)
          ":"
          (parser/state p :delimiters) "> ")
        buf env)))
  (defn make-onsignal
    [e level]

    (defn enter-debugger
      [f x]
      (def nextenv (make-env env))
      (put nextenv :fiber f)
      (put nextenv :debug-level level)
      (put nextenv :signal x)
      (merge-into nextenv debugger-env)
      (debug/stacktrace f x)
      (eflush)
      (defn debugger-chunks [buf p]
        (def status (parser/state p :delimiters))
        (def c ((parser/where p) 0))
        (def prpt (string "debug[" level "]:" c ":" status "> "))
        (getline prpt buf nextenv))
      (print "entering debug[" level "] - (quit) to exit")
      (flush)
      (repl debugger-chunks (make-onsignal nextenv (+ 1 level)) nextenv)
      (print "exiting debug[" level "]")
      (flush)
      (nextenv :resume-value))

    (fn [f x]
      (if (= :dead (fiber/status f))
        (do
          (put e '_ @{:value x})
          (printf (get e :pretty-format "%q") x)
          (flush))
        (if (e :debug)
          (enter-debugger f x)
          (do (debug/stacktrace f x) (eflush))))))

  (run-context {:env env
                :chunks chunks
                :on-status (or onsignal (make-onsignal env 1))
                :source "repl"}))

###
###
### CLI Tool Main
###
###

(defn- no-side-effects
  "Check if form may have side effects. If returns true, then the src
  must not have side effects, such as calling a C function."
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

(def- safe-forms {'defn true 'defn- true 'defmacro true 'defmacro- true
                  'def is-safe-def 'var is-safe-def 'def- is-safe-def 'var- is-safe-def
                  'defglobal is-safe-def 'varglobal is-safe-def})

(def- importers {'import true 'import* true 'dofile true 'require true})
(defn- use-2 [evaluator args]
  (each a args (import* (string a) :prefix "" :evaluator evaluator)))

# conditional compilation for reduced os
(def- getenv-alias (if-let [entry (in _env 'os/getenv)] (entry :value) (fn [&])))

(defn cli-main
  "Entrance for the Janet CLI tool. Call this functions with the command line
  arguments as an array or tuple of strings to invoke the CLI interface."
  [args]

  (setdyn :args args)

  (var *should-repl* false)
  (var *no-file* true)
  (var *quiet* false)
  (var *raw-stdin* false)
  (var *handleopts* true)
  (var *exit-on-error* true)
  (var *colorize* true)
  (var *debug* false)
  (var *compile-only* false)

  (if-let [jp (getenv-alias "JANET_PATH")] (setdyn :syspath jp))
  (if-let [jp (getenv-alias "JANET_HEADERPATH")] (setdyn :headerpath jp))

  # Flag handlers
  (def handlers
    {"h" (fn [&]
           (print "usage: " (dyn :executable "janet") " [options] script args...")
           (print
             `Options are:
  -h : Show this help
  -v : Print the version string
  -s : Use raw stdin instead of getline like functionality
  -e code : Execute a string of janet
  -d : Set the debug flag in the repl
  -r : Enter the repl after running all scripts
  -p : Keep on executing if there is a top level error (persistent)
  -q : Hide logo (quiet)
  -k : Compile scripts but do not execute (flycheck)
  -m syspath : Set system path for loading global modules
  -c source output : Compile janet source code into an image
  -n : Disable ANSI color output in the repl
  -l lib : Import a module before processing more arguments
  -- : Stop handling options`)
           (os/exit 0)
           1)
     "v" (fn [&] (print janet/version "-" janet/build) (os/exit 0) 1)
     "s" (fn [&] (set *raw-stdin* true) (set *should-repl* true) 1)
     "r" (fn [&] (set *should-repl* true) 1)
     "p" (fn [&] (set *exit-on-error* false) 1)
     "q" (fn [&] (set *quiet* true) 1)
     "k" (fn [&] (set *compile-only* true) (set *exit-on-error* false) 1)
     "n" (fn [&] (set *colorize* false) 1)
     "m" (fn [i &] (setdyn :syspath (in args (+ i 1))) 2)
     "c" (fn c-switch [i &]
           (def e (dofile (in args (+ i 1))))
           (spit (in args (+ i 2)) (make-image e))
           (set *no-file* false)
           3)
     "-" (fn [&] (set *handleopts* false) 1)
     "l" (fn l-switch [i &]
           (import* (in args (+ i 1))
                    :prefix "" :exit *exit-on-error*)
           2)
     "e" (fn e-switch [i &]
           (set *no-file* false)
           (eval-string (in args (+ i 1)))
           2)
     "d" (fn [&] (set *debug* true) 1)})

  (defn- dohandler [n i &]
    (def h (in handlers n))
    (if h (h i) (do (print "unknown flag -" n) ((in handlers "h")))))

  (defn- evaluator
    [thunk source env where]
    (if *compile-only*
      (when (tuple? source)
        (def head (source 0))
        (def safe-check (safe-forms head))
        (cond
          # Sometimes safe form
          (function? safe-check)
          (if (safe-check source) (thunk))
          # Always safe form
          safe-check
          (thunk)
          # Use
          (= 'use head)
          (use-2 evaluator (tuple/slice source 1))
          # Import-like form
          (importers head)
          (do
            (let [[l c] (tuple/sourcemap source)
                  newtup (tuple/setmap (tuple ;source :evaluator evaluator) l c)]
              ((compile newtup env where))))))
      (thunk)))

  # Process arguments
  (var i 0)
  (def lenargs (length args))
  (while (< i lenargs)
    (def arg (in args i))
    (if (and *handleopts* (= "-" (string/slice arg 0 1)))
      (+= i (dohandler (string/slice arg 1) i))
      (do
        (set *no-file* false)
        (def env (make-env))
        (def subargs (array/slice args i))
        (put env :args subargs)
        (dofile arg :prefix "" :exit *exit-on-error* :evaluator evaluator :env env)
        (if-let [main (get (in env 'main) :value)]
          (let [thunk (compile [main ;(tuple/slice args i)] env arg)]
            (if (function? thunk) (thunk) (error (thunk :error)))))
        (set i lenargs))))

  (when (and (not *compile-only*) (or *should-repl* *no-file*))
    (if-not *quiet*
      (print "Janet " janet/version "-" janet/build " " (os/which) "/" (os/arch)))
    (flush)
    (defn getprompt [p]
      (def [line] (parser/where p))
      (string "repl:" line ":" (parser/state p :delimiters) "> "))
    (defn getstdin [prompt buf _]
      (file/write stdout prompt)
      (file/flush stdout)
      (file/read stdin :line buf))
    (def env (make-env))
    (if *debug* (put env :debug true))
    (def getter (if *raw-stdin* getstdin getline))
    (defn getchunk [buf p]
      (getter (getprompt p) buf env))
    (setdyn :pretty-format (if *colorize* "%.20Q" "%.20q"))
    (setdyn :err-color (if *colorize* true))
    (repl getchunk nil env)))

(put _env 'no-side-effects nil)
(put _env 'is-safe-def nil)
(put _env 'safe-forms nil)
(put _env 'importers nil)
(put _env 'use-2 nil)
(put _env 'getenv-alias nil)

###
###
### Clean up
###
###

(def root-env "The root environment used to create environments with (make-env)" _env)

(do
  (put _env 'boot/opts nil)
  (put _env '_env nil)
  (def load-dict (env-lookup _env))
  (merge-into load-image-dict load-dict)
  (merge-into make-image-dict (invert load-dict)))

###
###
### Bootstrap
###
###

(do

  (defn proto-flatten
    "Flatten a table and it's prototypes into a single table."
    [into x]
    (when x
      (proto-flatten into (table/getproto x))
      (loop [k :keys x]
        (put into k (x k))))
    into)

  (def env (fiber/getenv (fiber/current)))

  # Modify env based on some options.
  (loop [[k v] :pairs env
         :when (symbol? k)]
    (def flat (proto-flatten @{} v))
    (when (boot/config :no-docstrings)
      (put flat :doc nil))
    (when (boot/config :no-sourcemaps)
      (put flat :source-map nil))
    (put env k flat))

  (put env 'boot/config nil)
  (put env 'boot/args nil)
  (def image (let [env-pairs (pairs (env-lookup env))
                   essential-pairs (filter (fn [[k v]] (or (cfunction? v) (abstract? v))) env-pairs)
                   lookup (table ;(mapcat identity essential-pairs))
                   reverse-lookup (invert lookup)]
               (marshal env reverse-lookup)))

  # Create amalgamation

  (def feature-header "src/core/features.h")

  (def local-headers
    ["src/core/util.h"
     "src/core/state.h"
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
     "src/core/string.c"
     "src/core/strtod.c"
     "src/core/struct.c"
     "src/core/symcache.c"
     "src/core/table.c"
     "src/core/thread.c"
     "src/core/tuple.c"
     "src/core/typedarray.c"
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
