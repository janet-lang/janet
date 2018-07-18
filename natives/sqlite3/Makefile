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

CFLAGS=-std=c99 -Wall -Wextra -I../../src/include -O2 -shared -fpic \
	   -DSQLITE_THREADSAFE=0 \
	   -DSQLITE_OMIT_LOAD_EXTENSION
TARGET=sqlite3.so

# MacOS specifics
UNAME:=$(shell uname -s)
ifeq ($(UNAME), Darwin)
	CFLAGS:=$(CFLAGS) -undefined dynamic_lookup
endif

# Default target
all: $(TARGET)

OBJECTS:=main.o sqlite3.o
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

sqlite-autoconf-3230100/sqlite3.%:
	curl https://www.sqlite.org/2018/sqlite-autoconf-3230100.tar.gz | tar -xvz

sqlite3.c: sqlite-autoconf-3230100/sqlite3.c
	cp $< $@
sqlite3.h: sqlite-autoconf-3230100/sqlite3.h
	cp $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -rf sqlite-autoconf-3230100
	rm *.o
	rm sqlite3.c
	rm sqlite3.h
	rm $(TARGET)

install:
	cp $(TARGET) $(DST_PATH)

.PHONY: clean all
