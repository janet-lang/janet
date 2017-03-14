(do
	(:= a (set-class {} {"call" (fn [self a] (set self "last" a) (print self) self)}))
	(a 1)
	(a 2)
	(a 3)
)

# Run call-for-each test

(call-for-each print 1 2 3 4)

(call-for-each (fn [a] (print a "hi")) 1 2 3 45)
