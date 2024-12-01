# Make a language frontend for the sysir.
# Dialect:
# TODO -
# * arrays (declaration, loads, stores)

(defdyn *ret-type* "Current function return type")

(def slot-to-name @[])
(def name-to-slot @{})
(def type-to-name @[])
(def name-to-type @{})
(def slot-types @{})
(def functions @{})
(def type-fields @{})
(def syscalls @{})

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

(defn binding-type
  [name]
  (def slot (assert (get name-to-slot name)))
  (assert (get slot-types slot)))

(defn slot-type
  [slot]
  (assert (get slot-types slot)))

(defn assign-type
  [name typ]
  (def slot (get name-to-slot name))
  (put slot-types slot typ))

(defn assign-slot-type
  [slot typ]
  (put slot-types slot typ))

(defn setup-default-types
  [ctx]
  (def into @[])
  (defn add-prim-type
    [name native-name]
    (array/push into ~(type-prim ,name ,native-name))
    (make-type name))
  (add-prim-type 'float 'f32)
  (add-prim-type 'double 'f64)
  (add-prim-type 'int 's32)
  (add-prim-type 'uint 'u32)
  (add-prim-type 'long 's64)
  (add-prim-type 'ulong 'u64)
  (add-prim-type 'boolean 'boolean)
  (add-prim-type 's16 's16)
  (add-prim-type 'u16 'u16)
  (add-prim-type 'byte 'u8)
  (add-prim-type 'void 'void)
  (array/push into ~(type-pointer pointer void))
  (make-type 'pointer)
  (sysir/asm ctx into)
  ctx)

(defn type-extract
  "Given a symbol:type combination, extract the proper name and the type separately"
  [combined-name &opt default-type]
  (def parts (string/split ":" combined-name 0 2))
  (def [name tp] parts)
  [(symbol name) (symbol (or tp default-type))])

(var do-binop nil)
(var do-comp nil)

###
### Inside functions
###

(defn visit1
  "Take in a form and compile code and put it into `into`. Return result slot."
  [code into &opt no-return type-hint]
  (def subresult
      (cond

      # Compile a constant
      (string? code) ~(pointer ,code)
      (boolean? code) ~(boolean ,code)
      (number? code) ~(,(or type-hint 'double) ,code) # TODO - should default to double

      # Needed?
      (= :core/u64 (type code)) ~(ulong ,code)
      (= :core/s64 (type code)) ~(long ,code)

      # Binding
      (symbol? code)
      (named-slot code)

      # Array literals
      (and (tuple? code) (= :brackets (tuple/type code)))
      (do
        (assert type-hint (string/format "unknown type for array literal %v" code))
        ~(,type-hint ,code))

      # Compile forms
      (and (tuple? code) (= :parens (tuple/type code)))
      (do
        (assert (> (length code) 0))
        (def [op & args] code)
        (case op

          # Arithmetic
          '+ (do-binop 'add args into type-hint)
          '- (do-binop 'subtract args into type-hint)
          '* (do-binop 'multiply args into type-hint)
          '/ (do-binop 'divide args into type-hint)
          '<< (do-binop 'shl args into type-hint)
          '>> (do-binop 'shr args into type-hint)

          # Comparison
          '= (do-comp 'eq args into)
          'not= (do-comp 'neq args into)
          '< (do-comp 'lt args into)
          '<= (do-comp 'lte args into)
          '> (do-comp 'gt args into)
          '>= (do-comp 'gte args into)

          # Pointers
          'pointer-add
          (do
            (assert (= 2 (length args)))
            (def [base offset] args)
            (def base-slot (visit1 base into false type-hint))
            (def offset-slot (visit1 offset into false 'int))
            (def slot (get-slot))
            (when type-hint (array/push into ~(bind ,slot ,type-hint)))
            (array/push into ~(pointer-add ,slot ,base-slot ,offset-slot))
            slot)

          'pointer-sub
          (do
            (assert (= 2 (length args)))
            (def [base offset] args)
            (def base-slot (visit1 base into false type-hint))
            (def offset-slot (visit1 offset into false 'int))
            (def slot (get-slot))
            (when type-hint (array/push into ~(bind ,slot ,type-hint)))
            (array/push into ~(pointer-subtract ,slot ,base-slot ,offset-slot))
            slot)

          # Type hinting
          'the
          (do
            (assert (= 2 (length args)))
            (def [xtype x] args)
            (def result (visit1 x into false xtype))
            (if (tuple? result) # constant
              (let [[t y] result]
                (assertf (= t xtype) "type mismatch, %p doesn't match %p" t xtype)
                [xtype y])
              (do
                (array/push into ~(bind ,result ,xtype))
                result)))

          # Casting
          'cast
          (do
            (assert (= 1 (length args)))
            (assert type-hint) # should we add an explicit cast type?
            (def [x] args)
            (def slot (get-slot))
            (def result (visit1 x into false))
            (array/push into ~(bind ,slot ,type-hint))
            (array/push into ~(cast ,slot ,result))
            slot)

          # Named bindings
          'def
          (do
            (assert (= 2 (length args)))
            (def [full-name value] args)
            (assert (symbol? full-name))
            (def [name tp] (type-extract full-name 'int))
            (def result (visit1 value into false tp))
            (def slot (get-slot name))
            (assign-type name tp)
            (array/push into ~(bind ,slot ,tp))
            (array/push into ~(move ,slot ,result))
            slot)

          # Named variables
          'var
          (do
            (assert (= 2 (length args)))
            (def [full-name value] args)
            (assert (symbol? full-name))
            (def [name tp] (type-extract full-name 'int))
            (def result (visit1 value into false tp))
            (def slot (get-slot name))
            (assign-type name tp)
            (array/push into ~(bind ,slot ,tp))
            (array/push into ~(move ,slot ,result))
            slot)

          # Address of (& operator in C)
          'address
          (do
            (assert (= 1 (length args)))
            (def [thing] args)
            (def [name tp] (type-extract thing 'int))
            (def result (visit1 thing into false tp))
            (def slot (get-slot))
            # 
            (array/push into ~(bind ,slot ,type-hint))
            (array/push into ~(address ,slot ,result))
            slot)

          'load
          (do
            (assert (= 1 (length args)))
            (assert type-hint)
            (def [thing] args)
            # (def [name tp] (type-extract thing 'pointer))
            (def result (visit1 thing into false))
            (def slot (get-slot))
            (def ptype type-hint)
            (array/push into ~(bind ,slot ,ptype))
            (array/push into ~(load ,slot ,result))
            slot)

          'store
          (do
            (assert (= 2 (length args)))
            (def [dest value] args)
            # (def [name tp] (type-extract dest 'pointer))
            (def dest-r (visit1 dest into false))
            (def value-r (visit1 value into false))
            (array/push into ~(store ,dest-r ,value-r))
            value-r)

          # Assignment
          'set
          (do
            (assert (= 2 (length args)))
            (def [to x] args)
            (def type-hint (binding-type to))
            (def result (visit1 x into false type-hint))
            (def toslot (named-slot to))
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
                (array/push into ~(return ,(visit1 x into false (dyn *ret-type*))))))
            nil)

          # Sequence of operations
          'do
          (do
            (each form (slice args 0 -2) (visit1 form into true))
            (visit1 (last args) into false type-hint))

          # While loop
          'while
          (do
            (def lab-test (keyword (gensym)))
            (def lab-exit (keyword (gensym)))
            (assert (< 1 (length args)))
            (def [cnd & body] args)
            (array/push into lab-test)
            (def condition-slot (visit1 cnd into false 'boolean))
            (array/push into ~(branch-not ,condition-slot ,lab-exit))
            (each code body
              (visit1 code into true))
            (array/push into ~(jump ,lab-test))
            (array/push into lab-exit)
            nil)

          # Branch
          'if
          (do
            (def lab (keyword (gensym)))
            (def lab-end (keyword (gensym)))
            (assert (< 2 (length args) 4))
            (def [cnd tru fal] args)
            (def condition-slot (visit1 cnd into false 'boolean))
            (def ret (if type-hint (get-slot)))
            (when type-hint (array/push into ~(bind ,ret ,type-hint)))
            (array/push into ~(branch ,condition-slot ,lab))
            # false path
            (if type-hint
              (array/push into ~(move ,ret ,(visit1 fal into false type-hint)))
              (visit1 fal into true))
            (array/push into ~(jump ,lab-end))
            (array/push into lab)
            # true path
            (if type-hint
              (array/push into ~(move ,ret ,(visit1 tru into false type-hint)))
              (visit1 tru into true))
            (array/push into lab-end)
            ret)

          # Insert IR
          'ir
          (do
            (assert no-return)
            (array/push into ;args)
            nil)

          # Assume function call or syscall
          (do
            (def slots @[])
            (def signature (get functions op))
            (def is-syscall (get syscalls op))
            (assert signature (string "unknown function " op))
            (def ret (if no-return nil (get-slot)))
            (when ret
              (array/push into ~(bind ,ret ,(first signature)))
              (assign-type ret (first signature)))
            (each [arg-type arg] (map tuple (drop 1 signature) args)
              (array/push slots (visit1 arg into false arg-type)))
            (if is-syscall
              (array/push into ~(syscall :default ,ret (int ,is-syscall) ,;slots))
              (array/push into ~(call :default ,ret [pointer ,op] ,;slots)))
            ret)))

      (errorf "cannot compile %q" code)))

  # Check type-hint matches return type
  (if type-hint
    (when-let [t (first subresult)] # TODO - Disallow empty types
      (assert (= type-hint t) (string/format "%j, expected type %v, got %v" code type-hint t))))

  subresult)

(varfn do-binop
  "Emit an operation such as (+ x y).
  Extended to support any number of arguments such as (+ x y z ...)"
  [opcode args into type-hint]
  (var typ type-hint)
  (var final nil)
  (def slots @[])
  (each arg args
    (def right (visit1 arg into false typ))
    (when (number? right) (array/push slots right))

    # If we don't have a type hint, infer types from bottom up
    (when (nil? typ)
      (when-let [new-typ (get slot-types right)]
        (set typ new-typ)))

    (set final
         (if final
           (let [result (get-slot)]
             (array/push slots result)
             (array/push into ~(,opcode ,result ,final ,right))
             result)
           right)))
  (assert typ (string "unable to infer type for %j" [opcode ;args]))
  (each slot (distinct slots)
    (array/push into ~(bind ,slot ,typ)))
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
  (var typ nil)
  (each arg args
    (def right (visit1 arg into false typ))
    # If we don't have a type hint, infer types from bottom up
    (when (nil? typ)
      (when-let [new-typ (get slot-types right)]
        (set typ new-typ)))
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
### Top level
###

(defn top
  "Visit and emit code for a top level form."
  [ctx form]
  (assert (tuple? form))
  (def [head & rest] form)
  (case head

    # Declare a struct
    'defstruct
    (do
      (def into @[])
      (def [name & fields] rest)
      (assert (even? (length fields)) "expected an even number of fields for struct definition")
      (def field-types @[])
      (each [field-name typ] (partition 2 fields)
        # TODO - don't ignore field names
        (array/push field-types typ))
      (array/push into ~(type-struct ,name ,;field-types))
      # (eprintf "%.99M" into)
      (sysir/asm ctx into))

    # Declare a union
    'defunion
    (do
      (def into @[])
      (def [name & fields] rest)
      (assert (even? (length fields)) "expected an even number of fields for struct definition")
      (def field-types @[])
      (each [field-name typ] (partition 2 fields)
        # TODO - don't ignore field names
        (array/push field-types typ))
      (array/push into ~(type-union ,name ,;field-types))
      # (eprintf "%.99M" into)
      (sysir/asm ctx into))

    # Declare a pointer type
    'defpointer
    (do
      (def into @[])
      (def [name element] rest)
      (def field-types @[])
      (array/push into ~(type-pointer ,name ,element))
      # (eprintf "%.99M" into)
      (sysir/asm ctx into))

    # Declare an array type
    'defarray
    (do
      (def into @[])
      (def [name element cnt] rest)
      (assert (and (pos? cnt) (int? cnt)) "expected positive integer for array count")
      (array/push into ~(type-array ,name ,element ,cnt))
      # (eprintf "%.99M" into)
      (sysir/asm ctx into))
    
    # External function
    'defn-external
    (do
      (def [name args] rest)
      (assert (tuple? args))
      (def [fn-name fn-tp] (type-extract name 'void))
      (def pcount (length args)) #TODO - more complicated signatures
      (def signature @[fn-tp])
      (each arg args
        (def [name tp] (type-extract arg 'int))
        (array/push signature tp))
      (put functions fn-name (freeze signature)))

    # External syscall
    'defn-syscall
    (do
      (def [name sysnum args] rest)
      (assert (tuple? args))
      (def [fn-name fn-tp] (type-extract name 'void))
      (def pcount (length args)) #TODO - more complicated signatures
      (def signature @[fn-tp])
      (each arg args
        (def [name tp] (type-extract arg 'int))
        (array/push signature tp))
      (put syscalls fn-name sysnum)
      (put functions fn-name (freeze signature)))

    # Top level function definition
    'defn
    (do
      # TODO doc strings
      (table/clear name-to-slot)
      (table/clear slot-types)
      (array/clear slot-to-name)
      (def [name args & body] rest)
      (assert (tuple? args))
      (def [fn-name fn-tp] (type-extract name 'void))
      (def pcount (length args)) #TODO - more complicated signatures
      (def ir-asm
        @[~(link-name ,(string fn-name))
          ~(parameter-count ,pcount)])
      (def signature @[fn-tp])
      (each arg args
        (def [name tp] (type-extract arg 'int))
        (def slot (get-slot name))
        (assign-type name tp)
        (array/push signature tp)
        (array/push ir-asm ~(bind ,slot ,tp)))
      (with-dyns [*ret-type* fn-tp]
        (each part body
          (visit1 part ir-asm true)))
      (put functions fn-name (freeze signature))
      (when (dyn :verbose) (eprintf "%.99M" ir-asm))
      (sysir/asm ctx ir-asm))

    (errorf "unknown form %p" form)))

###
### Setup
###

(def ctx (sysir/context))
(setup-default-types ctx)

(defn compile1
  [x]
  (top ctx x))

(defn dump
  []
  (eprintf "%.99M\n" (sysir/to-ir ctx)))

(defn dumpx64
  []
  (print (sysir/to-x64 ctx)))

(defn dumpx64-windows
  []
  (print (sysir/to-x64 ctx @"" :windows)))

(defn dumpc
  []
  (print (sysir/to-c ctx)))

###
### Top Level aliases
###

(defmacro defstruct [& args] [compile1 ~',(keep-syntax! (dyn *macro-form*) ~(defstruct ,;args))])
(defmacro defunion [& args] [compile1 ~',(keep-syntax! (dyn *macro-form*) ~(defunion ,;args))])
(defmacro defarray [& args] [compile1 ~',(keep-syntax! (dyn *macro-form*) ~(defarray ,;args))])
(defmacro defpointer [& args] [compile1 ~',(keep-syntax! (dyn *macro-form*) ~(defpointer ,;args))])
(defmacro defn-external [& args] [compile1 ~',(keep-syntax! (dyn *macro-form*) ~(defn-external ,;args))])
(defmacro defn-syscall [& args] [compile1 ~',(keep-syntax! (dyn *macro-form*) ~(defn-syscall ,;args))])
(defmacro defsys [& args] [compile1 ~',(keep-syntax! (dyn *macro-form*) ~(defn ,;args))])
