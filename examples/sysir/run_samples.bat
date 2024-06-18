janet.exe examples/sysir/windows_samples.janet > temp.nasm
nasm -fwin64 temp.nasm -l temp.lst -o temp.o
link /entry:Start /subsystem:windows kernel32.lib user32.lib temp.o /out:temp.exe
temp.exe
