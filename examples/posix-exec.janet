# Switch to python

(print "running in Janet")
(os/posix-exec ["python"] :p)
(print "will not print")
