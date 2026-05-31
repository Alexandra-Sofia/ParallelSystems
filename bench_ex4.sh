#!/bin/bash -x
# bench_ex4.sh
REPEATS=4
for threads in 1 2 4 8; do
  for iters in 100000 1000000; do
    for mode in pthreads condvar sense; do
      total=0
      for r in $(seq 1 $REPEATS); do
        t=$(./ex4 $threads $iters $mode | grep "Elapsed" | awk '{print $2}')
        total=$(echo "$total + $t" | bc -l)
      done
      avg=$(echo "scale=6; $total / $REPEATS" | bc -l)
      echo "threads=$threads iters=$iters mode=$mode avg=$avg"
    done
  done
done