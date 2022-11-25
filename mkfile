</$objtype/mkfile
<|mkdir -p build/^(core c boot)

TARG=janet
HFILES=src/include/janet.h src/conf/janetconf.h 
JANET_PATH=/sys/lib/janet
BIN=/$objtype/bin/
JANET_CONFIG=JANET_SINGLE_THREADED JANET_NO_DYNAMIC_MODULES JANET_NO_THREADS JANET_OS_NAME=9front JANET_ARCH_NAME=$objtype JANET_BUILD="9front" JANET_API='' JANET_NO_RETURN='' JANET_NO_EV JANET_NO_REALPATH JANET_NO_UTC_MKTIME JANET_SIMPLE_GETLINE
CFLAGS=-FTVBNcw -D _POSIX_SOURCE -D_PLAN9_SOURCE -D_BSD_EXTENSION -D_LIMITS_EXTENSION -Isrc/include -Isrc/conf -D_PLAN9_$objtype -D__plan9__
BOOT_CFLAGS=$CFLAGS -DJANET_BOOTSTRAP
CC=pcc
CLEANFILES=build/c/* build/boot/* build/core/*

JANET_LOCAL_HEADERS=src/core/features.h \
					src/core/util.h \
					src/core/state.h \
					src/core/gc.h \
					src/core/vector.h \
					src/core/fiber.h \
					src/core/regalloc.h \
					src/core/compile.h \
					src/core/emit.h \
					src/core/symcache.h

JANET_CORE_SOURCES=src/core/abstract.c \
				   src/core/array.c \
				   src/core/asm.c \
				   src/core/buffer.c \
				   src/core/bytecode.c \
				   src/core/capi.c \
				   src/core/cfuns.c \
				   src/core/compile.c \
				   src/core/corelib.c \
				   src/core/debug.c \
				   src/core/emit.c \
				   src/core/ev.c \
				   src/core/ffi.c \
				   src/core/fiber.c \
				   src/core/gc.c \
				   src/core/inttypes.c \
				   src/core/io.c \
				   src/core/marsh.c \
				   src/core/math.c \
				   src/core/net.c \
				   src/core/os.c \
				   src/core/parse.c \
				   src/core/peg.c \
				   src/core/pp.c \
				   src/core/regalloc.c \
				   src/core/run.c \
				   src/core/specials.c \
				   src/core/state.c \
				   src/core/string.c \
				   src/core/strtod.c \
				   src/core/struct.c \
				   src/core/symcache.c \
				   src/core/table.c \
				   src/core/tuple.c \
				   src/core/util.c \
				   src/core/value.c \
				   src/core/vector.c \
				   src/core/vm.c \
				   src/core/wrap.c

JANET_BOOT_SOURCES=src/boot/array_test.c \
				   src/boot/boot.c \
				   src/boot/buffer_test.c \
				   src/boot/number_test.c \
				   src/boot/system_test.c \
				   src/boot/table_test.c
JANET_BOOT_HEADERS=src/boot/tests.h
JANET_BOOT_OBJECTS=`{echo $JANET_CORE_SOURCES $JANET_BOOT_SOURCES | sed -e 's/\.c/.boot.o/g' -e 's$src/$build/$g'}

build/%.boot.o: src/%.c $JANET_HEADERS $JANET_LOCAL_HEADERS $JANET_BOOT_HEADERS
	$CC $BOOT_CFLAGS  -D^$JANET_CONFIG -o $target $prereq(1)

build/boot/$O.janet: $JANET_BOOT_OBJECTS
	$LD $LDFLAGS -o $target $prereq

build/c/janet.c: build/boot/$O.janet src/boot/boot.janet
	build/boot/$O.janet . JANET_PATH $JANET_PATH >$target

build/c/shell.c: src/mainclient/shell.c
	cp $prereq $target

$O.janet: build/janet.$O build/shell.$O
	$LD $LDFLAGS -o $target $prereq

build/janet.$O: build/c/janet.c src/conf/janetconf.h src/include/janet.h
	$CC $CFLAGS -D^$JANET_CONFIG -o $target $prereq(1)

build/shell.$O: src/mainclient/shell.c src/conf/janetconf.h src/include/janet.h
	$CC $CFLAGS -D^$JANET_CONFIG -o $target $prereq(1)
	

</sys/src/cmd/mkmany
