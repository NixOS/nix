#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/store/derivations.hh"
#include "nix/store/downstream-placeholder.hh"
#include "nix/store/parsed-derivations.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

class TryResolveTest : public LibStoreTest, public CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "derivation" / "try-resolve";

public:

    /**
     * A simple in-memory *derived* build trace. (e.g. we have
     * `SingleDerivedPath::Built` not `StorePath` keys, because that is
     * how the `tryResolved` callback works.)
     *
     * For a real-world build trace, derived build traces are much more
     * work to get correct, and should be at most a caching layer atop
     * an underlying source-of-truth build trace. (This is as described
     * in the manual.)
     *
     * However, this is just for some simple in-memory unit tests, with
     * tiny amounts of data we can hand-review, so it's fine. The point
     * is unit testing `tryResolve`, not unit testing "deep queries"
     * from a non-derived build trace (what is done in
     * `outputs-query.cc`) anyways --- that would be a separate unit
     * test.
     */
    struct BuildTrace
    {
        std::map<SingleDerivedPath::Built, StorePath> dict;

        bool operator==(const BuildTrace &) const = default;
    };

protected:

    EnableExperimentalFeature caFeature{"ca-derivations"};

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

    /**
     * Just here because we do this a few times in a tests.
     */
    static DerivationOutput caFloatingOutput()
    {
        return DerivationOutput{DerivationOutput::CAFloating{
            .method = ContentAddressMethod::Raw::NixArchive,
            .hashAlgo = HashAlgorithm::SHA256,
        }};
    }

    /**
     * Build a callback from a BuildTrace lookup.
     */
    static auto makeCallback(const BuildTrace & table)
    {
        return
            [&table](ref<const SingleDerivedPath> drvPath, const std::string & outputName) -> std::optional<StorePath> {
                if (auto p = get(table.dict, SingleDerivedPath::Built{drvPath, outputName}))
                    return *p;
                return std::nullopt;
            };
    }

    /**
     * Checkpoint before/buildTrace/after and assert the resolved derivation
     * matches expected.
     */
    void resolveExpect(
        std::string_view stem, const Derivation & drv, const BuildTrace & buildTrace, const BasicDerivation & expected)
    {
        nix::checkpointJson(*this, std::string{stem} + "-before", drv);
        nix::checkpointJson(*this, std::string{stem} + "-buildTrace", buildTrace);

        auto resolved = drv.tryResolve(*store, makeCallback(buildTrace));
        ASSERT_TRUE(resolved);

        nix::checkpointJson(*this, std::string{stem} + "-after", *resolved);

        EXPECT_EQ(*resolved, expected);
    }
};

} // namespace nix

JSON_IMPL(nix::TryResolveTest::BuildTrace);

namespace nlohmann {

using nix::SingleDerivedPath;
using nix::StorePath;
using nix::TryResolveTest;

void adl_serializer<TryResolveTest::BuildTrace>::to_json(json & j, const TryResolveTest::BuildTrace & t)
{
    j = t.dict;
}

TryResolveTest::BuildTrace adl_serializer<TryResolveTest::BuildTrace>::from_json(const json & j)
{
    return TryResolveTest::BuildTrace{
        .dict = j.get<std::map<SingleDerivedPath::Built, StorePath>>(),
    };
}

} // namespace nlohmann

namespace nix {

TEST_F(TryResolveTest, noInputs)
{
    resolveExpect(
        "no-inputs",
        [&] {
            Derivation drv;
            drv.name = "no-inputs";
            drv.platform = "x86_64-linux";
            drv.builder = "/bin/bash";
            drv.outputs = {{"out", caFloatingOutput()}};
            drv.env = {{"FOO", "bar"}};
            return drv;
        }(),
        {},
        [&] {
            BasicDerivation expected;
            expected.name = "no-inputs";
            expected.platform = "x86_64-linux";
            expected.builder = "/bin/bash";
            expected.outputs = {{"out", caFloatingOutput()}};
            expected.env = {{"FOO", "bar"}};
            return expected;
        }());
}

TEST_F(TryResolveTest, withInputs)
{
    // dep1 has two outputs (out, dev), dep2 has one (out)
    StorePath dep1DrvPath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-dep1.drv"};
    StorePath dep1OutPath{"f1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-dep1-out"};
    StorePath dep1DevPath{"j1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-dep1-dev"};
    StorePath dep2DrvPath{"h1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-dep2.drv"};
    StorePath dep2OutPath{"i1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-dep2-out"};

    auto placeholder1Out = DownstreamPlaceholder::unknownCaOutput(dep1DrvPath, "out").render();
    auto placeholder1Dev = DownstreamPlaceholder::unknownCaOutput(dep1DrvPath, "dev").render();
    auto placeholder2Out = DownstreamPlaceholder::unknownCaOutput(dep2DrvPath, "out").render();

    DerivationOutputs multiOutputs = {
        {"out", caFloatingOutput()},
        {"dev", caFloatingOutput()},
    };

    resolveExpect(
        "with-inputs",
        [&] {
            Derivation drv;
            drv.name = "with-inputs";
            drv.platform = "x86_64-linux";
            drv.builder = "/bin/bash";
            drv.outputs = multiOutputs;
            drv.inputDrvs = {
                .map = {
                    {dep1DrvPath, {.value = {"out", "dev"}}},
                    {dep2DrvPath, {.value = {"out"}}},
                }};
            drv.env = {
                {"DEP1_OUT", "prefix-" + placeholder1Out + "-suffix"},
                {"DEP1_DEV", placeholder1Dev},
                {"DEP2", placeholder2Out},
            };
            drv.structuredAttrs = StructuredAttrs{{
                {"dep1out", placeholder1Out},
                {"nested", nlohmann::json::object({{"dep2", "before " + placeholder2Out + " after"}})},
            }};
            return drv;
        }(),
        {.dict{
            {
                SingleDerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(dep1DrvPath),
                    .output = "out",
                },
                dep1OutPath,
            },
            {
                SingleDerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(dep1DrvPath),
                    .output = "dev",
                },
                dep1DevPath,
            },
            {
                SingleDerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(dep2DrvPath),
                    .output = "out",
                },
                dep2OutPath,
            },
        }},
        [&] {
            BasicDerivation expected;
            expected.name = "with-inputs";
            expected.platform = "x86_64-linux";
            expected.builder = "/bin/bash";
            expected.outputs = multiOutputs;
            expected.inputSrcs = {dep1OutPath, dep1DevPath, dep2OutPath};
            expected.env = {
                {"DEP1_OUT", "prefix-" + store->printStorePath(dep1OutPath) + "-suffix"},
                {"DEP1_DEV", store->printStorePath(dep1DevPath)},
                {"DEP2", store->printStorePath(dep2OutPath)},
            };
            expected.structuredAttrs = StructuredAttrs{{
                {"dep1out", store->printStorePath(dep1OutPath)},
                {"nested",
                 nlohmann::json::object({{"dep2", "before " + store->printStorePath(dep2OutPath) + " after"}})},
            }};
            return expected;
        }());
}

TEST_F(TryResolveTest, resolutionFailure)
{
    StorePath depDrvPath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-dep.drv"};

    Derivation drv;
    drv.name = "resolution-failure";
    drv.platform = "x86_64-linux";
    drv.builder = "/bin/bash";
    drv.outputs = {{"out", caFloatingOutput()}};
    drv.inputDrvs = {.map = {{depDrvPath, {.value = {"out"}}}}};

    BuildTrace buildTrace;

    checkpointJson(*this, "resolution-failure-before", drv);
    checkpointJson(*this, "resolution-failure-buildTrace", buildTrace);

    auto resolved = drv.tryResolve(*store, makeCallback(buildTrace));
    EXPECT_FALSE(resolved);
}

} // namespace nix
