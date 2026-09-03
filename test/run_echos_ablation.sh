#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

repetitions="${1:-3}"
result_dir="test/echos_ablation_runs"
mkdir -p "$result_dir"

cmake --build build --target PerformanceProgram -j"${JOBS:-2}"
for run in $(seq 1 "$repetitions"); do
  ./build/test/PerformanceProgram --gtest_filter=Perf.EchosAblation
  cp test/echos_ablation_abs_table.csv "$result_dir/abs_run_${run}.csv"
  cp test/echos_ablation_rel_table.csv "$result_dir/rel_run_${run}.csv"
done

aggregate() {
  local family="$1"
  awk -F, '
    BEGIN {
      OFS=","
      print "Method,DataSet,CompressionRatio,CompressionTime(us/block)," \
            "DecompressionTime(us/block),DecisionMetadataRatio," \
            "DecisionMetadataBitsPerValue"
    }
    FNR == 1 { next }
    {
      key=$1 SUBSEP $2
      if (!(key in seen)) {
        seen[key]=1
        order[++n]=key
        method[key]=$1
        dataset[key]=$2
      }
      count[key]++
      for (column=3; column<=7; ++column) sum[key,column]+=$column
    }
    END {
      for (row=1; row<=n; ++row) {
        key=order[row]
        printf "%s,%s,%.8f,%.8f,%.8f,%.8f,%.8f\n", \
               method[key],dataset[key],sum[key,3]/count[key], \
               sum[key,4]/count[key],sum[key,5]/count[key], \
               sum[key,6]/count[key],sum[key,7]/count[key]
      }
    }
  ' "$result_dir/${family}_run_"*.csv > "test/echos_ablation_${family}_average.csv"
}

summarize() {
  local family="$1"
  awk -F, '
    BEGIN {
      OFS=","
      print "Method,AvgCompressionRatio,AvgCompressionTime(us/block)," \
            "AvgDecompressionTime(us/block),AvgDecisionMetadataRatio," \
            "AvgDecisionMetadataBitsPerValue"
    }
    NR > 1 {
      if (!seen[$1]++) order[++n]=$1
      count[$1]++
      for (column=3; column<=7; ++column) sum[$1,column]+=$column
    }
    END {
      for (row=1; row<=n; ++row) {
        method=order[row]
        printf "%s,%.8f,%.8f,%.8f,%.8f,%.8f\n",method, \
               sum[method,3]/count[method],sum[method,4]/count[method], \
               sum[method,5]/count[method],sum[method,6]/count[method], \
               sum[method,7]/count[method]
      }
    }
  ' "test/echos_ablation_${family}_average.csv" > "test/echos_ablation_${family}_summary.csv"
}

aggregate abs
aggregate rel
summarize abs
summarize rel
