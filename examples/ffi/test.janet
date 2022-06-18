(def ffi/loc "examples/ffi/so.so")
(def ffi/source-loc "examples/ffi/so.c")

(os/execute ["cc" ffi/source-loc "-shared" "-o" ffi/loc] :px)
(def module (ffi/native ffi/loc))

(def int-fn-sig (ffi/signature :default :int :int :int))
(def int-fn-pointer (ffi/lookup module "int_fn"))
(defn int-fn
  [x y]
  (ffi/call int-fn-pointer int-fn-sig x y))

(def double-fn-sig (ffi/signature :default :double :double :double :double))
(def double-fn-pointer (ffi/lookup module "double_fn"))
(defn double-fn
  [x y z]
  (ffi/call double-fn-pointer double-fn-sig x y z))

(def double-many-sig (ffi/signature :default :double :double :double :double :double :double :double))
(def double-many-pointer (ffi/lookup module "double_many"))
(defn double-many
  [x y z w a b]
  (ffi/call double-many-pointer double-many-sig x y z w a b))

(def double-lots-sig (ffi/signature :default :double
                                    :double :double :double :double :double
                                    :double :double :double :double :double))
(def double-lots-pointer (ffi/lookup module "double_lots"))
(defn double-lots
  [a b c d e f g h i j]
  (ffi/call double-lots-pointer double-lots-sig a b c d e f g h i j))

(def float-fn-sig (ffi/signature :default :double :float :float :float))
(def float-fn-pointer (ffi/lookup module "float_fn"))
(defn float-fn
  [x y z]
  (ffi/call float-fn-pointer float-fn-sig x y z))

(def intint-fn-sig (ffi/signature :default :int :double [:int :int]))
(def intint-fn-pointer (ffi/lookup module "intint_fn"))
(defn intint-fn
  [x ii]
  (ffi/call intint-fn-pointer intint-fn-sig x ii))

(def return-struct-sig (ffi/signature :default [:int :int] :int))
(def return-struct-pointer (ffi/lookup module "return_struct"))
(defn return-struct-fn
  [i]
  (ffi/call return-struct-pointer return-struct-sig i))

(def intintint (ffi/struct :int :int :int))
(def intintint-fn-sig (ffi/signature :default :int :double intintint))
(def intintint-fn-pointer (ffi/lookup module "intintint_fn"))
(defn intintint-fn
  [x iii]
  (ffi/call intintint-fn-pointer intintint-fn-sig x iii))

(def big (ffi/struct :s64 :s64 :s64))
(def struct-big-fn-sig (ffi/signature :default big :int :double))
(def struct-big-fn-pointer (ffi/lookup module "struct_big"))
(defn struct-big-fn
  [i d]
  (ffi/call struct-big-fn-pointer struct-big-fn-sig i d))

(def void-fn-pointer (ffi/lookup module "void_fn"))
(def void-fn-sig (ffi/signature :default :void))
(defn void-fn
  []
  (ffi/call void-fn-pointer void-fn-sig))

#
# Call functions
#

(pp (void-fn))
(pp (int-fn 10 20))
(pp (double-fn 1.5 2.5 3.5))
(pp (double-many 1 2 3 4 5 6))
(pp (double-lots 1 2 3 4 5 6 7 8 9 10))
(pp (float-fn 8 4 17))
(pp (intint-fn 123.456 [10 20]))
(pp (intintint-fn 123.456 [10 20 30]))
(pp (return-struct-fn 42))
(pp (struct-big-fn 11 99.5))

(assert (= 60 (int-fn 10 20)))
(assert (= 42 (double-fn 1.5 2.5 3.5)))
(assert (= 21 (double-many 1 2 3 4 5 6)))
(assert (= 19 (double-lots 1 2 3 4 5 6 7 8 9 10)))
(assert (= 204 (float-fn 8 4 17)))

#
# Struct reading and writing
#

(defn check-round-trip
  [t value]
  (def buf (ffi/write t value))
  (def same-value (ffi/read t buf))
  (assert (deep= value same-value)
          (string/format "round trip %j (got %j)" value same-value)))

(check-round-trip :bool true)
(check-round-trip :bool false)
(check-round-trip :void nil)
(check-round-trip :void nil)
(check-round-trip :s8 10)
(check-round-trip :s8 0)
(check-round-trip :s8 -10)
(check-round-trip :u8 10)
(check-round-trip :u8 0)
(check-round-trip :s16 10)
(check-round-trip :s16 0)
(check-round-trip :s16 -12312)
(check-round-trip :u16 10)
(check-round-trip :u16 0)
(check-round-trip :u32 0)
(check-round-trip :u32 10)
(check-round-trip :u32 0xFFFF7777)
(check-round-trip :s32 0x7FFF7777)
(check-round-trip :s32 0)
(check-round-trip :s32 -1234567)

(def s (ffi/struct :s8 :s8 :s8 :float))
(check-round-trip s [1 3 5 123.5])
(check-round-trip s [-1 -3 -5 -123.5])

(print "Done.")
