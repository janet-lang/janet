# The core janet library
# Copyright 2018 (C) Calvin Rose

###
###
### Macros and Basic Functions
###
###

(var *env*
  "The current environment."
  _env)

(def defn :macro
  "(def name & more)\n\nDefine a function. Equivalent to (def name (fn name [args] ...))."
  (fn defn [name & more]
    (def len (length more))
    (def modifiers @[])
    (var docstr "")
    (def fstart
      (fn recur [i]
        (def {i ith} more)
        (def t (type ith))
        (def tuple? (= t :tuple))
        (def array? (= t :array))
        (if (if tuple? tuple? array?)
          i
          (do 
            (if (= (type ith) :string)
              (:= docstr ith)
              (array.push modifiers ith))
            (if (< i len) (recur (+ i 1)))))))
    (def start (fstart 0))
    (def args (get more start))
    # Add arguments to definition
    (var index 0)
    (def arglen (length args))
    (def buf (buffer "(" name))
    (while (< index arglen)
      (buffer.push-string buf " " (get args index))
      (:= index (+ index 1)))
    (array.push modifiers (string buf ")\n\n" docstr))
    # Build return value
    (def fnbody (tuple.prepend (tuple.prepend (tuple.slice more start) name) 'fn))
    (def formargs (array.concat @['def name] modifiers @[fnbody]))
    (tuple.slice formargs 0)))

(defn defmacro :macro
  "Define a macro."
  [name & more]
    (apply defn (array.concat @[name :macro] more)))

(defmacro defmacro-
  "Define a private macro that will not be exported."
  [name & more]
  (tuple.slice (array.concat @['defmacro name :private] more) 0))

(defmacro defn-
  "Define a private function that will not be exported."
  [name & more]
  (tuple.slice (array.concat @['defn name :private] more) 0))

(defmacro def-
  "Define a private value that will not be exported."
  [name & more]
  (tuple.slice (array.concat @['def name :private] more) 0))

(defn defglobal
  "Dynamically create a global def."
  [name value]
  (def name* (symbol name))
  (put *env* name* @{:value value})
  nil)

(defn varglobal
  "Dynamically create a global var."
  [name init]
  (def name* (symbol name))
  (put *env* name* @{:ref @[init]})
  nil)

# Basic predicates
(defn even? "Check if x is even." [x] (== 0 (% x 2)))
(defn odd? "Check if x is odd." [x] (not= 0 (% x 2)))
(defn zero? "Check if x is zero." [x] (== x 0))
(defn pos? "Check if x is greater than 0." [x] (> x 0))
(defn neg? "Check if x is less than 0." [x] (< x 0))
(defn one? "Check if x is equal to 1." [x] (== x 1))
(defn integer? "Check if x is an integer." [x] (= (type x) :integer))
(defn real? [x] "Check if x is a real number." (= (type x) :real))
(defn number? "Check if x is a number." [x]
  (def t (type x))
  (if (= t :integer) true (= t :real)))
(defn fiber? "Check if x is a fiber." [x] (= (type x) :fiber))
(defn string? "Check if x is a string." [x] (= (type x) :string))
(defn symbol? "Check if x is a symbol." [x] (= (type x) :symbol))
(defn keyword? "Check if x is a keyword style symbol." 
  [x]
  (if (not= (type x) :symbol) nil (= 58 (get x 0))))
(defn buffer? "Check if x is a buffer." [x] (= (type x) :buffer))
(defn function? "Check if x is a function (not a cfunction)."
  [x] (= (type x) :function))
(defn cfunction? "Check if x a cfunction." [x] (= (type x) :cfunction))
(defn abstract? "Check if x an abstract type." [x] (= (type x) :abstract))
(defn table? [x] "Check if x a table." (= (type x) :table ))
(defn struct? [x] "Check if x a struct." (= (type x) :struct))
(defn array? [x] "Check if x is an array." (= (type x) :array))
(defn tuple? [x] "Check if x is a tuple." (= (type x) :tuple))
(defn boolean? [x] "Check if x is a boolean." (= (type x) :boolean))
(defn bytes? "Check if x is a string, symbol, or buffer." [x]
  (def t (type x))
  (if (= t :string) true (if (= t :symbol) true (= t :buffer))))
(defn dictionary? "Check if x a table or struct." [x]
  (def t (type x))
  (if (= t :table) true (= t :struct)))
(defn indexed? "Check if x is an array or tuple." [x]
  (def t (type x))
  (if (= t :array) true (= t :tuple)))
(defn callable? "Check if x is a function or cfunction." [x]
  (def t (type x))
  (if (= t :function) true (= t :cfunction)))
(defn true? "Check if x is true." [x] (= x true))
(defn false? "Check if x is false." [x] (= x false))
(defn nil? "Check if x is nil." [x] (= x nil))
(def atomic?
  "(atomic? x)\n\nCheck if x is a value that evaluates to itself when compiled."
  (do
    (def non-atomic-types
      {:array true
       :tuple true
       :table true
       :buffer true
       :struct true})
    (fn atomic? [x] (not (get non-atomic-types (type x))))))

# C style macros and functions for imperative sugar
(defn inc "Returns x + 1." [x] (+ x 1))
(defn dec "Returns x - 1." [x] (- x 1))
(defmacro ++ "Increments the var x by 1." [x] (tuple ':= x (tuple + x 1)))
(defmacro -- "Decrements the var x by 1." [x] (tuple ':= x (tuple - x 1)))
(defmacro += "Increments the var x by n." [x n] (tuple ':= x (tuple + x n)))
(defmacro -= "Decrements the vat x by n." [x n] (tuple ':= x (tuple - x n)))
(defmacro *= "Shorthand for (:= x (* x n))." [x n] (tuple ':= x (tuple * x n)))
(defmacro /= "Shorthand for (:= x (/ x n))." [x n] (tuple ':= x (tuple / x n)))
(defmacro %= "Shorthand for (:= x (% x n))." [x n] (tuple ':= x (tuple % x n)))
(defmacro &= "Shorthand for (:= x (& x n))." [x n] (tuple ':= x (tuple & x n)))
(defmacro |= "Shorthand for (:= x (| x n))." [x n] (tuple ':= x (tuple | x n)))
(defmacro ^= "Shorthand for (:= x (^ x n))." [x n] (tuple ':= x (tuple ^ x n)))
(defmacro >>= "Shorthand for (:= x (>> x n))." [x n] (tuple ':= x (tuple >> x n)))
(defmacro <<= "Shorthand for (:= x (<< x n))." [x n] (tuple ':= x (tuple << x n)))
(defmacro >>>= "Shorthand for (:= x (>>> x n))." [x n] (tuple ':= x (tuple >>> x n)))

(defmacro default
  "Define a default value for an optional argument.
  Expands to (def sym (if (= nil sym) val sym))"
  [sym val]
  (tuple 'def sym (tuple 'if (tuple = nil sym) val sym)))

(defmacro comment
  "Ignores the body of the comment."
  [])

(defmacro if-not
  "Shorthand for (if (not ... "
  [condition exp-1 exp-2]
  (tuple 'if condition exp-2 exp-1))

(defmacro when
  "Evaluates the body when the condition is true. Otherwise returns nil."
  [condition & body]
  (tuple 'if condition (tuple.prepend body 'do)))

(defmacro unless
  "Shorthand for (when (not ... "
  [condition & body]
  (tuple 'if condition nil (tuple.prepend body 'do)))

(defmacro cond
  "Evaluates conditions sequentially until the first true condition
  is found, and then executes the corresponding body. If there are an
  odd number of forms, the last expression is executed if no forms
  are matched. If there are no matches, return nil."
  [& pairs]
  (defn aux [i]
    (def restlen (- (length pairs) i))
    (if (= restlen 0) nil
      (if (= restlen 1) (get pairs i)
        (tuple 'if (get pairs i)
               (get pairs (+ i 1))
               (aux (+ i 2))))))
  (aux 0))

(defn doc*
  "Get the documentation for a symbol in a given environment."
  [env sym]
  (def x (get env sym))
  (if (not x)
    (print "symbol " sym " not found.")
    (do
      (def d (get x :doc))
      (print "\n" (if d d "no documentation found.") "\n"))))

(defmacro doc
  "Shows documentation for the given symbol."
  [sym]
  (tuple doc* '_env (tuple 'quote sym)))

(defmacro case
  "Select the body that equals the dispatch value. When pairs
  has an odd number of arguments, the last is the default expression.
  If no match is found, returns nil"
  [dispatch & pairs]
  (def atm (atomic? dispatch))
  (def sym (if atm dispatch (gensym)))
  (defn aux [i]
    (def restlen (- (length pairs) i))
    (if (= restlen 0) nil
      (if (= restlen 1) (get pairs i)
        (tuple 'if (tuple = sym (get pairs i))
               (get pairs (+ i 1))
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
    (array.push accum (tuple 'def k v))
    (+= i 2))
  (array.concat accum body)
  (tuple.slice accum 0))

(defmacro and
  "Evaluates to the last argument if all preceding elements are true, otherwise
  evaluates to false."
  [& forms]
  (def len (length forms))
  (if (= len 0)
    true
    ((fn aux [i]
       (cond
         (>= (+ 1 i) len) (get forms i)
         (tuple 'if (get forms i) (aux (+ 1 i)) false))) 0)))

(defmacro or
  "Evaluates to the last argument if all preceding elements are false, otherwise
  evaluates to true."
  [& forms]
  (def len (length forms))
  (if (= len 0)
    false
    ((fn aux [i]
       (def fi (get forms i))
       (if
         (>= (+ 1 i) len) fi
         (do
           (if (atomic? fi)
             (tuple 'if fi fi (aux (+ 1 i)))
             (do
               (def $fi (gensym))
               (tuple 'do (tuple 'def $fi fi)
                      (tuple 'if $fi $fi (aux (+ 1 i))))))))) 0)))

(defmacro loop
  "A general purpose loop macro."
  [head & body]
  (def len (length head))
  (defn doone
    @[i preds]
    (default preds @['and])
    (if (>= i len)
      (tuple.prepend body 'do)
      (do
        (def {
              i bindings
              (+ i 1) verb
              (+ i 2) object
              } head)
        (if (keyword? bindings)
          (case
            bindings
            :while (do
                     (array.push preds verb)
                     (doone (+ i 2) preds))
            :let (tuple 'let verb (doone (+ i 2)))
            :when (tuple 'if verb (doone (+ i 2)))
            (error ("unexpected loop predicate: " verb)))
          (case verb
            :iterate (do
                       (def $iter (gensym))
                       (def preds @['and (tuple ':= $iter object)])
                       (def subloop (doone (+ i 3) preds))
                       (tuple 'do
                              (tuple 'var $iter nil)
                              (tuple 'while (tuple.slice preds 0)
                                     (tuple 'def bindings $iter)
                                     subloop)))
            :range (do
                     (def [start end _inc] object)
                     (def inc (if _inc _inc 1))
                     (def endsym (gensym))
                     (def $iter (gensym))
                     (def preds @['and (tuple < $iter endsym)])
                     (def subloop (doone (+ i 3) preds))
                     (tuple 'do
                            (tuple 'var $iter start)
                            (tuple 'def endsym end)
                            (tuple 'while (tuple.slice preds 0)
                                   (tuple 'def bindings $iter)
                                   subloop
                                   (tuple ':= $iter (tuple + $iter inc)))))
            :keys (do
                    (def $dict (gensym))
                    (def $iter (gensym))
                    (def preds @['and (tuple not= nil $iter)])
                    (def subloop (doone (+ i 3) preds))
                    (tuple 'do
                           (tuple 'def $dict object)
                           (tuple 'var $iter (tuple next $dict nil))
                           (tuple 'while (tuple.slice preds 0)
                                  (tuple 'def bindings $iter)
                                  subloop
                                  (tuple ':= $iter (tuple next $dict $iter)))))
            :in (do
                  (def $len (gensym))
                  (def $i (gensym))
                  (def $indexed (gensym))
                  (def preds @['and (tuple < $i $len)])
                  (def subloop (doone (+ i 3) preds))
                  (tuple 'do
                         (tuple 'def $indexed object)
                         (tuple 'def $len (tuple length $indexed))
                         (tuple 'var $i 0)
                         (tuple 'while (tuple.slice preds 0)
                                (tuple 'def bindings (tuple get $indexed $i))
                                subloop
                                (tuple ':= $i (tuple + 1 $i)))))
            (error (string "unexpected loop verb: " verb)))))))
  (doone 0 nil))

(defmacro fora
  "Similar to loop, but accumulates the loop body into an array and returns that."
  [head & body]
  (def $accum (gensym))
  (tuple 'do
         (tuple 'def $accum @[])
         (tuple 'loop head
                (tuple array.push $accum
                       (tuple.prepend body 'do)))
         $accum))

(defmacro for
  "Similar to loop, but accumulates the loop body into a tuple and returns that."
  [head & body]
  (def $accum (gensym))
  (tuple 'do
         (tuple 'def $accum @[])
         (tuple 'loop head
                (tuple array.push $accum
                       (tuple.prepend body 'do)))
         (tuple tuple.slice $accum 0)))

(defn sum [xs]
  (var accum 0)
  (loop [x :in xs] (+= accum x))
  accum)

(defn product [xs]
  (var accum 1)
  (loop [x :in xs] (*= accum x))
  accum)

(defmacro coro
  "A wrapper for making fibers. Same as (fiber.new (fn @[] ...body))."
  [& body]
  (tuple fiber.new (apply tuple 'fn @[] body)))

(defmacro if-let
  "Takes the first one or two forms in a vector and if both are true binds
  all the forms with let and evaluates the first expression else
  evaluates the second"
  @[bindings tru fal]
  (def len (length bindings))
  (if (zero? len) (error "expected at least 1 binding"))
  (if (odd? len) (error "expected an even number of bindings"))
  (defn aux [i]
    (def bl (get bindings i))
    (def br (get bindings (+ 1 i)))
    (if (>= i len)
      tru
      (do
        (def atm (atomic? bl))
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
  "Takes the first one or two forms in vector and if true binds
  all the forms  with let and evaluates the body"
  [bindings & body]
  (tuple 'if-let bindings (tuple.prepend body 'do)))

(defn comp
  "Takes multiple functions and returns a function that is the composition
  of those functions."
  [& functions]
  (case (length functions)
    0 nil
    1 (get functions 0)
    2 (let [[f g]       functions] (fn [x] (f (g x))))
    3 (let [[f g h]     functions] (fn [x] (f (g (h x)))))
    4 (let [[f g h i]   functions] (fn [x] (f (g (h (i x))))))
    (let [[f g h i j] functions]
      (apply comp (fn [x] (f (g (h (i (j x))))))
             (tuple.slice functions 5 -1)))))

(defn identity
  "A function that returns its first argument."
  [x]
  x)

(defn complement
  "Returns a function that is the complement to the argument."
  [f]
  (fn [x] (not (f x))))

(defn extreme
  "Returns the most extreme value in args based on the orderer order.
  Returns nil if args is empty."
  [order args]
  (def len (length args))
  (when (pos? len)
    (var ret (get args 0))
    (loop [i :range [0 len]]
      (def v (get args i))
      (if (order v ret) (:= ret v)))
    ret))

(defn max [& args] (extreme > args))
(defn min [& args] (extreme < args))
(defn max-order [& args] (extreme order> args))
(defn min-order [& args] (extreme order< args))

###
###
### Indexed Combinators
###
###

(def sort
  "Sort an array in-place. Uses quicksort and is not a stable sort."
  (do

    (defn partition
      [a lo hi by]
      (def pivot (get a hi))
      (var i lo)
      (loop [j :range [lo hi]]
        (def aj (get a j))
        (when (by aj pivot)
          (def ai (get a i))
          (put a i aj)
          (put a j ai)
          (++ i)))
      (put a hi (get a i))
      (put a i pivot)
      i)

    (defn sort-help
      [a lo hi by]
      (when (> hi lo)
        (def piv (partition a lo hi by))
        (sort-help a lo (- piv 1) by)
        (sort-help a (+ piv 1) hi by))
      a)

    (fn @[a by]
      (sort-help a 0 (- (length a) 1) (or by order<)))))

(defn sorted
  "Returns the sorted version of an indexed data structure."
  @[ind by t]
  (def sa (sort (array.slice ind 0) by))
  (if (= :tuple (or t (type ind)))
    (tuple.slice sa 0)
    sa))

(defn reduce
  "Reduce, also know as fold-left in many languages, transforms
  an indexed type (array, tuple) with a function to produce a value."
  @[f init ind]
  (var res init)
  (loop [x :in ind]
    (:= res (f res x)))
  res)

(defn mapa
  "Map a function over every element in an indexed data structure and
  return an array of the results."
  [f & inds]
  (def ninds (length inds))
  (if (= 0 ninds) (error "expected at least 1 indexed collection"))
  (var limit (length (get inds 0)))
  (loop [i :range [0 ninds]]
    (def l (length (get inds i)))
    (if (< l limit) (:= limit l)))
  (def [i1 i2 i3 i4] inds)
  (def res (array.new limit))
  (case ninds
    1 (loop [i :range [0 limit]] (put res i (f (get i1 i))))
    2 (loop [i :range [0 limit]] (put res i (f (get i1 i) (get i2 i))))
    3 (loop [i :range [0 limit]] (put res i (f (get i1 i) (get i2 i) (get i3 i))))
    4 (loop [i :range [0 limit]] (put res i (f (get i1 i) (get i2 i) (get i3 i) (get i4 i))))
    (loop [i :range [0 limit]]
      (def args (array.new ninds))
      (loop [j :range [0 ninds]] (put args j (get (get inds j) i)))
      (put res i (apply f args))))
  res)

(defn map
  "Map a function over every element in an indexed data structure and
  return a tuple of the results."
  [f & inds]
  (tuple.slice (apply mapa f inds) 0))

(defn each
  "Map a function over every element in an array or tuple but do not
  return a new indexed type."
  [f & inds]
  (def ninds (length inds))
  (if (= 0 ninds) (error "expected at least 1 indexed collection"))
  (var limit (length (get inds 0)))
  (loop [i :range [0 ninds]]
    (def l (length (get inds i)))
    (if (< l limit) (:= limit l)))
  (def [i1 i2 i3 i4] inds)
  (case ninds
    1 (loop [i :range [0 limit]] (f (get i1 i)))
    2 (loop [i :range [0 limit]] (f (get i1 i) (get i2 i)))
    3 (loop [i :range [0 limit]] (f (get i1 i) (get i2 i) (get i3 i)))
    4 (loop [i :range [0 limit]] (f (get i1 i) (get i2 i) (get i3 i) (get i4 i)))
    (loop [i :range [0 limit]]
      (def args (array.new ninds))
      (loop [j :range [0 ninds]] (array.push args (get (get inds j) i)))
      (apply f args))))

(defn mapcat
  "Map a function over every element in an array or tuple and
  use array to concatenate the results. Returns the type given
  as the third argument, or same type as the input indexed structure."
  @[f ind t]
  (def res @[])
  (loop [x :in ind]
    (array.concat res (f x)))
  (if (= :tuple (or t (type ind)))
    (tuple.slice res 0)
    res))

(defn filter
  "Given a predicate, take only elements from an array or tuple for
  which (pred element) is truthy. Returns the type given as the
  third argument, or the same type as the input indexed structure."
  @[pred ind t]
  (def res @[])
  (loop [item :in ind]
    (if (pred item)
      (array.push res item)))
  (if (= :tuple (or t (type ind)))
    (tuple.slice res 0)
    res))

(defn range
  "Create an array of values [0, n)."
  [& args]
  (case (length args)
    1 (do
        (def [n] args)
        (def arr (array.new n))
        (loop [i :range [0 n]] (put arr i i))
        arr)
    2 (do
        (def [n m] args)
        (def arr (array.new n))
        (loop [i :range [n m]] (put arr (- i n) i))
        arr)
    (error "expected 1 to 2 arguments to range")))

(defn find-index
  "Find the index of indexed type for which pred is true. Returns nil if not found."
  [pred ind]
  (def len (length ind))
  (var i 0)
  (var going true)
  (while (if (< i len) going)
    (def item (get ind i))
    (if (pred item) (:= going false) (++ i)))
  (if going nil i))

(defn find
  "Find the first value in an indexed collection that satisfies a predicate. Returns
  nil if not found. Note their is no way to differentiate a nil from the indexed collection
  and a not found. Consider find-index if this is an issue."
  [pred ind]
  (get ind (find-index pred ind)))

(defn take-until
  "Given a predicate, take only elements from an indexed type that satisfy
  the predicate, and abort on first failure. Returns a new tuple."
  [pred ind]
  (def i (find-index pred ind))
  (if i
    (tuple.slice ind 0 i)
    ind))

(defn take-while
  "Same as (take-until (complement pred) ind)."
  [pred ind]
  (take-until (complement pred) ind))

(defn drop-until
  "Given a predicate, remove elements from an indexed type that satisfy
  the predicate, and abort on first failure. Returns a new tuple."
  [pred ind]
  (def i (find-index pred ind))
  (tuple.slice ind i))

(defn drop-while
  "Same as (drop-until (complement pred) ind)."
  [pred ind]
  (drop-until (complement pred) ind))

(defn juxt*
  [& funs]
  (fn [& args]
    (def ret @[])
    (loop [f :in funs]
      (array.push ret (apply f args)))
    (tuple.slice ret 0)))

(defmacro juxt
  [& funs]
  (def parts @['tuple])
  (def $args (gensym))
  (loop [f :in funs]
    (array.push parts (tuple apply f $args)))
  (tuple 'fn (tuple '& $args) (tuple.slice parts 0)))

(defmacro ->
  "Threading macro. Inserts x as the second value in the first form
  in form, and inserts the modified firsts form into the second form
  in the same manner, and so on. Useful for expressing pipelines of data."
  [x & forms]
  (defn fop [last n]
    (def [h t] (if (= :tuple (type n))
                 [tuple (get n 0) (array.slice n 1)]
                 [tuple n @[]]))
    (def parts (array.concat @[h last] t))
    (tuple.slice parts 0))
  (reduce fop x forms))

(defmacro ->>
  "Threading macro. Inserts x as the last value in the first form
  in form, and inserts the modified firsts form into the second form
  in the same manner, and so on. Useful for expressing pipelines of data."
  [x & forms]
  (defn fop [last n]
    (def [h t] (if (= :tuple (type n))
                 [tuple (get n 0) (array.slice n 1)]
                 [tuple n @[]]))
    (def parts (array.concat @[h] t @[last]))
    (tuple.slice parts 0))
  (reduce fop x forms))

(defn partial
  "Partial function application."
  [f & more]
  (if (zero? (length more)) f
    (fn [& r] (apply f (array.concat @[] more r)))))

(defn every? [pred ind]
  (var res true)
  (var i 0)
  (def len (length ind))
  (while (< i len)
    (def item (get ind i))
    (if (pred item)
      (++ i)
      (do (:= res false) (:= i len))))
  res)

(defn array.reverse
  "Reverses the order of the elements in a given array or tuple and returns a new array."
  [t]
  (var n (dec (length t)))
  (var reversed @[])
  (while (>= n 0)
    (array.push reversed (get t n))
    (-- n))
  reversed)

(defn tuple.reverse
  "Reverses the order of the elements given an array or tuple and returns a tuple"
  [t]
  (tuple.slice (array.reverse t) 0))

(defn reverse
  "Reverses order of elements in a given array or tuple"
  [t]
  ((case (type t)
     :tuple tuple.reverse
     :array array.reverse) t))

(defn invert
  "Returns a table of where the keys of an associative data structure
are the values, and the values of the keys. If multiple keys have the same
value, one key will be ignored."
  [ds]
  (def ret @{})
  (loop [k :keys ds]
    (put ret (get ds k) k))
  ret)

(defn zipcoll
  "Creates an table or tuple from two arrays/tuples. If a third argument of
  :struct is given result is struct else is table."
  @[keys vals t]
  (def res @{})
  (def lk (length keys))
  (def lv (length vals))
  (def len (if (< lk lv) lk lv))
  (loop [i :range [0 len]]
    (put res (get keys i) (get vals i)))
  (if (= :struct t)
    (table.to-struct res)
    res))

(defn update
  "Accepts a key argument and passes its' associated value to a function.
  The key then, is associated to the function's return value"
  [coll a-key a-function & args]
  (def old-value (get coll a-key))
  (put coll a-key (apply a-function old-value args)))

(defn merge
  "Merges multiple tables/structs to one. If a key appears in more than one
  collection, then later values replace any previous ones.
  The type of the first collection determines the type of the resulting
  collection"
  [& colls]
  (def container @{})
  (loop [c :in colls
         key :keys c]
    (put container key (get c key)))
  (if (table? (get colls 0)) container (table.to-struct container)))

(defn keys
  "Get the keys of an associative data structure."
  [x]
  (def arr (array.new (length x)))
  (var k (next x nil))
  (while (not= nil k)
    (array.push arr k)
    (:= k (next x k)))
  arr)

(defn values
  "Get the values of an associative data structure."
  [x]
  (def arr (array.new (length x)))
  (var k (next x nil))
  (while (not= nil k)
    (array.push arr (get x k))
    (:= k (next x k)))
  arr)

(defn pairs
  "Get the values of an associative data structure."
  [x]
  (def arr (array.new (length x)))
  (var k (next x nil))
  (while (not= nil k)
    (array.push arr (tuple k (get x k)))
    (:= k (next x k)))
  arr)

(defn frequencies
  "Get the number of occurences of each value in a indexed structure."
  [ind]
  (def freqs @{})
  (loop
    [x :in ind]
    (def n (get freqs x))
    (put freqs x (if n (+ 1 n) 1)))
  freqs)

(defn interleave
  "Returns an array of the first elements of each col,
  then the second, etc."
  [& cols]
  (def res @[])
  (def ncol (length cols))
  (when (> ncol 0)
    (def len (apply min (mapa length cols)))
    (loop [i :range [0 len]]
      (loop [ci :range [0 ncol]]
        (array.push res (get (get cols ci) i)))))
  res)

###
###
### Pretty Printer
###
###

(defn pp
  "Pretty print a value. Displays values inside collections, and is safe
  to call on any table. Does not print table prototype information."
  @[x file]

  (default file stdout)
  (def buf @"")
  (def indent @"\n")
  (def seen @{})
  (var nextid 0)

  # Forward declaration
  (var recur nil)

  (defn do-ds
    [y start end checkcycle dispatch]
    (def id (get seen y))
    (if (and checkcycle id)
      (do
        (buffer.push-string buf "<cycle ")
        (buffer.push-string buf (string id))
        (buffer.push-string buf ">"))
      (do
        (put seen y (++ nextid))
        (buffer.push-string buf start)
        (dispatch y)
        (buffer.push-string buf end))))

  (defn pp-seq [y]
    (def len (length y))
    (if (< len 5)
      (do
        (loop [i :range [0 len]]
          (when (not= i 0) (buffer.push-string buf " "))
          (recur (get y i))))
      (do
        (buffer.push-string indent "  ")
        (loop [i :range [0 len]]
          (when (not= i len) (buffer.push-string buf indent))
          (recur (get y i)))
        (buffer.popn indent 2)
        (buffer.push-string buf indent))))

  (defn pp-dict-nested [y]
    (buffer.push-string indent "  ")
    (loop [[k v] :in (sort (pairs y))]
      (buffer.push-string buf indent)
      (recur k)
      (buffer.push-string buf " ")
      (recur v))
    (buffer.popn indent 2)
    (buffer.push-string buf indent))

  (defn pp-dict-simple [y]
    (var i -1)
    (loop [[k v] :in (sort (pairs y))]
      (if (pos? (++ i)) (buffer.push-string buf " "))
      (recur k)
      (buffer.push-string buf " ")
      (recur v)))

  (defn pp-dict [y]
    (def complex? (> (length y) 4))
    ((if complex? pp-dict-nested pp-dict-simple) y))

  (def printers
    {:array  (fn [y] (do-ds y "@[" "]" true pp-seq))
     :tuple  (fn [y] (do-ds y "(" ")" false pp-seq))
     :table  (fn [y] (do-ds y "@{" "}" true pp-dict))
     :struct  (fn [y] (do-ds y "{" "}" false pp-dict))})

  (:= recur (fn [y]
              (def p (get printers (type y)))
              (if p
                (p y)
                (buffer.push-string buf (describe y)))))

  (recur x)
  (buffer.push-string buf "\n")

  (file.write file buf)
  nil)

###
###
### Macro Expansion
###
###

(defn macroexpand-1
  "Expand macros in a form, but do not recursively expand macros."
  [x]

  (defn dotable [t recur-value]
    (def newt @{})
    (var key (next t nil))
    (while (not= nil key)
      (put newt (macroexpand-1 key) (recur-value (get t key)))
      (:= key (next t key)))
    newt)

  (defn expand-bindings [x]
    (case (type x)
      :array (mapa expand-bindings x)
      :tuple (map expand-bindings x)
      :table (dotable x expand-bindings)
      :struct (table.to-struct (dotable x expand-bindings))
      (macroexpand-1 x)))

  (defn expanddef [t]
    (def len (length t))
    (def last (get t (- len 1)))
    (def last2 (get t (- len 2)))
    (tuple.slice (array.concat (array.slice t 0 -3)
                                @[(expand-bindings last2) (macroexpand-1 last)]) 0))

  (defn expandall [t]
    (def args (mapa macroexpand-1 (tuple.slice t 1)))
    (apply tuple (get t 0) args))

  (defn expandfn [t]
    (def args (mapa macroexpand-1 (tuple.slice t 2)))
    (apply tuple 'fn (get t 1) args))

  (def specs
    {':= expanddef
     'def expanddef
     'do expandall
     'fn expandfn
     'if expandall
     'quote identity
     'var expanddef
     'while expandall})

  (defn dotup [t]
    (def h (get t 0))
    (def s (get specs h))
    (def entry (or (get *env* h) {}))
    (def m (get entry :value))
    (def m? (get entry :macro))
    (cond
      s (s t)
      m? (apply m (tuple.slice t 1))
      (map macroexpand-1 t) 0))

  (def ret
    (case (type x)
      :tuple (dotup x)
      :array (mapa macroexpand-1 x)
      :struct (table.to-struct (dotable x macroexpand-1))
      :table (dotable x macroexpand-1)
      x))
  ret)

(defn all? [xs]
  (var good true)
  (loop [x :in xs :while good] (if x nil (:= good false)))
  good)

(defn some? [xs]
  (var bad true)
  (loop [x :in xs :while bad] (if x (:= bad false)))
  (not bad))

(defn deep-not= [x y]
  "Like not=, but mutable types (arrays, tables, buffers) are considered
  equal if they have identical structure. Much slower than not=."
  (def tx (type x))
  (or
    (not= tx (type y))
    (case tx
      :tuple (or (not= (length x) (length y)) (some? (map deep-not= x y)))
      :array (or (not= (length x) (length y)) (some? (map deep-not= x y)))
      :struct (deep-not= (pairs x) (pairs y))
      :table (deep-not= (table.to-struct x) (table.to-struct y))
      :buffer (not= (string x) (string y))
      (not= x y))))

(defn deep= [x y]
  "Like =, but mutable types (arrays, tables, buffers) are considered
  equal if they have identical structure. Much slower than =."
  (not (deep-not= x y)))

(defn macroexpand
  "Expand macros completely."
  [x]
  (var previous x)
  (var current (macroexpand-1 x))
  (var counter 0)
  (while (deep-not= current previous)
    (if (> (++ counter) 200)
      (error "macro expansion too nested"))
    (:= previous current)
    (:= current (macroexpand-1 current)))
  current)

###
###
### Evaluation and Compilation
###
###

(defn make-env
  @[parent]
  (def parent (if parent parent _env))
  (def newenv (table.setproto @{} parent))
  (put newenv '_env @{:value newenv :private true})
  newenv)

(defn run-context
  "Run a context. This evaluates expressions of janet in an environment,
  and is encapsulates the parsing, compilation, and evaluation of janet.
  env is the environment to evaluate the code in, chunks is a function
  that returns strings or buffers of source code (from a repl, file,
  network connection, etc. onvalue and onerr are callbacks that are
  invoked when a result is returned and when an error is produced,
  respectively.

  This function can be used to implement a repl very easily, simply
  pass a function that reads line from stdin to chunks, and print to
  onvalue."
  @[env chunks onvalue onerr where]

  # Are we done yet?
  (var going true)

  # The parser object
  (def p (parser.new))

  # Fiber stream of characters
  (def chars
    (coro
      (def buf @"")
      (var len 1)
      (while (< 0 len)
        (buffer.clear buf)
        (chunks buf p)
        (:= len (length buf))
        (loop [i :range [0 len]]
          (yield (get buf i))))
      0))

  # Fiber stream of values
  (def vals
    (coro
      (while going
        (case (parser.status p)
          :full (yield (parser.produce p))
          :error (do
                   (def (line col) (parser.where p))
                   (onerr where "parse" (string (parser.error p) " on line " line ", column " col)))
          (case (fiber.status chars)
            :new (parser.byte p (resume chars nil))
            :pending (parser.byte p (resume chars nil))
            (:= going false))))
      (when (not= :root (parser.status p))
        (onerr where "parse" "unexpected end of source"))))

  # Evaluate 1 source form
  (defn eval1 [source]
    (var good true)
    (def f
      (fiber.new
        (fn @[]
          (def res (compile source env where))
          (if (= (type res) :function)
            (res)
            (do
              (:= good false)
              (def {:error err :line errl :column errc :fiber errf} res)
              (onerr
                where
                "compile"
                (if (< 0 errl)
                  (string err "\n  in a form at line " errl ", column " errc)
                  err)
                errf))))
        :a))
    (def res (resume f nil))
    (when good
      (def sig (fiber.status f))
      (if going
        (if (= sig :dead)
          (onvalue res)
          (onerr where "runtime" res f)))))

  # Run loop
  (def oldenv *env*)
  (:= *env* env)
  (while going (eval1 (resume vals nil)))
  (:= *env* oldenv)

  env)

(defn default-error-handler
  @[source t x f]
  (file.write stderr (string t " error in " source ": "))
  (if (bytes? x)
    (do (file.write stderr x)
      (file.write stderr "\n"))
    (pp x stderr))
  (when f
    (def st (fiber.stack f))
    (loop
      [{:function func
        :tail tail
        :pc pc
        :c c
        :name name
        :source source
        :line source-line
        :column source-col} :in st]
      (file.write stderr "  in")
      (when c (file.write stderr " cfunction"))
      (if name
        (file.write stderr " " name)
        (when func (file.write stderr " " (string func))))
      (if source
        (do
          (file.write stderr " [" source "]")
          (if source-line
            (file.write
              stderr
              " on line "
              (string source-line)
              ", column "
              (string source-col)))))
      (if (and (not source-line) pc)
        (file.write stderr " (pc=" (string pc) ")"))
      (when tail (file.write stderr " (tailcall)"))
      (file.write stderr "\n"))))

(defn eval
  "Evaluates a string in the current environment. If more control over the
  environment is needed, use run-context."
  [str]
  (var state (string str))
  (defn chunks [buf _]
    (def ret state)
    (:= state nil)
    (when ret
      (buffer.push-string buf ret)
      (buffer.push-string buf "\n")))
  (var returnval nil)
  (run-context *env* chunks (fn [x] (:= returnval x)) default-error-handler "eval")
  returnval)

(do
  (def syspath (or (os.getenv "JANET_PATH") "/usr/local/lib/janet/"))
  (defglobal 'module.paths
    @["./?.janet"
      "./?/init.janet"
      "./janet_modules/?.janet"
      "./janet_modules/?/init.janet"
      (string syspath janet.version "/?.janet")
      (string syspath janet.version "/?/init.janet")
      (string syspath "/?.janet")
      (string syspath "/?/init.janet")])
  (defglobal 'module.native-paths
    @["./?.so"
      "./?/??.so"
      "./janet_modules/?.so"
      "./janet_modules/?/??.so"
      (string syspath janet.version "/?.so")
      (string syspath janet.version "/?/??.so")
      (string syspath "/?.so")
      (string syspath "/?/??.so")]))

(if (= :windows (os.which))
   (loop [i :range [0 (length module.native-paths)]]
     (def x (get module.native-paths i))
     (put
       module.native-paths
       i
       (string.replace ".so" ".dll" x))))

(defn module.find
  [path paths]
  (def parts (string.split "." path))
  (def last (get parts (- (length parts) 1)))
  (def normname (string.replace-all "." "/" path))
  (array.push
    (mapa (fn [x]
           (def y (string.replace "??" last x))
           (string.replace "?" normname y))
         paths)
    path))

(def require
  "Require a module with the given name. Will search all of the paths in
  module.paths, then the path as a raw file path. Returns the new environment
  returned from compiling and running the file."
  (do

    (defn check-mod
      [f testpath]
      (if f f (file.open testpath)))

    (defn find-mod [path]
      (def paths (module.find path module.paths))
      (reduce check-mod nil paths))

    (defn check-native
      [p testpath]
      (if p
        p
        (do
          (def f (file.open testpath))
          (if f (do (file.close f) testpath)))))

    (defn find-native [path]
      (def paths (module.find path module.native-paths))
      (reduce check-native nil paths))

    (def cache @{})
    (def loading @{})
    (fn require @[path args]
      (when (get loading path)
        (error (string "circular dependency: module " path " is loading")))
      (def {:exit exit-on-error} (or args {}))
      (def check (get cache path))
      (if check
        check
        (do
          (def newenv (make-env))
          (put cache path newenv)
          (put loading path true)
          (def f (find-mod path))
          (if f
            (do
              # Normal janet module
              (defn chunks [buf _] (file.read f 1024 buf))
              (run-context newenv chunks identity
                           (if exit-on-error
                             (fn @[a b c d] (default-error-handler a b c d) (os.exit 1))
                             default-error-handler)
                           path)
              (file.close f))
            (do
              # Try native module
              (def n (find-native path))
              (if (not n)
                (error (string "could not open file for module " path)))
              ((native n) newenv)))
          (put loading path false)
          newenv)))))

(defn import* [env path & args]
  (def targs (apply table args))
  (def {:as as
        :prefix prefix} targs)
  (def newenv (require path targs))
  (var k (next newenv nil))
  (def {:meta meta} newenv)
  (def prefix (or (and as (string as ".")) prefix (string path ".")))
  (while k
    (def v (get newenv k))
    (when (not (get v :private))
      (def newv (table.setproto @{:private true} v))
      (put env (symbol prefix k) newv))
    (:= k (next newenv k))))

(defmacro import
  "Import a module. First requires the module, and then merges its
  symbols into the current environment, prepending a given prefix as needed.
  (use the :as or :prefix option to set a prefix). If no prefix is provided,
  use the name of the module as a prefix."
  [path & args]
  (def argm (map (fn [x]
                   (if (and (symbol? x) (= (get x 0) 58))
                     x
                     (string x)))
                 args))
  (apply tuple import* '_env (string path) argm))

(defn repl
  "Run a repl. The first parameter is an optional function to call to
  get a chunk of source code. Should return nil for end of file."
  @[getchunk onvalue onerr]
  (def newenv (make-env))
  (default getchunk (fn @[buf]
                      (file.read stdin :line buf)))
  (default onvalue (fn [x]
                     (put newenv '_ @{:value x})
                     (pp x)))
  (default onerr default-error-handler)
  (run-context newenv getchunk onvalue onerr "repl"))

(defn all-symbols
  "Get all symbols available in the current environment."
  @[env]
  (default env *env*)
  (def envs @[])
  (do (var e env) (while e (array.push envs e) (:= e (table.getproto e))))
  (array.reverse envs)
  (def symbol-set @{})
  (defn onenv [envi]
    (defn onk [k]
      (put symbol-set k true))
    (each onk (keys envi)))
  (each onenv envs)
  (sort (keys symbol-set)))
