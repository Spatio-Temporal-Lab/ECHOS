#!/usr/bin/env bash
set -euo pipefail

dexor_test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dexor_repo_root="$(cd "${dexor_test_dir}/.." && pwd)"

"${dexor_test_dir}/baselines/dexor/build.sh"

java -Xms512m -Xmx512m \
  -cp "${dexor_repo_root}/build/dexor/classes" \
  benchmark.DeXOROverallAdapter \
  --data-dir "${dexor_test_dir}/data_set" \
  --output-dir "${dexor_test_dir}" \
  --merge-dir "${dexor_test_dir}" \
  "$@"
