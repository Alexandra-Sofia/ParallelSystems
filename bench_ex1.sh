#!/bin/bash -x
# bench_ex1.sh
REPEATS=4
for degree in 100000 1000000; do
  for threads in 1 2 4 8; do
    for mode in pthreads openmp; do
      total=0
      for r in $(seq 1 $REPEATS); do
        t=$(./ex1 $degree $mode $threads | grep "Parallel time" | awk '{print $3}')
        total=$(echo "$total + $t" | bc -l)
      done
      avg=$(echo "scale=6; $total / $REPEATS" | bc -l)
      echo "degree=$degree mode=$mode threads=$threads avg_parallel=$avg"
    done
  done
  # Serial baseline (run once per degree, it's deterministic)
  t_serial=$(./ex1 $degree pthreads 1 | grep "Serial time" | awk '{print $3}')
  echo "degree=$degree mode=serial threads=1 time=$t_serial"
done