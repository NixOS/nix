#include <benchmark/benchmark.h>
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/util/experimental-features.hh"
#include "nix/store/store-open.hh"
#include <fstream>
#include <sstream>

using namespace nix;

// Benchmark parsing real derivation files
static void BM_ParseRealDerivationFile(benchmark::State & state, const std::string & filename)
{
    // Read the file once
    std::ifstream file(filename);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    auto store = openStore("dummy://");
    ExperimentalFeatureSettings xpSettings;

    for (auto _ : state) {
        auto drv = parseDerivation(*store, std::string(content), "test", xpSettings);
        benchmark::DoNotOptimize(drv);
    }
    state.SetBytesProcessed(state.iterations() * content.size());
}

// Register benchmarks for actual test derivation files if they exist
BENCHMARK_CAPTURE(BM_ParseRealDerivationFile, hello, std::string(NIX_UNIT_TEST_DATA) + "/derivation/hello.drv");
BENCHMARK_CAPTURE(BM_ParseRealDerivationFile, firefox, std::string(NIX_UNIT_TEST_DATA) + "/derivation/firefox.drv");
