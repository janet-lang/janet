</$objtype/mkfile

TARG=janet
HFILES=src/include/janet.h src/conf/janetconf.h
JANET_PATH=/sys/lib/janet
BIN=/$objtype/bin/
JANET_CONFIG=JANET_SINGLE_THREADED JANET_NO_DYNAMIC_MODULES JANET_NO_THREADS JANET_OS_NAME=9front JANET_ARCH_NAME=$objtype JANET_BUILD="9front" JANET_API='' JANET_NO_RETURN='' JANET_NO_EV JANET_NO_REALPATH JANET_NO_UTC_MKTIME JANET_SIMPLE_GETLINE JANET_NO_FFI JANET_REDUCED_OS JANET_64 JANET_NO_ASSEMBLER
CFLAGS=-FTVBNcwp -D _POSIX_SOURCE -DJANET_PLAN9 -D_BSD_EXTENSION -D_LIMITS_EXTENSION -Isrc/include -Isrc/conf -I/sys/include/npe -Dtypestr=janettypestr -DJANET_API `{echo '-D'^$JANET_CONFIG}
BOOT_CFLAGS=$CFLAGS -DJANET_BOOTSTRAP
CLEANFILES=`{ls build/c/* build/boot/* build/core/*}y

list:
	 echo $CFLAGS

JANET_CORE_HEADERS=`{ls src/core/*.h}
JANET_CORE_SOURCES=`{ls src/core/*.c}

JANET_BOOT_SOURCES=src/boot/array_test.c \
				   src/boot/boot.c \
				   src/boot/buffer_test.c \
				   src/boot/number_test.c \
				   src/boot/system_test.c \
				   src/boot/table_test.c
JANET_BOOT_HEADERS=src/boot/tests.h
JANET_BOOT_OBJECTS=`{echo $JANET_CORE_SOURCES $JANET_BOOT_SOURCES | sed -e 's/\.c/.boot.o/g' -e 's$src/$build/$g'}

OFILES=build/janet.$O build/shell.$O

build/%.boot.o: src/%.c $JANET_HEADERS $JANET_CORE_HEADERS $JANET_BOOT_HEADERS
	$CC $BOOT_CFLAGS  -o $target $prereq(1)

build/core/%.$O: build/core

build/core:
	mkdir -p build/core

build/boot/%.$O: build/boot

build/boot:
	mkdir -p build/boot

build/boot/$O.janet: $JANET_BOOT_OBJECTS
	$LD $LDFLAGS -o $target $prereq

build/c/janet.c: build/boot/$O.janet src/boot/boot.janet
	build/boot/$O.janet . JANET_PATH $JANET_PATH >$target

build/c/shell.c: src/mainclient/shell.c
	cp $prereq $target


build/janet.$O: build/c/janet.c src/conf/janetconf.h src/include/janet.h
	$CC $CFLAGS -D^$JANET_CONFIG -o $target $prereq(1)

build/shell.$O: src/mainclient/shell.c src/conf/janetconf.h src/include/janet.h
	$CC $CFLAGS -D^$JANET_CONFIG -o $target $prereq(1)

</sys/src/cmd/mkone
