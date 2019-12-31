# Creates an amalgamated janet.c

# Head
(print "/* Amalgamated build - DO NOT EDIT */")
(print "/* Generated from janet version " janet/version "-" janet/build " */")
(print "#define JANET_BUILD \"" janet/build "\"")
(print ```#define JANET_AMALG```)
(print ```#define _POSIX_C_SOURCE 200112L```)
(print ```#include "janet.h"```)

# Body
(each path (tuple/slice (dyn :args) 1)
  (print "\n/* " path " */\n")
  (print (slurp path)))

# maybe will help
(:flush stdout)
