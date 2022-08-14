(ffi/context "user32.dll")

(ffi/defbind MessageBoxA :int
  [w :ptr text :string cap :string typ :int])

(MessageBoxA nil "Hello, World!" "Test" 0)

