# Copyright (c) 2018 Calvin Rose
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

INCLUDEDIR=$(PREFIX)/include/janet
LIBDIR=$(PREFIX)/lib
BINDIR=$(PREFIX)/bin
JANET_BUILD?="\"$(shell git log --pretty=format:'%h' -n 1)\""

CFLAGS=-std=c99 -Wall -Wextra -Isrc/include -fpic -O2 -fvisibility=hidden \
	   -DJANET_BUILD=$(JANET_BUILD)
CLIBS=-lm -ldl
JANET_TARGET=build/janet
JANET_LIBRARY=build/libjanet.so
JANET_PATH?=/usr/local/lib/janet
DEBUGGER=gdb

UNAME:=$(shell uname -s)
LDCONFIG:=ldconfig
ifeq ($(UNAME), Darwin)
	# Add other macos/clang flags
	LDCONFIG:=
else
	CFLAGS:=$(CFLAGS) -rdynamic
	CLIBS:=$(CLIBS) -lrt
endif

$(shell mkdir -p build/core build/mainclient build/webclient)

# Source headers
JANET_HEADERS=$(sort $(wildcard src/include/janet/*.h))
JANET_LOCAL_HEADERS=$(sort $(wildcard src/*/*.h))

# Source files
JANET_CORE_SOURCES=$(sort $(wildcard src/core/*.c))
JANET_MAINCLIENT_SOURCES=$(sort $(wildcard src/mainclient/*.c))
JANET_WEBCLIENT_SOURCES=$(sort $(wildcard src/webclient/*.c))

all: $(JANET_TARGET) $(JANET_LIBRARY)

##########################################################
##### The main interpreter program and shared object #####
##########################################################

JANET_CORE_OBJECTS=$(patsubst src/%.c,build/%.o,$(JANET_CORE_SOURCES)) build/core.gen.o
JANET_MAINCLIENT_OBJECTS=$(patsubst src/%.c,build/%.o,$(JANET_MAINCLIENT_SOURCES)) build/init.gen.o

%.gen.o: %.gen.c
	$(CC) $(CFLAGS) -o $@ -c $<

build/%.o: src/%.c $(JANET_HEADERS) $(JANET_LOCAL_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

$(JANET_TARGET): $(JANET_CORE_OBJECTS) $(JANET_MAINCLIENT_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

$(JANET_LIBRARY): $(JANET_CORE_OBJECTS)
	$(CC) $(CFLAGS) -shared -o $@ $^ $(CLIBS)

######################
##### Emscripten #####
######################

EMCC=emcc
EMCFLAGS=-std=c99 -Wall -Wextra -Isrc/include -O2 \
		  -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]' \
		  -s ALLOW_MEMORY_GROWTH=1 \
		  -s AGGRESSIVE_VARIABLE_ELIMINATION=1 \
		  -DJANET_BUILD=$(JANET_BUILD)
JANET_EMTARGET=build/janet.js
JANET_WEB_SOURCES=$(JANET_CORE_SOURCES) $(JANET_WEBCLIENT_SOURCES)
JANET_EMOBJECTS=$(patsubst src/%.c,build/%.bc,$(JANET_WEB_SOURCES)) \
				build/webinit.gen.bc build/core.gen.bc

%.gen.bc: %.gen.c
	$(EMCC) $(EMCFLAGS) -o $@ -c $<

build/%.bc: src/%.c $(JANET_HEADERS) $(JANET_LOCAL_HEADERS)
	$(EMCC) $(EMCFLAGS) -o $@ -c $<

$(JANET_EMTARGET): $(JANET_EMOBJECTS)
	$(EMCC) $(EMCFLAGS) -shared -o $@ $^

emscripten: $(JANET_EMTARGET)

#############################
##### Generated C files #####
#############################

build/xxd: tools/xxd.c
	$(CC) $< -o $@

build/core.gen.c: src/core/core.janet build/xxd
	build/xxd $< $@ janet_gen_core
build/init.gen.c: src/mainclient/init.janet build/xxd
	build/xxd $< $@ janet_gen_init
build/webinit.gen.c: src/webclient/webinit.janet build/xxd
	build/xxd $< $@ janet_gen_webinit

###################
##### Testing #####
###################

TEST_SOURCES=$(wildcard ctest/*.c)
TEST_PROGRAMS=$(patsubst ctest/%.c,build/%.out,$(TEST_SOURCES))
TEST_SCRIPTS=$(wildcard test/suite*.janet)

build/%.out: ctest/%.c $(JANET_CORE_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

repl: $(JANET_TARGET)
	./$(JANET_TARGET)

debug: $(JANET_TARGET)
	$(DEBUGGER) ./$(JANET_TARGET)

VALGRIND_COMMAND=valgrind --leak-check=full

valgrind: $(JANET_TARGET)
	$(VALGRIND_COMMAND) ./$(JANET_TARGET)

test: $(JANET_TARGET) $(TEST_PROGRAMS)
	for f in build/*.out; do "$$f" || exit; done
	for f in test/*.janet; do ./$(JANET_TARGET) "$$f" || exit; done

valtest: $(JANET_TARGET) $(TEST_PROGRAMS)
	for f in build/*.out; do $(VALGRIND_COMMAND) "$$f" || exit; done
	for f in test/*.janet; do $(VALGRIND_COMMAND) ./$(JANET_TARGET) "$$f" || exit; done

callgrind: $(JANET_TARGET)
	for f in test/*.janet; do valgrind --tool=callgrind ./$(JANET_TARGET) "$$f" || exit; done

########################
##### Distribution #####
########################

dist: build/janet-dist.tar.gz

build/janet-%.tar.gz: $(JANET_TARGET) src/include/janet/janet.h \
	janet.1 LICENSE CONTRIBUTING.md $(JANET_LIBRARY) \
	build/doc.html README.md
	tar -czvf $@ $^

#########################
##### Documentation #####
#########################

docs: build/doc.html

build/doc.html: $(JANET_TARGET) tools/gendoc.janet
	$(JANET_TARGET) tools/gendoc.janet > build/doc.html

#################
##### Other #####
#################

grammar: build/janet.tmLanguage
build/janet.tmLanguage: tools/tm_lang_gen.janet $(JANET_TARGET)
	$(JANET_TARGET) $< > $@

clean:
	-rm -rf build vgcore.* callgrind.*

install: $(JANET_TARGET)
	mkdir -p $(BINDIR)
	cp $(JANET_TARGET) $(BINDIR)/janet
	mkdir -p $(INCLUDEDIR)
	cp $(JANET_HEADERS) $(INCLUDEDIR)
	mkdir -p $(LIBDIR)
	cp $(JANET_LIBRARY) $(LIBDIR)/libjanet.so
	mkdir -p $(JANET_PATH)
	cp tools/cook.janet $(JANET_PATH)
	cp tools/highlight.janet $(JANET_PATH)
	cp janet.1 /usr/local/share/man/man1/
	mandb
	$(LDCONFIG)

uninstall:
	-rm $(BINDIR)/../$(JANET_TARGET)
	-rm $(LIBDIR)/../$(JANET_LIBRARY)
	-rm -rf $(INCLUDEDIR)
	$(LDCONFIG)

.PHONY: clean install repl debug valgrind test \
	valtest emscripten dist uninstall docs grammar \
	$(TEST_PROGRAM_PHONIES) $(TEST_PROGRAM_VALPHONIES)
