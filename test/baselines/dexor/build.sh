#!/usr/bin/env bash
set -euo pipefail

dexor_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
dexor_repo_root="$(cd "${dexor_script_dir}/../../.." && pwd)"
dexor_classes_dir="${dexor_repo_root}/build/dexor/classes"

mkdir -p "${dexor_classes_dir}"
find "${dexor_classes_dir}" -type f -delete

mapfile -t dexor_sources < <(find "${dexor_script_dir}/src/main/java" -name '*.java' -type f | sort)
javac -encoding UTF-8 -source 1.8 -target 1.8 -d "${dexor_classes_dir}" "${dexor_sources[@]}"
jar cf "${dexor_repo_root}/build/dexor/dexor-overall-adapter.jar" -C "${dexor_classes_dir}" .
