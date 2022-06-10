(def native-loc "ffitest/so.so")
(def native-source-loc "ffitest/so.c")

(os/execute ["cc" native-source-loc "-shared" "-o" native-loc] :px)
(def module (raw-native native-loc))

(def int-fn-sig (native-signature :default :int :int :int))
(def int-fn-pointer (native-lookup module "int_fn"))
(defn int-fn
  [x y]
  (native-call int-fn-pointer int-fn-sig x y))

(def double-fn-sig (native-signature :default :double :double :double :double))
(def double-fn-pointer (native-lookup module "double_fn"))
(defn double-fn
  [x y z]
  (native-call double-fn-pointer double-fn-sig x y z))

(def double-many-sig (native-signature :default :double :double :double :double :double :double :double))
(def double-many-pointer (native-lookup module "double_many"))
(defn double-many
  [x y z w a b]
  (native-call double-many-pointer double-many-sig x y z w a b))

(def double-lots-sig (native-signature :default :double
                                    :double :double :double :double :double
                                    :double :double :double :double :double))
(def double-lots-pointer (native-lookup module "double_lots"))
(defn double-lots
  [a b c d e f g h i j]
  (native-call double-lots-pointer double-lots-sig a b c d e f g h i j))

(def float-fn-sig (native-signature :default :double :float :float :float))
(def float-fn-pointer (native-lookup module "float_fn"))
(defn float-fn
  [x y z]
  (native-call float-fn-pointer float-fn-sig x y z))

(def intint-fn-sig (native-signature :default :int :double [:int :int]))
(def intint-fn-pointer (native-lookup module "intint_fn"))
(defn intint-fn
  [x ii]
  (native-call intint-fn-pointer intint-fn-sig x ii))

(def return-struct-sig (native-signature :default [:int :int] :int))
(def return-struct-pointer (native-lookup module "return_struct"))
(defn return-struct-fn
  [i]
  (native-call return-struct-pointer return-struct-sig i))

(def intintint (native-struct :int :int :int))
(def intintint-fn-sig (native-signature :default :int :double intintint))
(def intintint-fn-pointer (native-lookup module "intintint_fn"))
(defn intintint-fn
  [x iii]
  (native-call intintint-fn-pointer intintint-fn-sig x iii))

(def big (native-struct :s64 :s64 :s64))
(def struct-big-fn-sig (native-signature :default big :int :double))
(def struct-big-fn-pointer (native-lookup module "struct_big"))
(defn struct-big-fn
  [i d]
  (native-call struct-big-fn-pointer struct-big-fn-sig i d))

#
# Call functions
#

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
  (def buf (native-write t value))
  (def same-value (native-read t buf))
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

(def s (native-struct :s8 :s8 :s8 :float))
(check-round-trip s [1 3 5 123.5])
(check-round-trip s [-1 -3 -5 -123.5])

(print "Done.")
