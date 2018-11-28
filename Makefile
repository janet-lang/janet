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
.POSIX:

################################
##### Set global variables #####
################################

PREFIX?=/usr/local

INCLUDEDIR=$(PREFIX)/include/janet
LIBDIR=$(PREFIX)/lib
BINDIR=$(PREFIX)/bin
JANET_VERSION?="\"commit-$(shell git log --pretty=format:'%h' -n 1)\""

CFLAGS=-std=c99 -Wall -Wextra -Isrc/include -fpic -O2 -fvisibility=hidden \
	   -DJANET_VERSION=$(JANET_VERSION)
CLIBS=-lm -ldl
JANET_TARGET=janet
JANET_LIBRARY=libjanet.so
JANET_PATH?=/usr/local/lib/janet
DEBUGGER=gdb

# Some system specifics (for macOS)
UNAME:=$(shell uname -s)
LDCONFIG:=$(shell [ $(UNAME) != Darwin ] && echo "ldconfig")
CLIBS:=$(CLIBS) $(shell [ $(UNAME) != Darwin ] && echo "-lrt -rdynamic")

# Source headers
JANET_GENERATED_HEADERS= \
	src/include/generated/core.h \
 	src/include/generated/init.h
JANET_HEADERS=$(sort $(wildcard src/include/janet/*.h))
JANET_LOCAL_HEADERS=$(sort $(wildcard src/*/*.h))

# Source files
JANET_CORE_SOURCES=$(sort $(wildcard src/core/*.c))
JANET_MAINCLIENT_SOURCES=$(sort $(wildcard src/mainclient/*.c))
JANET_WEBCLIENT_SOURCES=$(sort $(wildcard src/webclient/*.c))

all: $(JANET_TARGET) $(JANET_LIBRARY)

###################################
##### The code generator tool #####
###################################

xxd: src/tools/xxd.c
	$(CC) $< -o $@

#############################
##### Generated Headers #####
#############################

src/include/generated/init.h: src/mainclient/init.janet xxd
	./xxd $< $@ janet_gen_init 

src/include/generated/webinit.h: src/webclient/webinit.janet xxd
	./xxd $< $@ janet_gen_webinit 

src/include/generated/core.h: src/core/core.janet xxd
	./xxd $< $@ janet_gen_core

# Only a few files depend on the generated headers
src/core/corelib.o: src/include/generated/core.h
src/mainclient/main.o: src/include/generated/init.h

##########################################################
##### The main interpreter program and shared object #####
##########################################################

JANET_ALL_SOURCES=$(JANET_CORE_SOURCES) \
				$(JANET_MAINCLIENT_SOURCES)

JANET_CORE_OBJECTS=$(patsubst %.c,%.o,$(JANET_CORE_SOURCES))
JANET_ALL_OBJECTS=$(patsubst %.c,%.o,$(JANET_ALL_SOURCES))

%.o: %.c $(JANET_HEADERS) $(JANET_LOCAL_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

$(JANET_TARGET): $(JANET_ALL_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

$(JANET_LIBRARY): $(JANET_CORE_OBJECTS)
	$(CC) $(CFLAGS) -shared -o $@ $^ $(CLIBS)

######################
##### Emscripten #####
######################

EMCC=emcc
EMCCFLAGS=-std=c99 -Wall -Wextra -Isrc/include -O2 \
		  -s EXTRA_EXPORTED_RUNTIME_METHODS='["cwrap"]' \
		  -s ALLOW_MEMORY_GROWTH=1 \
		  -s AGGRESSIVE_VARIABLE_ELIMINATION=1 \
		  -DJANET_VERSION=$(JANET_VERSION)
JANET_EMTARGET=janet.js
JANET_WEB_SOURCES=$(JANET_CORE_SOURCES) $(JANET_WEBCLIENT_SOURCES)
JANET_EMOBJECTS=$(patsubst %.c,%.bc,$(JANET_WEB_SOURCES))

# Only a few files depend on generated headers
src/core/corelib.bc: src/include/generated/core.h
src/webclient/main.bc: src/include/generated/webinit.h

%.bc: %.c $(JANET_HEADERS) $(JANET_LOCAL_HEADERS)
	$(EMCC) $(EMCCFLAGS) -o $@ -c $<

$(JANET_EMTARGET): $(JANET_EMOBJECTS)
	$(EMCC) $(EMCCFLAGS) -shared -o $@ $^

###################
##### Testing #####
###################

TEST_SOURCES=$(wildcard ctest/*.c)
TEST_OBJECTS=$(patsubst %.c,%.o,$(TEST_SOURCES))
TEST_PROGRAMS=$(patsubst %.c,%.out,$(TEST_SOURCES))

ctest/%.o: ctest/%.c $(JANET_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

ctest/%.out: ctest/%.o $(JANET_CORE_OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(CLIBS)

repl: $(JANET_TARGET)
	./$(JANET_TARGET)

debug: $(JANET_TARGET)
	$(DEBUGGER) ./$(JANET_TARGET)

valgrind: $(JANET_TARGET)
	valgrind --leak-check=full -v ./$(JANET_TARGET)

test: $(JANET_TARGET) $(TEST_PROGRAMS)
	ctest/system_test.out
	ctest/array_test.out
	ctest/buffer_test.out
	ctest/table_test.out
	./$(JANET_TARGET) test/suite0.janet
	./$(JANET_TARGET) test/suite1.janet
	./$(JANET_TARGET) test/suite2.janet
	./$(JANET_TARGET) test/suite3.janet

valtest: $(JANET_TARGET) $(TEST_PROGRAMS)
	valgrind --leak-check=full -v ctest/system_test.out
	valgrind --leak-check=full -v ctest/array_test.out
	valgrind --leak-check=full -v ctest/buffer_test.out
	valgrind --leak-check=full -v ctest/table_test.out
	valgrind --leak-check=full -v ./$(JANET_TARGET) test/suite0.janet
	valgrind --leak-check=full -v ./$(JANET_TARGET) test/suite1.janet
	valgrind --leak-check=full -v ./$(JANET_TARGET) test/suite2.janet
	valgrind --leak-check=full -v ./$(JANET_TARGET) test/suite3.janet

###################
##### Natives #####
###################

natives: $(JANET_TARGET)
	$(MAKE) -C natives/json
	$(MAKE) -j 8 -C natives/sqlite3

clean-natives:
	$(MAKE) -C natives/json clean
	$(MAKE) -C natives/sqlite3 clean

#################
##### Other #####
#################

clean:
	-rm $(JANET_TARGET)
	-rm $(JANET_LIBRARY)
	-rm ctest/*.o ctest/*.out
	-rm src/**/*.o src/**/*.bc vgcore.* *.js *.wasm *.html
	-rm $(JANET_GENERATED_HEADERS)

install: $(JANET_TARGET)
	mkdir -p $(BINDIR)
	cp $(JANET_TARGET) $(BINDIR)/$(JANET_TARGET)
	mkdir -p $(INCLUDEDIR)
	cp $(JANET_HEADERS) $(INCLUDEDIR)
	mkdir -p $(LIBDIR)
	cp $(JANET_LIBRARY) $(LIBDIR)/$(JANET_LIBRARY)
	cp janet.1 /usr/local/share/man/man1/
	mandb
	$(LDCONFIG)

install-libs: natives
	mkdir -p $(JANET_PATH)
	cp -r lib $(JANET_PATH)
	cp natives/*/*.so $(JANET_PATH)

uninstall:
	-rm $(BINDIR)/$(JANET_TARGET)
	-rm $(LIBDIR)/$(JANET_LIBRARY)
	-rm -rf $(INCLUDEDIR)
	$(LDCONFIG)

.PHONY: clean install repl debug valgrind test valtest install uninstall \
	$(TEST_PROGRAM_PHONIES) $(TEST_PROGRAM_VALPHONIES)
