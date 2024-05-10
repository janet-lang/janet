# Make a language frontend for the sysir.
# Dialect:
# TODO -
# * basic types
# * constants
# * sequence (do)
# * basic arithmetic
# * bindings
# * branch (if)
# * looping
# * returns
# * tail call returns
# * function definitions
# * arrays (declaration, loads, stores)
# * ...
# insight - using : inside symbols for types can be used to allow manipulating symbols with macros

(def slot-to-name @[])
(def name-to-slot @{})
(def type-to-name @[])
(def name-to-type @{})

(defn get-slot
  [&opt new-name]
  (def next-slot (length slot-to-name))
  (array/push slot-to-name new-name)
  (if new-name (put name-to-slot new-name next-slot))
  next-slot)

(defn named-slot
  [name]
  (assert (get name-to-slot name)))

(defn make-type
  [&opt new-name]
  (def next-type (length type-to-name))
  (array/push type-to-name new-name)
  (if new-name (put name-to-type new-name next-type))
  next-type)

(defn named-type
  [name]
  (def t (get name-to-type name))
  (assert t)
  t)

(defn setup-default-types
  [into]
  (defn add-prim-type
    [name native-name]
    (array/push into ~(type-prim ,name ,native-name))
    (make-type name))
  (add-prim-type 'float 'f32)
  (add-prim-type 'double 'f64)
  (add-prim-type 'boolean 'boolean))

(defn type-extract
  "Given a symbol:type combination, extract the proper name and the type separately"
  [combined-name &opt default-type]
  (def parts (string/split ":" combined-name 0 2))
  (def [name tp] parts)
  [(symbol name) (symbol (or tp default-type))])

(var do-binop nil)
(var do-comp nil)

(defn visit1
  "Take in a form and compile code and put it into `into`. Return result slot."
  [code into]
  (cond

    # Compile a constant
    (number? code)
    (let [slot (get-slot)
          slottype 'double]
      (array/push into ~(bind ,slot ,slottype))
      (array/push into ~(constant ,slot ,code))
      slot)

    # Binding
    (symbol? code)
    (named-slot code)

    # Compile forms
    (and (tuple? code) (= :parens (tuple/type code)))
    (do
      (assert (> (length code) 0))
      (def [op & args] code)
      (case op

        # Arithmetic
        '+ (do-binop 'add args into)
        '- (do-binop 'subtract args into)
        '* (do-binop 'multiply args into)
        '/ (do-binop 'divide args into)

        # Comparison
        '= (do-comp 'eq args into)
        'not= (do-comp 'neq args into)
        '< (do-comp 'lt args into)
        '<= (do-comp 'lte args into)
        '> (do-comp 'gt args into)
        '>= (do-comp 'gte args into)

        # Type hinting
        'the
        (do
          (assert (= 2 (length args)))
          (def [xtype x] args)
          (def result (visit1 x into))
          (array/push into ~(bind ,result ,xtype))
          result)

        # Named bindings
        # TODO - type inference
        'def
        (do
          (assert (= 2 (length args)))
          (def [full-name value] args)
          (assert (symbol? full-name))
          (def [name tp] (type-extract full-name 'double))
          (def result (visit1 value into))
          (def slot (get-slot name))
          (when tp
            (array/push into ~(bind ,slot ,tp)))
          (array/push into ~(move ,slot ,result))
          slot)

        # Assignment
        'set
        (do
          (assert (= 2 (length args)))
          (def [to x] args)
          (def result (visit1 x into))
          (def toslot (get-slot to))
          (array/push into ~(move ,toslot ,result))
          toslot)

        # Return
        'return
        (do
          (assert (>= 1 (length args)))
          (if (empty? args)
            (array/push into '(return))
            (do
              (def [x] args)
              (array/push into ~(return ,(visit1 x into)))))
          nil)

        # Sequence of operations
        'do
        (do
          (var ret nil)
          (each form args (set ret (visit1 form into)))
          ret)
        (errorf "unknown form %V" code)))
    (errorf "cannot compile %V" code)))

(varfn do-binop
  "Emit a 'binary' op succh as (+ x y). 
  Extended to support any number of arguments such as (+ x y z ...)"
  [opcode args into]
  (var final nil)
  (each arg args
    (def right (visit1 arg into))
    (set final
         (if final
           (let [result (get-slot)]
             # TODO - finish type inference - we should be able to omit the bind
             # call and sysir should be able to infer the type
             (array/push into ~(bind ,result double))
             (array/push into ~(,opcode ,result ,final ,right))
             result)
           right)))
  (assert final))

(varfn do-comp
  "Emit a comparison form such as (= x y z ...)"
  [opcode args into]
  (def result (get-slot))
  (def needs-temp (> 2 (length args)))
  (def temp-result (if needs-temp (get-slot) nil))
  (array/push into ~(bind ,result boolean))
  (when needs-temp
    (array/push into ~(bind ,temp-result boolean)))
  (var left nil)
  (var first-compare true)
  (each arg args
    (def right (visit1 arg into))
    (when left
      (if first-compare
        (array/push into ~(,opcode ,result ,left ,right))
        (do
          (array/push into ~(,opcode ,temp-result ,left ,right))
          (array/push into ~(and ,result ,temp-result ,result))))
      (set first-compare false))
    (set left right))
  result)

###
###
###

(def myprog
  '(do
     (def xyz:double (+ 1 2 3))
     (def abc:double (* 4 5 6))
     (def x:boolean (= 5 7))
     (return (/ abc xyz))))

(defn dotest
  []
  (def ctx (sysir/context))
  (def ir-asm
    @['(link-name "entry")
      '(parameter-count 0)])
  (setup-default-types ir-asm)
  (visit1 myprog ir-asm)
  (printf "%.99M" ir-asm)
  (sysir/asm ctx ir-asm)
  (print (sysir/to-c ctx)))

(dotest)
