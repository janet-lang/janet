# Copyright (c) 2021 Calvin Rose
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

################################
##### Set global variables #####
################################

PREFIX?=/usr/local

INCLUDEDIR?=$(PREFIX)/include
BINDIR?=$(PREFIX)/bin
LIBDIR?=$(PREFIX)/lib
JANET_BUILD?="\"$(shell git log --pretty=format:'%h' -n 1 2> /dev/null || echo local)\""
CLIBS=-lm -lpthread
JANET_TARGET=build/janet
JANET_LIBRARY=build/libjanet.so
JANET_STATIC_LIBRARY=build/libjanet.a
JANET_PATH?=$(LIBDIR)/janet
JANET_MANPATH?=$(PREFIX)/share/man/man1/
JANET_PKG_CONFIG_PATH?=$(LIBDIR)/pkgconfig
JANET_DIST_DIR?=janet-dist
JPM_TAG?=master
DEBUGGER=gdb
SONAME_SETTER=-Wl,-soname,

# For cross compilation
HOSTCC?=$(CC)
HOSTAR?=$(AR)
CFLAGS?=-O2
LDFLAGS?=-rdynamic

COMMON_CFLAGS:=-std=c99 -Wall -Wextra -Isrc/include -Isrc/conf -fvisibility=hidden -fPIC
BOOT_CFLAGS:=-DJANET_BOOTSTRAP -DJANET_BUILD=$(JANET_BUILD) -O0 -g $(COMMON_CFLAGS)
BUILD_CFLAGS:=$(CFLAGS) $(COMMON_CFLAGS)

# For installation
LDCONFIG:=ldconfig "$(LIBDIR)"

# Check OS
UNAME:=$(shell uname -s)
ifeq ($(UNAME), Darwin)
	CLIBS:=$(CLIBS) -ldl
	SONAME_SETTER:=-Wl,-install_name,
	LDCONFIG:=true
else ifeq ($(UNAME), Linux)
	CLIBS:=$(CLIBS) -lrt -ldl
endif
# For other unix likes, add flags here!
ifeq ($(UNAME), Haiku)
	LDCONFIG:=true
	LDFLAGS=-Wl,--export-dynamic
endif

$(shell mkdir -p build/core build/c build/boot)
all: $(JANET_TARGET) $(JANET_LIBRARY) $(JANET_STATIC_LIBRARY) build/janet.h

######################
##### Name Files #####
######################

JANET_HEADERS=src/include/janet.h src/conf/janetconf.h

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

##########################################################
##### The bootstrap interpreter that creates janet.c #####
##########################################################

JANET_BOOT_OBJECTS=$(patsubst src/%.c,build/%.boot.o,$(JANET_CORE_SOURCES) $(JANET_BOOT_SOURCES))

$(JANET_BOOT_OBJECTS): $(JANET_BOOT_HEADERS)

build/%.boot.o: src/%.c $(JANET_HEADERS) $(JANET_LOCAL_HEADERS) Makefile
	$(CC) $(BOOT_CFLAGS) -o $@ -c $<

build/janet_boot: $(JANET_BOOT_OBJECTS)
	$(CC) $(BOOT_CFLAGS) -o $@ $(JANET_BOOT_OBJECTS) $(CLIBS)

# Now the reason we bootstrap in the first place
build/c/janet.c: build/janet_boot src/boot/boot.janet
	build/janet_boot . JANET_PATH '$(JANET_PATH)' > $@
	cksum $@

########################
##### Amalgamation #####
########################

SONAME=libjanet.so.1.18

build/c/shell.c: src/mainclient/shell.c
	cp $< $@

build/janet.h: $(JANET_TARGET) src/include/janet.h src/conf/janetconf.h
	./$(JANET_TARGET) tools/patch-header.janet src/include/janet.h src/conf/janetconf.h $@

build/janetconf.h: src/conf/janetconf.h
	cp $< $@

build/janet.o: build/c/janet.c src/conf/janetconf.h src/include/janet.h
	$(HOSTCC) $(BUILD_CFLAGS) -c $< -o $@

build/shell.o: build/c/shell.c src/conf/janetconf.h src/include/janet.h
	$(HOSTCC) $(BUILD_CFLAGS) -c $< -o $@

$(JANET_TARGET): build/janet.o build/shell.o
	$(HOSTCC) $(LDFLAGS) $(BUILD_CFLAGS) -o $@ $^ $(CLIBS)

$(JANET_LIBRARY): build/janet.o build/shell.o
	$(HOSTCC) $(LDFLAGS) $(BUILD_CFLAGS) $(SONAME_SETTER)$(SONAME) -shared -o $@ $^ $(CLIBS)

$(JANET_STATIC_LIBRARY): build/janet.o build/shell.o
	$(HOSTAR) rcs $@ $^

###################
##### Testing #####
###################

# Testing assumes HOSTCC=CC

TEST_SCRIPTS=$(wildcard test/suite*.janet)

repl: $(JANET_TARGET)
	./$(JANET_TARGET)

debug: $(JANET_TARGET)
	$(DEBUGGER) ./$(JANET_TARGET)

VALGRIND_COMMAND=valgrind --leak-check=full

valgrind: $(JANET_TARGET)
	$(VALGRIND_COMMAND) ./$(JANET_TARGET)

test: $(JANET_TARGET) $(TEST_PROGRAMS)
	for f in test/suite*.janet; do ./$(JANET_TARGET) "$$f" || exit; done
	for f in examples/*.janet; do ./$(JANET_TARGET) -k "$$f"; done

valtest: $(JANET_TARGET) $(TEST_PROGRAMS)
	for f in test/suite*.janet; do $(VALGRIND_COMMAND) ./$(JANET_TARGET) "$$f" || exit; done
	for f in examples/*.janet; do ./$(JANET_TARGET) -k "$$f"; done

callgrind: $(JANET_TARGET)
	for f in test/suite*.janet; do valgrind --tool=callgrind ./$(JANET_TARGET) "$$f" || exit; done

########################
##### Distribution #####
########################

dist: build/janet-dist.tar.gz

build/janet-%.tar.gz: $(JANET_TARGET) \
	build/janet.h \
	janet.1 LICENSE CONTRIBUTING.md $(JANET_LIBRARY) $(JANET_STATIC_LIBRARY) \
	README.md build/c/janet.c build/c/shell.c
	mkdir -p build/$(JANET_DIST_DIR)/bin
	cp $(JANET_TARGET) build/$(JANET_DIST_DIR)/bin/
	mkdir -p build/$(JANET_DIST_DIR)/include
	cp build/janet.h build/$(JANET_DIST_DIR)/include/
	mkdir -p build/$(JANET_DIST_DIR)/lib/
	cp $(JANET_LIBRARY) $(JANET_STATIC_LIBRARY) build/$(JANET_DIST_DIR)/lib/
	mkdir -p build/$(JANET_DIST_DIR)/man/man1/
	mkdir -p build/$(JANET_DIST_DIR)/src/
	cp build/c/janet.c build/c/shell.c build/$(JANET_DIST_DIR)/src/
	cp CONTRIBUTING.md LICENSE README.md build/$(JANET_DIST_DIR)/
	cd build && tar -czvf ../$@ ./$(JANET_DIST_DIR)

#########################
##### Documentation #####
#########################

docs: build/doc.html

build/doc.html: $(JANET_TARGET) tools/gendoc.janet
	$(JANET_TARGET) tools/gendoc.janet > build/doc.html

########################
##### Installation #####
########################

.INTERMEDIATE: build/janet.pc
build/janet.pc: $(JANET_TARGET)
	echo 'prefix=$(PREFIX)' > $@
	echo 'exec_prefix=$${prefix}' >> $@
	echo 'includedir=$(INCLUDEDIR)/janet' >> $@
	echo 'libdir=$(LIBDIR)' >> $@
	echo "" >> $@
	echo "Name: janet" >> $@
	echo "Url: https://janet-lang.org" >> $@
	echo "Description: Library for the Janet programming language." >> $@
	$(JANET_TARGET) -e '(print "Version: " janet/version)' >> $@
	echo 'Cflags: -I$${includedir}' >> $@
	echo 'Libs: -L$${libdir} -ljanet' >> $@
	echo 'Libs.private: $(CLIBS)' >> $@

install: $(JANET_TARGET) $(JANET_LIBRARY) $(JANET_STATIC_LIBRARY) build/janet.pc build/janet.h
	mkdir -p '$(DESTDIR)$(BINDIR)'
	cp $(JANET_TARGET) '$(DESTDIR)$(BINDIR)/janet'
	mkdir -p '$(DESTDIR)$(INCLUDEDIR)/janet'
	cp -r build/janet.h '$(DESTDIR)$(INCLUDEDIR)/janet'
	mkdir -p '$(DESTDIR)$(JANET_PATH)'
	mkdir -p '$(DESTDIR)$(LIBDIR)'
	cp $(JANET_LIBRARY) '$(DESTDIR)$(LIBDIR)/libjanet.so.$(shell $(JANET_TARGET) -e '(print janet/version)')'
	cp $(JANET_STATIC_LIBRARY) '$(DESTDIR)$(LIBDIR)/libjanet.a'
	ln -sf $(SONAME) '$(DESTDIR)$(LIBDIR)/libjanet.so'
	ln -sf libjanet.so.$(shell $(JANET_TARGET) -e '(print janet/version)') $(DESTDIR)$(LIBDIR)/$(SONAME)
	mkdir -p '$(DESTDIR)$(JANET_MANPATH)'
	cp janet.1 '$(DESTDIR)$(JANET_MANPATH)'
	mkdir -p '$(DESTDIR)$(JANET_PKG_CONFIG_PATH)'
	cp build/janet.pc '$(DESTDIR)$(JANET_PKG_CONFIG_PATH)/janet.pc'
	[ -z '$(DESTDIR)' ] && $(LDCONFIG) || true

install-jpm-git: $(JANET_TARGET)
	mkdir -p build
	rm -rf build/jpm
	git clone --depth=1 --branch='$(JPM_TAG)' https://github.com/janet-lang/jpm.git build/jpm
	cd build/jpm && PREFIX='$(PREFIX)' \
		DESTDIR=$(DESTDIR) \
		JANET_MANPATH='$(JANET_MANPATH)' \
		JANET_HEADERPATH='$(INCLUDEDIR)/janet' \
		JANET_BINPATH='$(BINDIR)' \
		JANET_LIBPATH='$(LIBDIR)' \
		../../$(JANET_TARGET) ./bootstrap.janet

uninstall:
	-rm '$(DESTDIR)$(BINDIR)/janet'
	-rm -rf '$(DESTDIR)$(INCLUDEDIR)/janet'
	-rm -rf '$(DESTDIR)$(LIBDIR)'/libjanet.*
	-rm '$(DESTDIR)$(JANET_PKG_CONFIG_PATH)/janet.pc'
	-rm '$(DESTDIR)$(JANET_MANPATH)/janet.1'
	# -rm -rf '$(DESTDIR)$(JANET_PATH)'/* - err on the side of correctness here

#################
##### Other #####
#################

format:
	tools/format.sh

grammar: build/janet.tmLanguage
build/janet.tmLanguage: tools/tm_lang_gen.janet $(JANET_TARGET)
	$(JANET_TARGET) $< > $@

compile-commands:
	# Requires pip install copmiledb
	compiledb make

clean:
	-rm -rf build vgcore.* callgrind.*
	-rm -rf test/install/build test/install/modpath

test-install:
	echo "JPM has been removed from default install."

help:
	@echo
	@echo 'Janet: A Dynamic Language & Bytecode VM'
	@echo
	@echo Usage:
	@echo '   make            Build Janet'
	@echo '   make repl       Start a REPL from a built Janet'
	@echo
	@echo '   make test       Test a built Janet'
	@echo '   make valgrind   Assess Janet with Valgrind'
	@echo '   make callgrind  Assess Janet with Valgrind, using Callgrind'
	@echo '   make valtest    Run the test suite with Valgrind to check for memory leaks'
	@echo '   make dist       Create a distribution tarball'
	@echo '   make docs       Generate documentation'
	@echo '   make debug      Run janet with GDB or LLDB'
	@echo '   make install    Install into the current filesystem'
	@echo '   make uninstall  Uninstall from the current filesystem'
	@echo '   make clean      Clean intermediate build artifacts'
	@echo "   make format     Format Janet's own source files"
	@echo '   make grammar    Generate a TextMate language grammar'
	@echo

.PHONY: clean install repl debug valgrind test \
	valtest dist uninstall docs grammar format help compile-commands
