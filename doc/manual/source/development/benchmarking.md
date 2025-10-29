# Running Benchmarks

This guide explains how to build and run performance benchmarks in the Nix codebase.

## Overview

Nix uses the [Google Benchmark](https://github.com/google/benchmark) framework for performance testing. Benchmarks help measure and track the performance of critical operations like derivation parsing.

## Building Benchmarks

Benchmarks are disabled by default and must be explicitly enabled during the build configuration. For accurate results, use a debug-optimized release build.

### Development Environment Setup

First, enter the development shell which includes the necessary dependencies:

```bash
nix develop .#native-ccacheStdenv
```

### Configure Build with Benchmarks

From the project root, configure the build with benchmarks enabled and optimization:

```bash
cd build
meson configure -Dbenchmarks=true -Dbuildtype=debugoptimized
```

The `debugoptimized` build type provides:
- Compiler optimizations for realistic performance measurements
- Debug symbols for profiling and analysis
- Balance between performance and debuggability

### Build the Benchmarks

Build the project including benchmarks:

```bash
ninja
```

This will create benchmark executables in the build directory. Currently available:
- `build/src/libstore-tests/nix-store-benchmarks` - Store-related performance benchmarks

Additional benchmark executables will be created as more benchmarks are added to the codebase.

## Running Benchmarks

### Basic Usage

Run benchmark executables directly. For example, to run store benchmarks:

```bash
./build/src/libstore-tests/nix-store-benchmarks
```

As more benchmark executables are added, run them similarly from their respective build directories.

### Filtering Benchmarks

Run specific benchmarks using regex patterns:

```bash
# Run only derivation parser benchmarks
./build/src/libstore-tests/nix-store-benchmarks --benchmark_filter="derivation.*"

# Run only benchmarks for hello.drv
./build/src/libstore-tests/nix-store-benchmarks --benchmark_filter=".*hello.*"
```

### Output Formats

Generate benchmark results in different formats:

```bash
# JSON output
./build/src/libstore-tests/nix-store-benchmarks --benchmark_format=json > results.json

# CSV output
./build/src/libstore-tests/nix-store-benchmarks --benchmark_format=csv > results.csv
```

### Advanced Options

```bash
# Run benchmarks multiple times for better statistics
./build/src/libstore-tests/nix-store-benchmarks --benchmark_repetitions=10

# Set minimum benchmark time (useful for micro-benchmarks)
./build/src/libstore-tests/nix-store-benchmarks --benchmark_min_time=2

# Compare against baseline
./build/src/libstore-tests/nix-store-benchmarks --benchmark_baseline=baseline.json

# Display time in custom units
./build/src/libstore-tests/nix-store-benchmarks --benchmark_time_unit=ms
```

## Writing New Benchmarks

To add new benchmarks:

1. Create a new `.cc` file in the appropriate `*-tests` directory
2. Include the benchmark header:
   ```cpp
   #include <benchmark/benchmark.h>
   ```

3. Write benchmark functions:
   ```cpp
   static void BM_YourBenchmark(benchmark::State & state)
   {
       // Setup code here

       for (auto _ : state) {
           // Code to benchmark
       }
   }
   BENCHMARK(BM_YourBenchmark);
   ```

4. Add the file to the corresponding `meson.build`:
   ```meson
   benchmarks_sources = files(
       'your-benchmark.cc',
       # existing benchmarks...
   )
   ```

## Profiling with Benchmarks

For deeper performance analysis, combine benchmarks with profiling tools:

```bash
# Using Linux perf
perf record ./build/src/libstore-tests/nix-store-benchmarks
perf report
```

### Using Valgrind Callgrind

Valgrind's callgrind tool provides detailed profiling information that can be visualized with kcachegrind:

```bash
# Profile with callgrind
valgrind --tool=callgrind ./build/src/libstore-tests/nix-store-benchmarks

# Visualize the results with kcachegrind
kcachegrind callgrind.out.*
```

This provides:
- Function call graphs
- Instruction-level profiling
- Source code annotation
- Interactive visualization of performance bottlenecks

## Continuous Performance Testing

```bash
# Save baseline results
./build/src/libstore-tests/nix-store-benchmarks --benchmark_format=json > baseline.json

# Compare against baseline in CI
./build/src/libstore-tests/nix-store-benchmarks --benchmark_baseline=baseline.json
```

## Troubleshooting

### Benchmarks not building

Ensure benchmarks are enabled:
```bash
meson configure build | grep benchmarks
# Should show: benchmarks true
```

### Inconsistent results

- Ensure your system is not under heavy load
- Disable CPU frequency scaling for consistent results
- Run benchmarks multiple times with `--benchmark_repetitions`

## See Also

- [Google Benchmark documentation](https://github.com/google/benchmark/blob/main/docs/user_guide.md)
