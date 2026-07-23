#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/store/derivations.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

class FillInOutputPathsTest : public LibStoreTest, public JsonCharacterizationTest<Derivation>
{
    std::filesystem::path unitTestData = getUnitTestData() / "derivation" / "invariants";

protected:
    FillInOutputPathsTest()
        : LibStoreTest([]() {
            auto config = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{});
            config->readOnly = false;
            return config->openDummyStore();
        }())
    {
    }

    /**
     * Create a CA floating output derivation and write it to the store.
     * This is useful for creating dependencies that will cause downstream
     * derivations to remain deferred.
     */
    StorePath makeCAFloatingDependency(std::string_view name)
    {
        Derivation depDrv{
            .name = std::string{name},
            .outputs{
                {
                    "out",
                    {.output =
                         DerivationOutput::CAFloating{
                             .method = ContentAddressMethod::Raw::NixArchive,
                             .hashAlgo = HashAlgorithm::SHA256,
                         }},
                },
            },
            .platform = "x86_64-linux",
            .builder = "/bin/sh",
            .env = {{"out", {}}},
        };

        // Fill in the dependency derivation's output paths
        depDrv.fillInOutputPaths(*store);

        // Write the dependency to the store
        return store->writeDerivation(depDrv, NoRepair);
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
    Derivation drv{
        .name = "filled-in-deferred-empty-env-var",
        .outputs = {{"out", {.output = DerivationOutput::Deferred{}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", {.value = "Fill in deferred output with empty env var"}}, {"out", {}}},
    };

    // Serialize before state
    checkpointJson("filled-in-deferred-empty-env-var-pre", drv);

    drv.fillInOutputPaths(*store);

    // Serialize after state
    checkpointJson("filled-in-deferred-empty-env-var-post", drv);

    // After: Should have been converted to InputAddressed
    auto * outputP = std::get_if<DerivationOutput::InputAddressed>(&drv.outputs.at("out").output.raw);
    ASSERT_TRUE(outputP);
    auto & output = *outputP;

    // Environment variable should be filled in
    EXPECT_EQ(drv.env.at("out").value, store->printStorePath(output.path));
}

TEST_F(FillInOutputPathsTest, fillsDeferredOutputs_empty_string_var)
{
    using nlohmann::json;

    // Before: Derivation with deferred output
    Derivation drv{
        .name = "filled-in-deferred-no-env-var",
        .outputs = {{"out", {.output = DerivationOutput::Deferred{}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", {.value = "Fill in deferred with missing env var"}}},
    };

    // Serialize before state
    checkpointJson("filled-in-deferred-no-env-var-pre", drv);

    drv.fillInOutputPaths(*store);

    // Serialize after state
    checkpointJson("filled-in-deferred-no-env-var-post", drv);

    // After: Should have been converted to InputAddressed
    auto * outputP = std::get_if<DerivationOutput::InputAddressed>(&drv.outputs.at("out").output.raw);
    ASSERT_TRUE(outputP);
    auto & output = *outputP;

    // Environment variable should be filled in
    EXPECT_EQ(drv.env.at("out").value, store->printStorePath(output.path));
}

TEST_F(FillInOutputPathsTest, preservesInputAddressedOutputs)
{
    auto expectedPath = StorePath{"w4bk7hpyxzgy2gx8fsa8f952435pll3i-filled-in-already"};

    Derivation drv{
        .name = "filled-in-already",
        .outputs = {{"out", {.output = DerivationOutput{DerivationOutput::InputAddressed{.path = expectedPath}}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env =
            {{"__doc", {.value = "Correct path stays unchanged"}},
             {"out", {.value = store->printStorePath(expectedPath)}}},
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

    Derivation drv{
        .name = "bad-path",
        .outputs = {{"out", {.output = DerivationOutput{DerivationOutput::InputAddressed{.path = wrongPath}}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env =
            {{"__doc", {.value = "Wrong InputAddressed path throws error"}},
             {"out", {.value = store->printStorePath(wrongPath)}}},
    };

    // Serialize before state
    checkpointJson("bad-path", drv);

    ASSERT_THROW(drv.fillInOutputPaths(*store), Error);
}

#if 0
TEST_F(FillInOutputPathsTest, throwsOnIncorrectEnvVar)
{
    auto wrongPath = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-wrong-name"};

    Derivation drv{
        .name = "bad-env-var",
        .outputs = {{"out", {.output = DerivationOutput{DerivationOutput::Deferred{}}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", {.value = "Wrong env var value throws error"}}, {"out", {.value = store->printStorePath(wrongPath)}}},
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
    Derivation drv{
        .name = "depends-on-drv",
        .outputs{
            {"out", {.output = DerivationOutput::Deferred{}}},
        },
        .inputs{
            SingleDerivedPath::Built{
                .drvPath = makeConstantStorePathRef(depDrvPath),
                .output = "out",
            },
        },
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", {.value = "Deferred stays deferred with CA dependencies"}}, {"out", {}}},
    };

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
    Derivation drv{
        .name = "depends-on-drv",
        .outputs{
            {"out",
             {.output = DerivationOutput{DerivationOutput::InputAddressed{
                  .path = wrongPath,
              }}}},
        },
        .inputs{
            SingleDerivedPath::Built{
                .drvPath = makeConstantStorePathRef(depDrvPath),
                .output = "out",
            },
        },
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", {.value = "InputAddressed throws when should be deferred"}}, {"out", {}}},
    };

    // Serialize before state
    checkpointJson("bad-depends-on-drv-pre", drv);

    // Apply fillInOutputPaths
    ASSERT_THROW(drv.fillInOutputPaths(*store), Error);
}

} // namespace nix
