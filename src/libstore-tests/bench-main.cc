#include <benchmark/benchmark.h>
#include "nix/store/globals.hh"

// Custom main to initialize Nix before running benchmarks
int main(int argc, char ** argv)
{
    // Initialize libstore
    nix::initLibStore(false);

    // Initialize and run benchmarks
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    return 0;
}
