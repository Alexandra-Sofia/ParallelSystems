#!/bin/bash -x
# bench_ex3.sh
REPEATS=4
THREADS=4
ACCOUNTS=1000
TXNS=10000

# Sweep 1: vary read percentage across all schemes
for read_pct in 0 20 50 80 95; do
  for scheme in coarse_mutex fine_mutex coarse_rw fine_rw; do
    total=0
    for r in $(seq 1 $REPEATS); do
      t=$(./ex3 $ACCOUNTS $TXNS $read_pct $scheme $THREADS | grep "Elapsed" | awk '{print $2}')
      total=$(echo "$total + $t" | bc -l)
    done
    avg=$(echo "scale=6; $total / $REPEATS" | bc -l)
    echo "read_pct=$read_pct scheme=$scheme avg=$avg"
  done
done

# Sweep 2: vary thread count at high read ratio (shows rwlock benefit)
for threads in 1 2 4 8; do
  for scheme in coarse_mutex fine_mutex coarse_rw fine_rw; do
    total=0
    for r in $(seq 1 $REPEATS); do
      t=$(./ex3 $ACCOUNTS $TXNS 80 $scheme $threads | grep "Elapsed" | awk '{print $2}')
      total=$(echo "$total + $t" | bc -l)
    done
    avg=$(echo "scale=6; $total / $REPEATS" | bc -l)
    echo "threads=$threads scheme=$scheme read_pct=80 avg=$avg"
  done
done