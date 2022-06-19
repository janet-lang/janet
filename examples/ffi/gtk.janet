# :lazy true needed for jpm quickbin
# lazily loads library on first function use
# so the `main` function
# can be marshalled.
(ffi/context "/usr/lib/libgtk-3.so" :lazy true)

(ffi/defbind
  gtk-application-new :ptr
  "Add docstrings as needed."
  [title :string flags :uint])

(ffi/defbind
  g-signal-connect-data :ulong
  [a :ptr b :ptr c :ptr d :ptr e :ptr f :int])

(ffi/defbind
  g-application-run :int
  [app :ptr argc :int argv :ptr])

(ffi/defbind
  gtk-application-window-new :ptr
  [a :ptr])

(ffi/defbind
  gtk-button-new-with-label :ptr
  [a :ptr])

(ffi/defbind
  gtk-container-add :void
  [a :ptr b :ptr])

(ffi/defbind
  gtk-widget-show-all :void
  [a :ptr])

(ffi/defbind
  gtk-button-set-label :void
  [a :ptr b :ptr])

(def cb (delay (ffi/trampoline :default)))

(defn ffi/array
  ``Convert a janet array to a buffer that can be passed to FFI functions.
  For example, to create an array of type `char *` (array of c strings), one
  could use `(ffi/array ["hello" "world"] :ptr)`. One needs to be careful that
  array elements are not garbage collected though - the GC can't follow references
  inside an arbitrary byte buffer.``
  [arr ctype &opt buf]
  (default buf @"")
  (each el arr
    (ffi/write ctype el buf))
  buf)

(defn on-active
  [app]
  (def window (gtk-application-window-new app))
  (def btn (gtk-button-new-with-label "Click Me!"))
  (g-signal-connect-data btn "clicked" (cb)
                         (fn [btn] (gtk-button-set-label btn "Hello World"))
                         nil 1)
  (gtk-container-add window btn)
  (gtk-widget-show-all window))

(defn main
  [&]
  (def app (gtk-application-new "org.janet-lang.example.HelloApp" 0))
  (g-signal-connect-data app "activate" (cb) on-active nil 1)
  # manually build an array with ffi/write
  # - we are responsible for preventing gc when the arg array is used
  (def argv (ffi/array (dyn *args*) :string))
  (g-application-run app (length (dyn *args*)) argv))
