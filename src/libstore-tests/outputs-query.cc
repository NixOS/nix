// Regression tests for the functions in outputs-query.cc
//
// See https://github.com/NixOS/nix/issues/15713

#include <gtest/gtest.h>

#include "nix/store/outputs-query.hh"
#include "nix/store/derivations.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/realisation.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

class OutputsQueryTest : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        initLibStore(false);
    }

protected:
    EnableExperimentalFeature caFeature{"ca-derivations"};

    ref<DummyStore> store = [] {
        auto cfg = make_ref<DummyStoreConfig>(StoreReference::Params{});
        cfg->readOnly = false;
        return cfg->openDummyStore();
    }();

    static DerivationOutput caFloatingOutput()
    {
        return DerivationOutput{DerivationOutput::CAFloating{
            .method = ContentAddressMethod::Raw::NixArchive,
            .hashAlgo = HashAlgorithm::SHA256,
        }};
    }

    /**
     * Build a simple floating CA derivation with a given name and no input
     * derivations.
     */
    Derivation makeLeafDrv(std::string name)
    {
        Derivation drv;
        drv.name = std::move(name);
        drv.platform = "x86_64-linux";
        drv.builder = "/bin/sh";
        drv.outputs = {{"out", caFloatingOutput()}};
        return drv;
    }
};

/**
 * Regression test for https://github.com/NixOS/nix/issues/15713
 *
 * In a Fibonacci-style chain of floating CA derivations, the resolution
 * algorithm used to call queryRealisation O(Fib(N)) times.
 * This test verifies that memoization reduces this to O(N).
 */
TEST_F(OutputsQueryTest, fibonacciChainQueryCount)
{
    constexpr static size_t N = 10;
    std::vector<StorePath> drvPaths;

    // d0, d1: leaf derivations
    for (int i = 0; i < 2; ++i) {
        drvPaths.push_back(store->writeDerivation(makeLeafDrv("d" + std::to_string(i))));
    }

    // d_i depends on d_{i-1} and d_{i-2}
    for (size_t i = 2; i <= N; ++i) {
        Derivation drv = makeLeafDrv("d" + std::to_string(i));
        drv.inputDrvs.map[drvPaths[i - 1]].value.insert("out");
        drv.inputDrvs.map[drvPaths[i - 2]].value.insert("out");
        drvPaths.push_back(store->writeDerivation(drv));
    }

    // Tracker for queryRealisation calls.
    std::map<StorePath, int> callCounts;
    std::map<StorePath, StorePath> outPaths;

    QueryRealisationFun queryRealisation = [&](const DrvOutput & id) -> std::shared_ptr<const UnkeyedRealisation> {
        assert(id.outputName == "out");
        callCounts[id.drvPath]++;

        // Memoize mock output paths.
        auto it = outPaths.find(id.drvPath);
        if (it == outPaths.end()) {
            auto hash = hashString(HashAlgorithm::SHA1, "mock-output-" + std::to_string(outPaths.size()));
            it = outPaths.emplace(id.drvPath, StorePath(hash, "out")).first;
        }

        return std::make_shared<UnkeyedRealisation>(UnkeyedRealisation{.outPath = it->second});
    };

    auto result = deepQueryPartialDerivationOutput(*store, drvPaths[N], "out", nullptr, queryRealisation);

    ASSERT_TRUE(result);

    int totalCalls = 0;
    for (auto & [path, count] : callCounts) {
        totalCalls += count;
        if (count > 1)
            ADD_FAILURE() << "Derivation at " << store->printStorePath(path) << " was queried " << count
                          << " times (expected 1)";
    }

    // With full memoization (ResolveCache + RealisationCache), each derivation should be queried exactly once.
    EXPECT_EQ(totalCalls, N + 1) << "queryRealisation called " << totalCalls << " times; expected exactly " << (N + 1);
}

} // namespace nix
