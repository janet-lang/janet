# DST

################################
##### Set global variables #####
################################

PREFIX?=/usr/local
BINDIR=$(PREFIX)/bin
VERSION=\"0.0.0-beta\"

CFLAGS=-std=c99 -Wall -Wextra -I./include -I./libs -g -DDST_VERSION=$(VERSION)
PREFIX=/usr/local
DST_TARGET=dst
DST_XXD=xxd
# Use gdb. On mac use lldb
DEBUGGER=lldb
DST_INTERNAL_HEADERS=$(addprefix core/, internal.h bootstrap.h cache.h)
DST_HEADERS=$(addprefix include/dst/, dst.h)

#############################
##### Generated headers #####
#############################
DST_LANG_SOURCES=$(addprefix libs/, bootstrap.dst)
DST_LANG_HEADERS=$(patsubst %.dst,%.gen.h,$(DST_LANG_SOURCES))

all: $(DST_TARGET)

#######################
##### Build tools #####
#######################
$(DST_XXD): libs/xxd.c
	$(CC) -o $(DST_XXD) libs/xxd.c

%.gen.h: %.dst $(DST_XXD)
	./$(DST_XXD) $< $@ $(basename $(notdir $<))

###################################
##### The core vm and runtime #####
###################################
DST_CORE_SOURCES=$(addprefix core/,\
				 util.c wrap.c buffer.c array.c table.c userdata.c func.c\
				 value.c vm.c ds.c gc.c thread.c serialize.c tuple.c\
				 string.c bootstrap_parse.c client.c cache.c struct.c)
DST_CORE_OBJECTS=$(patsubst %.c,%.o,$(DST_CORE_SOURCES))

$(DST_TARGET): $(DST_CORE_OBJECTS) $(DST_LANG_HEADERS)
	$(CC) $(CFLAGS) -o $(DST_TARGET) $(DST_CORE_OBJECTS)

# Compile all .c to .o
%.o: %.c $(DST_HEADERS) $(DST_INTERNAL_HEADERS) $(DST_LANG_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

run: $(DST_TARGET)
	@ ./$(DST_TARGET)

debug: $(DST_TARGET)
	@ $(DEBUGGER) ./$(DST_TARGET)

valgrind: $(DST_TARGET)
	@ valgrind --leak-check=full -v ./$(DST_TARGET)

clean:
	rm $(DST_TARGET) || true
	rm $(DST_CORE_OBJECTS) || true
	rm $(DST_LANG_HEADERS) || true
	rm vgcore.* || true
	rm $(DST_XXD) || true

test: $(DST_TARGET)
	@ ./$(DST_TARGET) dsttests/basic.dst

valtest: $(DST_TARGET)
	valgrind --leak-check=full -v ./$(DST_TARGET) dsttests/basic.dst

install: $(DST_TARGET)
	cp $(DST_TARGET) $(BINDIR)/dst

uninstall:
	rm $(BINDIR)/dst

.PHONY: clean install run debug valgrind test valtest install uninstall
