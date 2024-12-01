#!/usr/bin/env bash
valgrind build/janet examples/sysir/drawing.janet > temp.c
cc temp.c
./a.out > temp.bmp
feh temp.bmp
