# Copyright (c) 2017 Calvin Rose
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
VERSION=\"0.0.0-beta\"

CFLAGS=-std=c99 -Wall -Wextra -I./include -I./libs -g  -fsanitize=address -DDST_VERSION=$(VERSION)
PREFIX=/usr/local
DST_TARGET=dst
DST_XXD=xxd
DEBUGGER=lldb
DST_INTERNAL_HEADERS=$(addprefix core/,symcache.h opcodes.h strtod.h compile.h gc.h sourcemap.h)
DST_HEADERS=$(addprefix include/dst/,dst.h dstconfig.h dsttypes.h dststate.h dststl.h)

#############################
##### Generated headers #####
#############################

DST_ALL_HEADERS=$(DST_HEADERS) $(DST_INTERNAL_HEADERS)

all: $(DST_TARGET)

###################################
##### The core vm and runtime #####
###################################

DST_CORE_SOURCES=$(addprefix core/,\
				 abstract.c array.c asm.c buffer.c compile.c\
				 fiber.c func.c gc.c math.c parse.c sourcemap.c string.c\
				 stl.c strtod.c struct.c symcache.c table.c tuple.c util.c\
				 value.c vm.c wrap.c)

DST_CLIENT_SOURCES=$(addprefix client/,\
				   main.c)

$(DST_TARGET): $(DST_CORE_SOURCES) $(DST_CLIENT_SOURCES) $(DST_ALL_HEADERS)
	$(CC) $(CFLAGS) $(DST_CORE_SOURCES) $(DST_CLIENT_SOURCES) -o $(DST_TARGET)

######################
##### Unit Tests #####
######################

CCU_FLAGS = $(CFLAGS) -DDST_UNIT_TEST

DST_UNIT_BINARIES=$(addprefix unittests/,\
				  asm_test.out array_test.out buffer_test.out compile_test.out fiber_test.out \
				  parse_test.out strtod_test.out table_test.out)

%.out: %.c $(DST_CORE_OBJECTS) $(DST_ALL_HEADERS) unittests/unit.h
	$(CC) $(CCU_FLAGS) $(DST_CORE_OBJECTS) $< -o $@

unit: $(DST_UNIT_BINARIES)
	unittests/array_test.out
	unittests/asm_test.out
	unittests/buffer_test.out
	unittests/compile_test.out
	unittests/fiber_test.out
	unittests/parse_test.out
	unittests/strtod_test.out
	unittests/table_test.out

###################
##### Testing #####
###################

run: $(DST_TARGET)
	@ ./$(DST_TARGET)

debug: $(DST_TARGET)
	@ $(DEBUGGER) ./$(DST_TARGET)

valgrind: $(DST_TARGET)
	@ valgrind --leak-check=full -v ./$(DST_TARGET)

test: $(DST_TARGET)
	@ ./$(DST_TARGET) dsttests/basic.dst

valtest: $(DST_TARGET)
	valgrind --leak-check=full -v ./$(DST_TARGET) dsttests/basic.dst

#################
##### Other #####
#################

clean:
	rm $(DST_TARGET) || true
	rm *.o || true
	rm client/*.o || true
	rm core/*.o || true
	rm $(DST_LANG_HEADERS) || true
	rm vgcore.* || true
	rm unittests/*.out || true
	rm $(DST_XXD) || true

install: $(DST_TARGET)
	cp $(DST_TARGET) $(BINDIR)/dst

uninstall:
	rm $(BINDIR)/dst

.PHONY: clean install run debug valgrind test valtest install uninstall
