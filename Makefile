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

CFLAGS=-std=c99 -Wall -Wextra -Isrc/include -fpic -O2
CLIBS=-lm -ldl
PREFIX=/usr/local
DST_TARGET=dst
DST_LIBRARY=libdst.so
DEBUGGER=gdb

UNAME:=$(shell uname -s)
ifeq ($(UNAME), Darwin) 
	# Add other macos/clang flags
else
	CFLAGS:=$(CFLAGS) -rdynamic
endif

# Source headers
DST_HEADERS=$(sort $(wildcard src/include/dst/*.h))
DST_LIBHEADERS=$(sort $(wildcard src/include/headerlibs/*.h))
DST_GENERATED_HEADERS=src/mainclient/clientinit.gen.h \
					  src/compiler/dststlbootstrap.gen.h
DST_ALL_HEADERS=$(DST_HEADERS) \
				$(DST_LIB_HEADERS) \
				$(DST_GENERATED_HEADERS)

# Source files
DST_ASM_SOURCES=$(sort $(wildcard src/assembler/*.c))
DST_COMPILER_SOURCES=$(sort $(wildcard src/compiler/*.c))
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

src/mainclient/clientinit.gen.h: src/mainclient/init.dst xxd
	./xxd $< $@ dst_mainclient_init 

src/compiler/dststlbootstrap.gen.h: src/compiler/boot.dst xxd
	./xxd $< $@ dst_stl_bootstrap_gen

##########################################################
##### The main interpreter program and shared object #####
##########################################################

DST_LIB_SOURCES=$(DST_ASM_SOURCES) \
				$(DST_COMPILER_SOURCES) \
				$(DST_CONTEXT_SOURCES) \
				$(DST_CORE_SOURCES)

DST_ALL_SOURCES=$(DST_LIB_SOURCES) \
				$(DST_MAINCLIENT_SOURCES)

DST_LIB_OBJECTS=$(patsubst %.c,%.o,$(DST_LIB_SOURCES))
DST_ALL_OBJECTS=$(patsubst %.c,%.o,$(DST_ALL_SOURCES))

%.o: %.c $(DST_ALL_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

$(DST_TARGET): $(DST_ALL_OBJECTS)
	$(CC) $(CFLAGS) -o $(DST_TARGET) $^ $(CLIBS)

$(DST_LIBRARY): $(DST_LIB_OBJECTS)
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
	-ldconfig

uninstall:
	-rm $(BINDIR)/$(DST_TARGET)
	-rm $(LIBDIR)/$(DST_LIBRARY)
	-rm -rf $(INCLUDEDIR)
	-ldconfig

.PHONY: clean install repl debug valgrind test valtest install uninstall
