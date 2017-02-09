# TIL

CFLAGS=-std=c99 -Wall -Wextra -m32 -g

TARGET=interp
PREFIX=/usr/local

# C sources
SOURCES=main.c parse.c value.c vm.c dict.c array.c buffer.c gc.c compile.c
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

%.o : %.c
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

.PHONY: clean install run debug
