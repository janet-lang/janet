# Simpler iteration primitives example.

(defn- iter-for
  [prelude binding start end body]
  (def $end (gensym))
  (tuple 'do
         prelude
         (tuple 'var binding start)
         (tuple 'def $end end)
         (tuple 'while (tuple < binding $end)
                body
                (tuple '++ binding))))

(defn- iter-keys
  [prelude binding tab body]
  (tuple 'do
         prelude
         (tuple 'var binding (tuple next tab nil))
         (tuple 'while (tuple not= nil binding)
                body
                (tuple := binding (tuple next tab binding)))))

(defmacro do-range
  "Iterate over a half open integer range."
  [binding start end & body]
  (def $iter (gensym))
  (iter-for nil $iter start end
            (apply tuple 'do (tuple 'def binding $iter) body)))

(defmacro each
  "Iterate over an indexed data structure."
  [binding ind & body]
  (def $iter (gensym))
  (def $ind (gensym))
  (iter-for (tuple 'def $ind ind)
            $iter 0 (tuple length $ind)
            (apply tuple 'do (tuple 'def binding (tuple get $ind $iter)) body)))


(defmacro each-key
  "Iterate over keys of a table or structure."
  [binding tab & body]
  (def $tab (gensym))
  (def $key (gensym))
  (iter-keys
             (tuple 'def $tab tab)
             $key
             $tab
             (apply tuple 'do (tuple 'def binding $key) body)))

(defmacro each-value
  "Iterate over values of a table or structure."
  [binding tab & body]
  (def $tab (gensym))
  (def $key (gensym))
  (iter-keys
             (tuple 'def $tab tab)
             $key
             $tab
             (apply tuple 'do (tuple 'def binding (tuple 'get $tab $key)) body)))

(defmacro each-pair
  "Iterate over keys and values of a table or structure."
  [k v tab & body]
  (def $tab (gensym))
  (def $key (gensym))
  (iter-keys
             (tuple 'def $tab tab)
             $key
             $tab
             (apply tuple 'do 
                    (tuple 'def k $key)
                    (tuple 'def v (tuple 'get $tab $key)) body)))
