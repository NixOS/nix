#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/store/derivations.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/util/tests/json-characterization.hh"

#include "derivation/test-support.hh"

namespace nix {

class FillInOutputPathsTest : public LibStoreTest, public JsonCharacterizationTest<Derivation>
{
    std::filesystem::path unitTestData = getUnitTestData() / "derivation" / "invariants";

protected:
    FillInOutputPathsTest()
        : LibStoreTest([](auto & settings) {
            auto config = make_ref<DummyStoreConfig>(settings, DummyStoreConfig::Params{});
            config->readOnly = false;
            return config->openDummyStore();
        })
    {
    }

    /**
     * Create a CA floating output derivation and write it to the store.
     * This is useful for creating dependencies that will cause downstream
     * derivations to remain deferred.
     */
    StorePath makeCAFloatingDependency(std::string_view name)
    {
        Derivation depDrv;
        depDrv.name = name;
        depDrv.platform = "x86_64-linux";
        depDrv.builder = "/bin/sh";
        depDrv.outputs = {
            {
                "out",
                // will ensure that downstream is deferred
                DerivationOutput{DerivationOutput::CAFloating{
                    .method = ContentAddressMethod::Raw::NixArchive,
                    .hashAlgo = HashAlgorithm::SHA256,
                }},
            },
        };
        depDrv.env = {{"out", ""}};

        // Fill in the dependency derivation's output paths
        depDrv.fillInOutputPaths(*store);

        // Write the dependency to the store
        return writeDerivation(*store, depDrv, NoRepair);
    }

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

TEST_F(FillInOutputPathsTest, fillsDeferredOutputs_emptyStringEnvVar)
{
    using nlohmann::json;

    // Before: Derivation with deferred output
    Derivation drv;
    drv.name = "filled-in-deferred-empty-env-var";
    drv.platform = "x86_64-linux";
    drv.builder = "/bin/sh";
    drv.outputs = {
        {"out", DerivationOutput{DerivationOutput::Deferred{}}},
    };
    drv.env = {{"__doc", "Fill in deferred output with empty env var"}, {"out", ""}};

    // Serialize before state
    checkpointJson("filled-in-deferred-empty-env-var-pre", drv);

    drv.fillInOutputPaths(*store);

    // Serialize after state
    checkpointJson("filled-in-deferred-empty-env-var-post", drv);

    // After: Should have been converted to InputAddressed
    auto * outputP = std::get_if<DerivationOutput::InputAddressed>(&drv.outputs.at("out").raw);
    ASSERT_TRUE(outputP);
    auto & output = *outputP;

    // Environment variable should be filled in
    EXPECT_EQ(drv.env.at("out"), store->printStorePath(output.path));
}

TEST_F(FillInOutputPathsTest, fillsDeferredOutputs_empty_string_var)
{
    using nlohmann::json;

    // Before: Derivation with deferred output
    Derivation drv;
    drv.name = "filled-in-deferred-no-env-var";
    drv.platform = "x86_64-linux";
    drv.builder = "/bin/sh";
    drv.outputs = {
        {"out", DerivationOutput{DerivationOutput::Deferred{}}},
    };
    drv.env = {
        {"__doc", "Fill in deferred with missing env var"},
    };

    // Serialize before state
    checkpointJson("filled-in-deferred-no-env-var-pre", drv);

    drv.fillInOutputPaths(*store);

    // Serialize after state
    checkpointJson("filled-in-deferred-no-env-var-post", drv);

    // After: Should have been converted to InputAddressed
    auto * outputP = std::get_if<DerivationOutput::InputAddressed>(&drv.outputs.at("out").raw);
    ASSERT_TRUE(outputP);
    auto & output = *outputP;

    // Environment variable should be filled in
    EXPECT_EQ(drv.env.at("out"), store->printStorePath(output.path));
}

TEST_F(FillInOutputPathsTest, preservesInputAddressedOutputs)
{
    auto expectedPath = StorePath{"w4bk7hpyxzgy2gx8fsa8f952435pll3i-filled-in-already"};

    Derivation drv;
    drv.name = "filled-in-already";
    drv.platform = "x86_64-linux";
    drv.builder = "/bin/sh";
    drv.outputs = {
        {"out", DerivationOutput{DerivationOutput::InputAddressed{.path = expectedPath}}},
    };
    drv.env = {
        {"__doc", "Correct path stays unchanged"},
        {"out", store->printStorePath(expectedPath)},
    };

    // Serialize before state
    checkpointJson("filled-in-idempotent", drv);

    auto drvBefore = drv;

    drv.fillInOutputPaths(*store);

    // Should still be no change
    EXPECT_EQ(drv, drvBefore);
}

TEST_F(FillInOutputPathsTest, throwsOnIncorrectInputAddressedPath)
{
    auto wrongPath = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-wrong-name"};

    Derivation drv;
    drv.name = "bad-path";
    drv.platform = "x86_64-linux";
    drv.builder = "/bin/sh";
    drv.outputs = {
        {"out", DerivationOutput{DerivationOutput::InputAddressed{.path = wrongPath}}},
    };
    drv.env = {
        {"__doc", "Wrong InputAddressed path throws error"},
        {"out", store->printStorePath(wrongPath)},
    };

    // Serialize before state
    checkpointJson("bad-path", drv);

    ASSERT_THROW(drv.fillInOutputPaths(*store), Error);
}

#if 0
TEST_F(FillInOutputPathsTest, throwsOnIncorrectEnvVar)
{
    auto wrongPath = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-wrong-name"};

    Derivation drv;
    drv.name = "bad-env-var";
    drv.platform = "x86_64-linux";
    drv.builder = "/bin/sh";
    drv.outputs = {
        {"out", DerivationOutput{DerivationOutput::Deferred{}}},
    };
    drv.env = {
        {"__doc", "Wrong env var value throws error"},
        {"out", store->printStorePath(wrongPath)},
    };

    // Serialize before state
    checkpointJson("bad-env-var", drv);

    ASSERT_THROW(drv.fillInOutputPaths(*store), Error);
}
#endif

TEST_F(FillInOutputPathsTest, preservesDeferredWithInputDrvs)
{
    using nlohmann::json;

    // Create a CA floating dependency derivation
    auto depDrvPath = makeCAFloatingDependency("dependency");

    // Create a derivation that depends on the dependency
    Derivation drv;
    drv.name = "depends-on-drv";
    drv.platform = "x86_64-linux";
    drv.builder = "/bin/sh";
    drv.outputs = {
        {"out", DerivationOutput{DerivationOutput::Deferred{}}},
    };
    drv.env = {
        {"__doc", "Deferred stays deferred with CA dependencies"},
        {"out", ""},
    };
    // Add the real input derivation dependency
    drv.inputDrvs = {.map = {{depDrvPath, {.value = {"out"}}}}};

    // Serialize before state
    checkpointJson("depends-on-drv-pre", drv);

    auto drvBefore = drv;

    // Apply fillInOutputPaths
    drv.fillInOutputPaths(*store);

    // Derivation should be unchanged
    EXPECT_EQ(drv, drvBefore);
}

TEST_F(FillInOutputPathsTest, throwsOnPatWhenShouldBeDeffered)
{
    using nlohmann::json;

    // Create a CA floating dependency derivation
    auto depDrvPath = makeCAFloatingDependency("dependency");

    auto wrongPath = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-wrong-name"};

    // Create a derivation that depends on the dependency
    Derivation drv;
    drv.name = "depends-on-drv";
    drv.platform = "x86_64-linux";
    drv.builder = "/bin/sh";
    drv.outputs = {
        {"out", DerivationOutput{DerivationOutput::InputAddressed{.path = wrongPath}}},
    };
    drv.env = {
        {"__doc", "InputAddressed throws when should be deferred"},
        {"out", ""},
    };
    // Add the real input derivation dependency
    drv.inputDrvs = {.map = {{depDrvPath, {.value = {"out"}}}}};

    // Serialize before state
    checkpointJson("bad-depends-on-drv-pre", drv);

    // Apply fillInOutputPaths
    ASSERT_THROW(drv.fillInOutputPaths(*store), Error);
}

} // namespace nix
