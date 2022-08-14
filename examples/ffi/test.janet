#
# Simple FFI test script that tests against a simple shared object
#

(def is-windows (= :windows (os/which)))
(def ffi/loc (string "examples/ffi/so." (if is-windows "dll" "so")))
(def ffi/source-loc "examples/ffi/so.c")

(if is-windows
  (os/execute ["cl.exe" "/nologo" "/LD" ffi/source-loc "/link" "/DLL" (string "/OUT:" ffi/loc)] :px)
  (os/execute ["cc" ffi/source-loc "-shared" "-o" ffi/loc] :px))

(ffi/context ffi/loc)

(def intintint (ffi/struct :int :int :int))
(def big (ffi/struct :s64 :s64 :s64))

(ffi/defbind int-fn :int [a :int b :int])
(ffi/defbind double-fn :double [a :double b :double c :double])
(ffi/defbind double-many :double
  [x :double y :double z :double w :double a :double b :double])
(ffi/defbind double-lots :double
  [a :double b :double c :double d :double e :double f :double g :double h :double i :double j :double])
(ffi/defbind float-fn :double
  [x :float y :float z :float])
(ffi/defbind intint-fn :int
  [x :double ii [:int :int]])
(ffi/defbind return-struct [:int :int]
  [i :int])
(ffi/defbind intintint-fn :int
  [x :double iii intintint])
(ffi/defbind struct-big big
  [i :int d :double])
(ffi/defbind void-fn :void [])
(ffi/defbind double-lots-2 :double
  [a :double
   b :double
   c :double
   d :double
   e :double
   f :double
   g :double
   h :double
   i :double
   j :double])

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

#
# Call functions
#

(tracev (double-many 1 2 3 4 5 6))
(tracev (string/format "%.17g" (double-many 1 2 3 4 5 6)))
(tracev (type (double-many 1 2 3 4 5 6)))
(tracev (double-lots-2 0 1 2 3 4 5 6 7 8 9))
(tracev (void-fn))
(tracev (int-fn 10 20))
(tracev (double-fn 1.5 2.5 3.5))
(tracev (double-lots 1 2 3 4 5 6 7 8 9 10))
(tracev (float-fn 8 4 17))
(tracev (intint-fn 123.456 [10 20]))
(tracev (intintint-fn 123.456 [10 20 30]))
(tracev (return-struct 42))
(tracev (double-lots 1 2 3 4 5 6 700 800 9 10))
(tracev (struct-big 11 99.5))

(assert (= 9876543210 (double-lots-2 0 1 2 3 4 5 6 7 8 9)))
(assert (= 60 (int-fn 10 20)))
(assert (= 42 (double-fn 1.5 2.5 3.5)))
(assert (= 21 (math/round (double-many 1 2 3 4 5 6.01))))
(assert (= 19 (double-lots 1 2 3 4 5 6 7 8 9 10)))
(assert (= 204 (float-fn 8 4 17)))

(print "Done.")
