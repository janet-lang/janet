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
BINDIR=$(PREFIX)/bin

# CFLAGS=-std=c99 -Wall -Wextra -Isrc/include -Wl,--dynamic-list=src/exported.list -s -O3
# TODO - when api is finalized, only export public symbols instead of using rdynamic
# which exports all symbols.

CFLAGS=-std=c99 -Wall -Wextra -Isrc/include -rdynamic -s -O3
CLIBS=-lm -ldl
PREFIX=/usr/local
DST_TARGET=dst
DEBUGGER=gdb

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
DST_PARSER_SOURCES=$(sort $(wildcard src/parser/*.c))

all: $(DST_TARGET)

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

########################################
##### The main interpreter program #####
########################################

DST_ALL_SOURCES=$(DST_ASM_SOURCES) \
				$(DST_COMPILER_SOURCES) \
				$(DST_CONTEXT_SOURCES) \
				$(DST_CORE_SOURCES) \
				$(DST_MAINCLIENT_SOURCES) \
				$(DST_PARSER_SOURCES)

$(DST_TARGET): $(DST_ALL_SOURCES) $(DST_ALL_HEADERS)
	$(CC) $(CFLAGS) -o $(DST_TARGET) $(DST_ALL_SOURCES) $(CLIBS)

#######################
##### C Libraries #####
#######################

# DST_C_LIBS=$(addprefix libs/,testlib.so)

%.so: %.c $(DST_HEADERS)
	$(CC) $(CFLAGS) -DDST_LIB -shared -undefined dynamic_lookup -o $@ $<

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

#################
##### Other #####
#################

clean:
	rm $(DST_TARGET) || true
	rm src/**/*.o || true
	rm vgcore.* || true
	rm $(DST_GENERATED_HEADERS) || true

install: $(DST_TARGET)
	cp $(DST_TARGET) $(BINDIR)/dst

uninstall:
	rm $(BINDIR)/dst

.PHONY: clean install repl debug valgrind test valtest install uninstall
