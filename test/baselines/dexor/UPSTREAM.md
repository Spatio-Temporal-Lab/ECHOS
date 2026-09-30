# DeXOR baseline

This directory vendors the Java implementation of DeXOR from:

- Repository: https://github.com/SuDIS-ZJU/DeXOR
- Commit: `06128b3df57cc1e8c53681231e235e22eb508c8d`
- License: MIT (see `LICENSE`)

The DeXOR encoding and decoding logic is kept unchanged. The local adapter
replaces the upstream file streams with API-compatible in-memory bit streams,
so the benchmark excludes filesystem I/O while preserving encoder and decoder
state across 50-value segments. `benchmark.DeXOROverallAdapter` follows the
same segment and metric definitions as `test/Perf.cc` and emits one DeXOR row
for each overall CSV.

The upstream validation checks recovery at the decimal precision represented
by each input value rather than requiring identical IEEE-754 payload bits. The
adapter retains that published-code criterion and reports both raw-bit mismatch
counts and maximum absolute errors in `test/dexor_overall_summary.csv`. These
diagnostics do not alter the CR, CT, or DT rows merged into the overall tables.

Run the adapter from the repository root with:

```bash
./test/run_dexor_overall.sh
```

The script requires OpenJDK 8, performs 400 warm-up segments per dataset, then
averages three measured passes. Re-running it replaces the existing DeXOR row,
so merging is idempotent.
