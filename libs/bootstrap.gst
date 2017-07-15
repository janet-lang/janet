
# This file is executed without any macro expansion (macros are not
# yet defined). Cannot use macros or anything outside the stl.

# Helper for macro expansion
(def macroexpander (fn [x env f]
	(if (= (type x) :tuple)
		(if (> (length x) 0)
			(do
				(def first (get x 0))
				(def macros (get env :macros))
				(if macros
					(do
						(def macro (get macros first))
						(if macro
							(f (apply macro (slice x 1)))
							x))
					x)
				x)
			x)
		x)))

# Macro expansion
(def macroexpand (fn [x env]
	(macroexpander x (if env env (getenv)) macroexpander)))

# Function to create macros
(def global-macro (fn [name f]
	(def env (getenv))
	(def env-macros (get env :macros))
	(def macros (if env-macros env-macros {}))
	(set! env :macros macros)
	(set! macros (symbol name) f)
	f))

# Make defn
(global-macro "defn" (fn [name &]
	(tuple 'def name (apply tuple 'fn &))))

# Make defmacro
(global-macro "defmacro" (fn [name &]
	(tuple global-macro (string name) (apply tuple 'fn &))))

# Comment returns nil
(global-macro "comment" (fn [] nil))

# The source file to read from
(var *sourcefile* stdin)

# The *read* macro gets the next form from the source file, and
# returns it. It is a var and therefor can be overwritten.
(var *read* (fn []
	(def b (buffer))
	(def p (parser))
	(while (not (parse-hasvalue p))
		(read *sourcefile* 1 b)
		(if (= (length b) 0)
			(error "parse error: unexpected end of source"))
		(parse-charseq p b)
		(if (= (parse-status p) :error)
			(error (string "parse error: " (parse-consume p))))
		(clear b))
	(parse-consume p)))

# Evaluates a form by macro-expanding it, compiling it, and
# then executing it.
(def eval (fn [x]
	(def func (compile (macroexpand x)))
	(if (= :function (type func))
		(func)
		(error (string "compiler error: " func)))))

# A simple repl for testing.
(while true
	(def t (thread (fn []
		(while true
			(print (eval (*read*)))))))
	(print (tran t)))
