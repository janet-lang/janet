set -eux

n=0
for tc in $(echo ./fuzz_out/$1/*/hangs/* ./fuzz_out/$1/*/crashes/*)
do
  if ! test -e $tc
  then
    continue
  fi
  mkdir -p ./fuzz_out/$1_aggregated/
  cp "$tc" $(printf "./fuzz_out/$1_aggregated/$1-%04d.test" $n)
  n=$((n + 1))
done
