#!/bin/bash -x
# bench_ex5.sh
REPEATS=4
ITERS=10

# Sweep 1: scale threads (fixed size and sparsity)
for threads in 1 2 4 8; do
  for size in 2000 4000; do
    total_csr=0; total_dense=0
    for r in $(seq 1 $REPEATS); do
      out=$(./ex5 $size 90 $ITERS $threads)
      tc=$(echo "$out" | grep "CSR SpMV" | awk '{print $3}')
      td=$(echo "$out" | grep "Dense SpMV" | awk '{print $3}')
      total_csr=$(echo "$total_csr + $tc" | bc -l)
      total_dense=$(echo "$total_dense + $td" | bc -l)
    done
    avg_csr=$(echo "scale=6; $total_csr / $REPEATS" | bc -l)
    avg_dense=$(echo "scale=6; $total_dense / $REPEATS" | bc -l)
    echo "threads=$threads size=$size sparsity=90 csr=$avg_csr dense=$avg_dense"
  done
done

# Sweep 2: vary sparsity (fixed threads=4, size=2000)
for sparsity in 0 50 75 90 99; do
  total_csr=0; total_dense=0; total_build=0
  for r in $(seq 1 $REPEATS); do
    out=$(./ex5 2000 $sparsity $ITERS 4)
    tc=$(echo "$out" | grep "CSR SpMV" | awk '{print $3}')
    td=$(echo "$out" | grep "Dense SpMV" | awk '{print $3}')
    tb=$(echo "$out" | grep "CSR build" | awk '{print $3}')
    total_csr=$(echo "$total_csr + $tc" | bc -l)
    total_dense=$(echo "$total_dense + $td" | bc -l)
    total_build=$(echo "$total_build + $tb" | bc -l)
  done
  avg_csr=$(echo "scale=6; $total_csr / $REPEATS" | bc -l)
  avg_dense=$(echo "scale=6; $total_dense / $REPEATS" | bc -l)
  avg_build=$(echo "scale=6; $total_build / $REPEATS" | bc -l)
  echo "sparsity=$sparsity build=$avg_build csr=$avg_csr dense=$avg_dense"
done