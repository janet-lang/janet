# Creates an amalgamated janet.c and janet.h to
# allow for easy embedding

(def {:year YY :month MM :month-day DD} (os/date))

(defn dofile
  "Print one file to stdout"
  [path]
  (print (slurp path)))

# Order is important here, as some headers
# depend on other headers.
(def headers
  @["src/core/util.h"
    "src/core/state.h"
    "src/core/gc.h"
    "src/core/vector.h"
    "src/core/fiber.h"
    "src/core/regalloc.h"
    "src/core/compile.h"
    "src/core/emit.h"
    "src/core/symcache.h"])

(def sources
  @["src/core/abstract.c"
    "src/core/array.c"
    "src/core/asm.c"
    "src/core/buffer.c"
    "src/core/bytecode.c"
    "src/core/capi.c"
    "src/core/cfuns.c"
    "src/core/compile.c"
    "src/core/corelib.c"
    "src/core/debug.c"
    "src/core/emit.c"
    "src/core/fiber.c"
    "src/core/gc.c"
    "src/core/io.c"
    "src/core/inttypes.c"
    "src/core/marsh.c"
    "src/core/math.c"
    "src/core/os.c"
    "src/core/parse.c"
    "src/core/peg.c"
    "src/core/pp.c"
    "src/core/regalloc.c"
    "src/core/run.c"
    "src/core/specials.c"
    "src/core/string.c"
    "src/core/strtod.c"
    "src/core/struct.c"
    "src/core/symcache.c"
    "src/core/table.c"
    "src/core/tuple.c"
    "src/core/typedarray.c"
    "src/core/util.c"
    "src/core/value.c"
    "src/core/vector.c"
    "src/core/vm.c"
    "src/core/wrap.c"])

(print "/* Amalgamated build - DO NOT EDIT */")
(print "/* Generated " YY "-" (inc MM) "-" (inc DD)
       " with janet version " janet/version "-" janet/build " */")

# Assume the version of janet used to run this script is the same
# as the version being generated
(print "#define JANET_BUILD \"" janet/build "\"")

(print ```#define JANET_AMALG```)
(print ```#include "janet.h"```)

(each h headers (dofile h))
(each s sources (dofile s))

# Relies on these files being built
(dofile "build/core_image.c")
