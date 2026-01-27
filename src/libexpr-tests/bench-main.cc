#include <benchmark/benchmark.h>

#include "nix/expr/eval-gc.hh"
#include "nix/store/globals.hh"

int main(int argc, char ** argv)
{
    nix::initLibStore(false);
    nix::initGC();

    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    return 0;
}
