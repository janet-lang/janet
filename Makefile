# TIL

CFLAGS=-std=c99 -Wall -Wextra -Wpedantic -g -O3

TARGET=interp
PREFIX=/usr/local

# C sources
HEADERS=vm.h ds.h compile.h parse.h value.h disasm.h datatypes.h gc.h thread.h
SOURCES=main.c parse.c value.c vm.c ds.c compile.c disasm.c gc.c thread.c
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
