(do
	(:= a (set-class {} {"call" (fn [self a] (set self "last" a) (print self) self)}))
	(a 1)
	(a 2)
	(a 3)
)
