#!/usr/bin/env bash

case "$2" in
c)
    rm temp.bin temp.o temp.nasm
    valgrind build/janet "$@" > temp.c
    gcc -nostdlib temp.c -c temp.o
    ;;
x64)
    rm temp.bin temp.o temp.nasm
    valgrind build/janet "$@" > temp.nasm
    nasm -felf64 temp.nasm -l temp.lst -o temp.o
    ;;
*)
    echo "Unknown mode $2"
    exit
    ;;
esac

ld -o temp.bin -dynamic-linker /lib64/ld-linux-x86-64.so.2 -lc temp.o
valgrind ./temp.bin
