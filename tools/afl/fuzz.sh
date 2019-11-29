set -eux

NFUZZ=${NFUZZ:-1}
children=""

function finish {
  for pid in $children
  do
    set +e
    kill -s INT $pid
  done
  wait
}
trap finish EXIT

test -e ./tools/afl/$1_testcases
test -e ./tools/afl/$1_runner.janet

echo "running fuzz master..."
xterm -e \
  "afl-fuzz -i ./tools/afl/$1_testcases -o ./fuzz_out/$1 -M Fuzz$1_0 -- ./build/janet ./tools/afl/$1_runner.janet @@" &
children="$! $children"
echo "waiting for afl to get started before starting secondary fuzzers"
sleep 10

NFUZZ=$((NFUZZ - 1))

for N in $(seq $NFUZZ)
do
  xterm -e \
    "afl-fuzz -i ./tools/afl/$1_testcases -o ./fuzz_out/$1 -S Fuzz$1_$N -- ./build/janet ./tools/afl/$1_runner.janet @@" &
  children="$! $children"
done

echo "waiting for child terminals to exit."
wait
