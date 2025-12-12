</$objtype/mkfile

TARG=janet
HFILES=src/include/janet.h src/conf/janetconf.h
JANET_PATH=/sys/lib/janet
BIN=/$objtype/bin/
DISABLED=PROCESSES EV NET ASSEMBLER FFI UTC_MKTIME REALPATH DYNAMIC_MODULES THREADS SYMLINKS LOCALES UMASK
JANET_CONFIG=JANET_SINGLE_THREADED JANET_OS_NAME=plan9 JANET_ARCH_NAME=$objtype JANET_API='' JANET_NO_RETURN='' JANET_SIMPLE_GETLINE `{echo JANET_NO_^$DISABLED} JANET_REDUCED_OS PLAN9_`{echo -n $objtype}

CFLAGS=-FTVBNcwp -D _POSIX_SOURCE -DJANET_PLAN9 -D_BSD_EXTENSION -D_LIMITS_EXTENSION -Isrc/include -Isrc/conf -I/sys/include/npe -Dtypestr=janettypestr -DJANET_API `{echo '-D'^$JANET_CONFIG}
BOOT_CFLAGS=$CFLAGS -DJANET_BOOTSTRAP

JANET_CORE_HEADERS=`{ls src/core/*.h}
JANET_CORE_SOURCES=`{ls src/core/*.c}

JANET_BOOT_SOURCES=src/boot/array_test.c \
				   src/boot/boot.c \
				   src/boot/buffer_test.c \
				   src/boot/number_test.c \
				   src/boot/system_test.c \
				   src/boot/table_test.c
JANET_BOOT_HEADERS=src/boot/tests.h
JANET_BOOT_OBJECTS=`{echo $JANET_CORE_SOURCES $JANET_BOOT_SOURCES | sed -e 's/\.c/.boot.'$O'/g'}

OFILES=janet.$O src/mainclient/shell.$O

default:V:all

src/%.boot.$O: src/%.c $JANET_HEADERS $JANET_CORE_HEADERS $JANET_BOOT_HEADERS
	$CC $BOOT_CFLAGS  -o $target $prereq(1)

src/mainclient/shell.$O: src/mainclient/shell.c
	$CC $BOOT_CFLAGS  -o $target $prereq(1)

$O.janetboot: $JANET_BOOT_OBJECTS
	$LD $LDFLAGS -o $target $prereq

janet.c: $O.janetboot src/boot/boot.janet
	$prereq(1) . JANET_PATH $JANET_PATH >$target

build/janet.$O: build/c/janet.c src/conf/janetconf.h src/include/janet.h
	$CC $CFLAGS -D^$JANET_CONFIG -o $target $prereq(1)

build/shell.$O: src/mainclient/shell.c src/conf/janetconf.h src/include/janet.h
	$CC $CFLAGS -D^$JANET_CONFIG -o $target $prereq(1)

</sys/src/cmd/mkone

clean:V:
	rm -f src/core/*.[$OS] src/boot/*.[$OS] *.a[$OS] y.tab.? lex.yy.c y.debug y.output [$OS].??* $TARG janet.[$OS] janet.c src/mainclient/shell.[$OS]
