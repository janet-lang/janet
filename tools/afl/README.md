# AFL Fuzzing scripts

To use these, you need to install afl (of course), and xterm. A tiling window manager helps manage
many concurrent fuzzer instances.

## Fuzz the parser
```
$ sh ./tools/afl/prepare_to_fuzz.sh
export NFUZZ=1
$ sh ./tools/afl/fuzz.sh parser
Ctrl+C when done to close all fuzzer terminals.
$ sh ./tools/afl/aggregate_cases.sh parser
$ ls ./fuzz_out/parser_aggregated/
```