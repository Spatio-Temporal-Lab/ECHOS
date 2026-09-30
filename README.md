# ECHOS: Error-Bounded Compression with Online Adaptation for Floating-Point Streams

ECHOS is a single-pass compressor for floating-point streams. It enforces pointwise absolute or relative error bounds and adapts residual coding to evolving data distributions. Encoder routing and parameter adaptation depend only on synchronized historical context, so the compressor and decompressor make the same decisions without transmitting per-value configuration metadata.

This repository contains the FP64 and FP32 implementations of ECHOS, correctness tests, the datasets used in the evaluation, and benchmark adapters for the compared methods.

## Highlights

- Pointwise absolute and relative error bounds through a unified mapping and quantization framework.
- Per-value online routing between complementary residual encoders.
- Online Golomb--Rice parameter adaptation without parameter search.
- Metadata-free synchronization of routing and parameter decisions.
- Native support for both 64-bit and 32-bit floating-point streams.

## Requirements

The primary supported environment is Linux. Windows users can build and run the project through WSL.

- CMake 3.15 or later
- A C++17 compiler, such as GCC or Clang
- Git
- GoogleTest development files
- A Rust toolchain for the native Buff baseline
- Network access during the first CMake configuration, which fetches the pinned SZ3 source
- OpenJDK 8 only for the optional DeXOR benchmark

Baseline switches are defined near the top of `test/CMakeLists.txt`. Set a baseline to `OFF` when its optional toolchain is unavailable.

## Build

Clone the repository and run the following commands from its root directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target serf_test PerformanceProgram -j4
```

The build produces the ECHOS library, the ECHOS correctness-test executable, and the main benchmark program.

## Correctness Tests

Run the FP64 and FP32 tests for both error models with:

```bash
./build/test/serf_test --gtest_filter='Correctness.Echos*'
```

## Reproducing the Experiments

Run all commands from the repository root because the benchmark uses paths relative to that directory.

### Overall performance

```bash
# FP64 under the absolute error bound
./build/test/PerformanceProgram --gtest_filter='Perf.Overall'

# FP64 under the relative error bound
./build/test/PerformanceProgram --gtest_filter='Perf.RelOverall'
```

### Sensitivity analysis

```bash
./build/test/PerformanceProgram \
  --gtest_filter='Perf.ParamAbsMaxDiff:Perf.Rel:Perf.ParamBlockSize'
```

### Ablation study

The following script runs each ablation experiment three times and generates averaged summaries:

```bash
./test/run_echos_ablation.sh 3
```

### Single-precision evaluation

```bash
./build/test/PerformanceProgram --gtest_filter='Perf.SinglePrecision'
```

Results are written as CSV files under `test/`. File suffixes identify the reported metric: `cr` is compression ratio, `ct` is compression time, and `dt` is decompression time. Timing is reported per configured data segment. Repeat performance runs under the same system conditions when computing averages.

### Optional DeXOR evaluation

DeXOR uses its official Java implementation and an independent benchmark adapter. When OpenJDK 8 is available, run:

```bash
cmake --build build --target DeXOROverall
```

The adapter performs the measured runs and merges the DeXOR row into the FP64 absolute-error result tables.

## Configuration and Datasets

The 13 evaluation datasets are stored in `test/data_set/`. Experiment parameters, including error bounds, segment sizes, method lists, and output paths, are defined in `test/Perf_expr_config.hpp`. The main experiment procedures are implemented in `test/Perf.cc`.

The repository contains third-party baselines under `test/baselines/`. These implementations are used only for evaluation and retain their original upstream terms and attribution.

## Project Structure

```text
.
├── src/
│   ├── compressor/          # FP64 ECHOS compressors
│   ├── decompressor/        # FP64 ECHOS decompressors
│   ├── compressor_32/       # FP32 ECHOS compressors
│   ├── decompressor_32/     # FP32 ECHOS decompressors
│   └── utils/               # Bitstream and adaptive-coding utilities
├── test/
│   ├── baselines/           # Baseline implementations and adapters
│   ├── data_set/            # Evaluation datasets
│   ├── unit_test/           # Correctness tests
│   ├── Perf.cc              # Benchmark procedures
│   └── Perf_expr_config.hpp # Experiment configuration
├── CMakeLists.txt
└── README.md
```
