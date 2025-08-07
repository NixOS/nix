#include "nix/store/references.hh"
#include "nix/store/path.hh"
#include "nix/util/base-nix-32.hh"

#include <benchmark/benchmark.h>

#include <random>

using namespace nix;

template<typename OIt>
static void randomReference(std::mt19937 & urng, OIt outIter)
{
    auto dist = std::uniform_int_distribution<std::size_t>(0, BaseNix32::characters.size() - 1);
    dist(urng);
    std::generate_n(outIter, StorePath::HashLen, [&]() { return BaseNix32::characters[dist(urng)]; });
}

/**
 * Generate a random byte sequence with interleaved
 *
 * @param charWeight relative frequency of a byte not belonging to a reference (hash part of the store path)
 */
static std::string
randomBytesWithReferences(std::mt19937 & urng, std::size_t size, double charWeight, StringSet & hashes)
{
    std::string res;
    res.reserve(size);

    /* NOTE: std::uniform_int_distribution isn't guaranteed to be implemented for char. */
    auto charGen = [&,
                    charDist = std::uniform_int_distribution<int>{
                        std::numeric_limits<char>::min(),
                        std::numeric_limits<char>::max(),
                    }]() mutable { res.push_back(charDist(urng)); };

    auto refGen = [&]() {
        std::string ref;
        randomReference(urng, std::back_inserter(ref));
        hashes.insert(ref);
        res += ref;
    };

    std::discrete_distribution<std::size_t> genDist{1.0, StorePath::HashLen * charWeight};

    while (res.size() < size) {
        auto c = genDist(urng);
        if (c == 0)
            refGen();
        else
            charGen();
    }

    res.resize(size);
    return res;
}

// Benchmark reference scanning
static void BM_RefScanSinkRandom(benchmark::State & state)
{
    auto size = state.range();
    auto chunkSize = 4199;

    std::mt19937 urng(0);
    StringSet hashes;
    auto bytes = randomBytesWithReferences(urng, size, /*charWeight=*/100.0, hashes);
    assert(hashes.size() > 0);

    std::size_t processed = 0;

    for (auto _ : state) {
        state.PauseTiming();
        RefScanSink Sink{StringSet(hashes)};
        state.ResumeTiming();

        auto data = std::string_view(bytes);
        while (!data.empty()) {
            auto chunk = data.substr(0, std::min<std::string_view::size_type>(chunkSize, data.size()));
            data = data.substr(chunk.size());
            Sink(chunk);
            processed += chunk.size();
        }

        benchmark::DoNotOptimize(Sink.getResult());
        state.PauseTiming();
        assert(Sink.getResult() == hashes);
        state.ResumeTiming();
    }

    state.SetBytesProcessed(processed);
}

BENCHMARK(BM_RefScanSinkRandom)->Arg(10'000)->Arg(100'000)->Arg(1'000'000)->Arg(5'000'000)->Arg(10'000'000);
