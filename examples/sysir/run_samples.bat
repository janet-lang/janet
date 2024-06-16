janet.exe examples/sysir/samples.janet > temp.nasm
nasm -fwin64 temp.nasm -l temp.lst -o temp.o
link temp.o legacy_stdio_definitions.lib msvcrt.lib /out:temp.exe
temp.exe
