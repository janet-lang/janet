# GST

######################################################
##### Set global variables for all gst Makefiles #####
######################################################
CFLAGS=-std=c99 -Wall -Wextra -Wpedantic -I./include -g
PREFIX=/usr/local
GST_TARGET=client/gst
GST_CORELIB=core/libgst.a
GST_INTERNAL_HEADERS=$(addprefix core/, cache.h)
GST_HEADERS=$(addprefix include/gst/, gst.h stl.h compile.h disasm.h parse.h)

all: $(GST_TARGET)

###################################
##### The core vm and runtime #####
###################################
GST_CORE_SOURCES=$(addprefix core/,\
				 compile.c disasm.c parse.c stl.c strings.c ids.c \
				 value.c vm.c ds.c gc.c thread.c serialize.c capi.c)
GST_CORE_OBJECTS=$(patsubst %.c,%.o,$(GST_CORE_SOURCES))
$(GST_CORELIB): $(GST_CORE_OBJECTS) $(GST_HEADERS)
	ar rcs $(GST_CORELIB) $(GST_CORE_OBJECTS)

##############################
##### The example client #####
##############################
GST_CLIENT_SOURCES=client/main.c
GST_CLIENT_OBJECTS=$(patsubst %.c,%.o,$(GST_CLIENT_SOURCES))
$(GST_TARGET): $(GST_CLIENT_OBJECTS) $(GST_CORELIB)
	$(CC) $(CFLAGS) -o $(GST_TARGET) $(GST_CLIENT_OBJECTS) $(GST_CORELIB)

# Compile all .c to .o
%.o : %.c $(GST_HEADERS) $(GST_INTERNAL_HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

run: $(GST_TARGET)
	./$(GST_TARGET)

debug: $(GST_TARGET)
	gdb $(GST_TARGET)

valgrind: $(GST_TARGET)
	valgrind ./$(GST_TARGET) --leak-check=full

clean:
	rm $(GST_TARGET) || true
	rm $(GST_CORELIB) || true
	rm $(GST_CORE_OBJECTS) || true
	rm $(GST_CLIENT_OBJECTS) || true
	rm vgcore.* || true

.PHONY: clean install run debug valgrind
