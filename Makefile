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

PREFIX?=/usr

INCLUDEDIR=$(PREFIX)/include/dst
LIBDIR=$(PREFIX)/lib
BINDIR=$(PREFIX)/bin

# CFLAGS=-std=c99 -Wall -Wextra -Isrc/include -Wl,--dynamic-list=src/exported.list -s -O3
# TODO - when api is finalized, only export public symbols instead of using rdynamic
# which exports all symbols. Saves a few KB in binary.

CFLAGS=-std=c99 -Wall -Wextra -Isrc/include -fpic -Os -s
CLIBS=-lm -ldl
PREFIX=/usr/local
DST_TARGET=dst
DST_LIBRARY=libdst.so
DEBUGGER=gdb

UNAME:=$(shell uname -s)
LDCONFIG:=ldconfig
ifeq ($(UNAME), Darwin) 
	# Add other macos/clang flags
	LDCONFIG:=
else
	CFLAGS:=$(CFLAGS) -rdynamic
endif

# Source headers
DST_GENERATED_HEADERS=$(sort $(wildcard src/include/generated/*.h))
DST_HEADERS=$(sort $(wildcard src/include/dst/*.h))
DST_LOCAL_HEADERS=$(sort $(wildcard src/*/*.h))

# Source files
DST_CORE_SOURCES=$(sort $(wildcard src/core/*.c))
DST_MAINCLIENT_SOURCES=$(sort $(wildcard src/mainclient/*.c))

all: $(DST_TARGET) $(DST_LIBRARY)

###################################
##### The code generator tool #####
###################################

xxd: src/tools/xxd.c
	$(CC) $< -o $@

#############################
##### Generated Headers #####
#############################

src/include/generated/init.h: src/mainclient/init.dst xxd
	./xxd $< $@ dst_mainclient_init 

src/include/generated/boot.h: src/core/boot.dst xxd
	./xxd $< $@ dst_stl_bootstrap_gen

# Only a few files depend on the generated headers
src/core/corelib.o: src/include/generated/boot.h
src/mainclient/main.o: src/include/generated/init.h

##########################################################
##### The main interpreter program and shared object #####
##########################################################

DST_ALL_SOURCES=$(DST_CORE_SOURCES) \
				$(DST_MAINCLIENT_SOURCES)

DST_CORE_OBJECTS=$(patsubst %.c,%.o,$(DST_CORE_SOURCES))
DST_ALL_OBJECTS=$(patsubst %.c,%.o,$(DST_ALL_SOURCES))

%.o: %.c $(DST_HEADERS) $(DST_LOCAL_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

$(DST_TARGET): $(DST_ALL_OBJECTS)
	$(CC) $(CFLAGS) -o $(DST_TARGET) $^ $(CLIBS)

$(DST_LIBRARY): $(DST_CORE_OBJECTS)
	$(CC) $(CFLAGS) -shared -o $(DST_LIBRARY) $^ $(CLIBS)

###################
##### Testing #####
###################

repl: $(DST_TARGET)
	./$(DST_TARGET)

debug: $(DST_TARGET)
	$(DEBUGGER) ./$(DST_TARGET)

valgrind: $(DST_TARGET)
	valgrind --leak-check=full -v ./$(DST_TARGET)

test: $(DST_TARGET)
	./$(DST_TARGET) test/suite0.dst
	./$(DST_TARGET) test/suite1.dst

valtest: $(DST_TARGET)
	valgrind --leak-check=full -v ./$(DST_TARGET) test/suite0.dst
	valgrind --leak-check=full -v ./$(DST_TARGET) test/suite1.dst

###################
##### Natives #####
###################

natives: $(DST_TARGET)
	$(MAKE) -C natives/hello
	$(MAKE) -j 8 -C natives/sqlite3

#################
##### Other #####
#################

clean:
	-rm $(DST_TARGET)
	-rm src/**/*.o
	-rm vgcore.*
	-rm $(DST_GENERATED_HEADERS)

install: $(DST_TARGET)
	cp $(DST_TARGET) $(BINDIR)/$(DST_TARGET)
	mkdir -p $(INCLUDEDIR)
	cp $(DST_HEADERS) $(INCLUDEDIR)
	cp $(DST_LIBRARY) $(LIBDIR)/$(DST_LIBRARY)
	$(LDCONFIG)

uninstall:
	-rm $(BINDIR)/$(DST_TARGET)
	-rm $(LIBDIR)/$(DST_LIBRARY)
	-rm -rf $(INCLUDEDIR)
	$(LDCONFIG)

.PHONY: clean install repl debug valgrind test valtest install uninstall
