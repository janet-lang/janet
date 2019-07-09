# Creates an amalgamated janet.c

# Head
(print "/* Amalgamated build - DO NOT EDIT */")
(print "/* Generated from janet version " janet/version "-" janet/build " */")
(print "#define JANET_BUILD \"" janet/build "\"")
(print ```#define JANET_AMALG```)
(print ```#include "janet.h"```)

# Body
(each path (tuple/slice process/args 2)
  (print (slurp path)))
