# FFI is best used with a wrapper like the one below
# An even more sophisticated macro wrapper could add
# better doc strings, better parameter checking, etc.

(defn ffi-context
  "Load a dynamic library and set it as the context for following declarations"
  [location]
  (setdyn :ffi-context (ffi/native location)))

(defmacro defnative
  "Declare a native binding"
  [name ret-type & body]
  (def signature-args (last body))
  (def defn-args (seq [_ :in signature-args] (gensym)))
  (def raw-symbol (string/replace-all "-" "_" name))
  (def $sig (symbol name "-signature-"))
  (def $pointer (symbol name "-raw-pointer-"))
  ~(upscope
     (def ,$pointer :private (as-macro ,assert (,ffi/lookup (,dyn :ffi-context) ,raw-symbol)))
     (def ,$sig :private (,ffi/signature :default ,ret-type ,;signature-args))
     (defn ,name [,;defn-args]
       (,ffi/call ,$pointer ,$sig ,;defn-args))))

(ffi-context "/usr/lib/libgtk-3.so")

(defnative gtk-application-new :ptr [:ptr :uint])
(defnative g-signal-connect-data :ulong [:ptr :ptr :ptr :ptr :ptr :int])
(defnative g-application-run :int [:ptr :int :ptr])
(defnative gtk-application-window-new :ptr [:ptr])
(defnative gtk-button-new-with-label :ptr [:ptr])
(defnative gtk-container-add :void [:ptr :ptr])
(defnative gtk-widget-show-all :void [:ptr])
(defnative gtk-button-set-label :void [:ptr :ptr])

# GTK follows a strict convention for callbacks. This lets us use
# a single "standard" callback whose behavior is specified by userdata.
# This lets use callbacks without code generation, so no issues with iOS, SELinux, etc.
# Limitation is that we cannot generate arbitrary closures to pass into apis.
# However, any stubs we need we would simply need to compile ourselves, so
# Janet includes a common stub out of the box.
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
