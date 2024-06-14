#!/usr/bin/env bash
build/janet examples/sysir/samples.janet > temp.nasm
nasm -felf64 temp.nasm -l temp.lst -o temp.o
ld -o temp.bin -dynamic-linker /lib64/ld-linux-x86-64.so.2 -lc temp.o
valgrind ./temp.bin
