#include <benchmark/benchmark.h>
#include "nix/util/util.hh"

// Custom main to initialize Nix before running benchmarks
int main(int argc, char ** argv)
{
    // Initialize libutil
    nix::initLibUtil();

    // Initialize and run benchmarks
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    return 0;
}
