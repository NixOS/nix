#include <benchmark/benchmark.h>

#include "nix/expr/eval-gc.hh"
#include "nix/store/globals.hh"

// Custom main to initialize Nix before running benchmarks. Locking a flake
// parses and evaluates its `flake.nix`, so we need both libstore and the
// garbage collector initialized (as in the libexpr benchmarks).
int main(int argc, char ** argv)
{
    nix::initLibStore(false);
    nix::initGC();

    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    return 0;
}
