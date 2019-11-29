set -eux

export CC=afl-clang
make clean
make -j $(nproc) all
mkdir -p "./fuzz_out"
