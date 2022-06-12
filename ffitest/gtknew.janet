(ffi/context "/usr/lib/libgtk-3.so")

(ffi/defbind
  gtk-application-new :ptr
  "Add docstrings as needed."
  [a :ptr b :uint])

(ffi/defbind
  g-signal-connect-data :ulong
  [a :ptr b :ptr c :ptr d :ptr e :ptr f :int])

(ffi/defbind
  g-application-run :int
  [a :ptr b :int c :ptr])

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

(def cb (ffi/trampoline :default))

(defn on-active
  [app]
  (def window (gtk-application-window-new app))
  (def btn (gtk-button-new-with-label "Click Me!"))
  (g-signal-connect-data btn "clicked" cb
                         (fn [btn] (gtk-button-set-label btn "Hello World"))
                         nil 1)
  (gtk-container-add window btn)
  (gtk-widget-show-all window))

(defn main
  [&]
  (def app (gtk-application-new "org.janet-lang.example.HelloApp" 0))
  (g-signal-connect-data app "activate" cb on-active nil 1)
  (g-application-run app 0 nil))
