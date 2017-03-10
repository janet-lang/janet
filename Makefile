# TIL

CFLAGS=-std=c99 -Wall -Wextra -Wpedantic -g

TARGET=interp
PREFIX=/usr/local

# C sources
HEADERS=vm.h ds.h compile.h parse.h value.h datatypes.h gc.h util.h gst.h stl.h disasm.h
SOURCES=main.c parse.c value.c vm.c ds.c compile.c gc.c stl.c disasm.c
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

%.o : %.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ -c $<

install: $(TARGET)
	cp $(TARGET) $(PREFIX)/bin

clean:
	rm $(TARGET) || true
	rm $(OBJECTS) || true

run: $(TARGET)
	./$(TARGET)

debug: $(TARGET)
	gdb $(TARGET)

valgrind: $(TARGET)
	valgrind ./$(TARGET) --leak-check=full

.PHONY: clean install run debug valgrind
