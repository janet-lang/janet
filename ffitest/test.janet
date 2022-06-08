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

#
# Call functions
#

(assert (= 60 (int-fn 10 20)))
(assert (= 42 (double-fn 1.5 2.5 3.5)))
(assert (= 21 (double-many 1 2 3 4 5 6)))
(assert (= 19 (double-lots 1 2 3 4 5 6 7 8 9 10)))
(assert (= 204 (float-fn 8 4 17)))

(print "Done.")
