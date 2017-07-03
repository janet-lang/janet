# GST

######################################################
##### Set global variables for all gst Makefiles #####
######################################################

PREFIX?=/usr/local
BINDIR=$(PREFIX)/bin
VERSION=\"0.0.0-beta\"

CFLAGS=-std=c99 -Wall -Wextra -I./include -g -DGST_VERSION=$(VERSION)
PREFIX=/usr/local
GST_TARGET=gst
GST_CORELIB=core/libgst.a
GST_INTERNAL_HEADERS=$(addprefix core/, cache.h)
GST_HEADERS=$(addprefix include/gst/, gst.h)

all: $(GST_TARGET)

###################################
##### The core vm and runtime #####
###################################
GST_CORE_SOURCES=$(addprefix core/,\
				 compile.c parse.c stl.c ids.c util.c\
				 value.c vm.c ds.c gc.c thread.c serialize.c)
GST_CORE_OBJECTS=$(patsubst %.c,%.o,$(GST_CORE_SOURCES))

######################
##### The client #####
######################
GST_CLIENT_SOURCES=client/main.c
GST_CLIENT_OBJECTS=$(patsubst %.c,%.o,$(GST_CLIENT_SOURCES))
$(GST_TARGET): $(GST_CLIENT_OBJECTS) $(GST_CORE_OBJECTS)
	$(CC) $(CFLAGS) -o $(GST_TARGET) $(GST_CLIENT_OBJECTS) $(GST_CORE_OBJECTS)

# Compile all .c to .o
%.o : %.c $(GST_HEADERS) $(GST_INTERNAL_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

run: $(GST_TARGET)
	@ ./$(GST_TARGET)

debug: $(GST_TARGET)
	@ gdb ./$(GST_TARGET)

valgrind: $(GST_TARGET)
	@ valgrind --leak-check=full -v ./$(GST_TARGET)

clean:
	rm $(GST_TARGET) || true
	rm $(GST_CORELIB) || true
	rm $(GST_CORE_OBJECTS) || true
	rm $(GST_CLIENT_OBJECTS) || true
	rm vgcore.* || true

test: $(GST_TARGET)
	@ ./$(GST_TARGET) gsttests/basic.gst

valtest: $(GST_TARGET)
	valgrind --leak-check=full -v ./$(GST_TARGET) gsttests/basic.gst

install: $(GST_TARGET)
	cp $(GST_TARGET) $(BINDIR)/gst

uninstall:
	rm $(BINDIR)/gst

.PHONY: clean install run debug valgrind test valtest install uninstall
