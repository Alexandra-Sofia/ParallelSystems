#!/bin/bash -x
# bench_ex6.sh
REPEATS=4
for size in 10000000 100000000; do
  # Serial baseline
  total=0
  for r in $(seq 1 $REPEATS); do
    t=$(./ex6 $size serial 1 | grep "Sort time" | awk '{print $3}')
    total=$(echo "$total + $t" | bc -l)
  done
  avg=$(echo "scale=6; $total / $REPEATS" | bc -l)
  echo "size=$size mode=serial threads=1 avg=$avg"

  # Parallel sweep
  for threads in 2 4 8; do
    total=0
    for r in $(seq 1 $REPEATS); do
      t=$(./ex6 $size parallel $threads | grep "Sort time" | awk '{print $3}')
      total=$(echo "$total + $t" | bc -l)
    done
    avg=$(echo "scale=6; $total / $REPEATS" | bc -l)
    echo "size=$size mode=parallel threads=$threads avg=$avg"
  done
done