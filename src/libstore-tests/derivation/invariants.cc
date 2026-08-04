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
            .outputs{
                {
                    "out",
                    DerivationOutput{DerivationOutput::CAFloating{
                        .method = ContentAddressMethod::Raw::NixArchive,
                        .hashAlgo = HashAlgorithm::SHA256,
                    }},
                },
            },
            .platform = "x86_64-linux",
            .builder = "/bin/sh",
            .env = {{"out", ""}},
            .name = std::string{name},
        };

        // Fill in the dependency derivation's output paths
        fillInOutputPaths(depDrv, *store);

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
        .outputs = {{"out", DerivationOutput{DerivationOutput::Deferred{}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", "Fill in deferred output with empty env var"}, {"out", ""}},
        .name = "filled-in-deferred-empty-env-var",
    };

    // Serialize before state
    checkpointJson("filled-in-deferred-empty-env-var-pre", drv);

    fillInOutputPaths(drv, *store);

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
    Derivation drv{
        .outputs = {{"out", DerivationOutput{DerivationOutput::Deferred{}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", "Fill in deferred with missing env var"}},
        .name = "filled-in-deferred-no-env-var",
    };

    // Serialize before state
    checkpointJson("filled-in-deferred-no-env-var-pre", drv);

    fillInOutputPaths(drv, *store);

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

    Derivation drv{
        .outputs = {{"out", DerivationOutput{DerivationOutput::InputAddressed{.path = expectedPath}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", "Correct path stays unchanged"}, {"out", store->printStorePath(expectedPath)}},
        .name = "filled-in-already",
    };

    // Serialize before state
    checkpointJson("filled-in-idempotent", drv);

    auto drvBefore = drv;

    fillInOutputPaths(drv, *store);

    // Should still be no change
    EXPECT_EQ(drv, drvBefore);
}

TEST_F(FillInOutputPathsTest, throwsOnIncorrectInputAddressedPath)
{
    auto wrongPath = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-wrong-name"};

    Derivation drv{
        .outputs = {{"out", DerivationOutput{DerivationOutput::InputAddressed{.path = wrongPath}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", "Wrong InputAddressed path throws error"}, {"out", store->printStorePath(wrongPath)}},
        .name = "bad-path",
    };

    // Serialize before state
    checkpointJson("bad-path", drv);

    ASSERT_THROW(fillInOutputPaths(drv, *store), Error);
}

TEST_F(FillInOutputPathsTest, warnsOnIncorrectEnvVarForDeferred)
{
    auto wrongPath = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-wrong-name"};

    Derivation drv{
        .outputs = {{"out", DerivationOutput{DerivationOutput::Deferred{}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", "Wrong env var for deferred output only warns"}, {"out", store->printStorePath(wrongPath)}},
        .name = "bad-env-var",
    };

    /* An incorrect pre-existing env var for a formerly-deferred output
       only warns, for compatibility with derivations produced by older
       versions of Nix. This will become an error in future versions of
       Nix; then this test should `ASSERT_THROW` instead. */
    fillInOutputPaths(drv, *store);

    // The output itself is still filled in...
    ASSERT_TRUE(std::get_if<DerivationOutput::InputAddressed>(&drv.outputs.at("out").raw));

    // ...and the env var is corrected so later validation passes.
    auto & filled = std::get<DerivationOutput::InputAddressed>(drv.outputs.at("out").raw);
    EXPECT_EQ(drv.env.at("out"), store->printStorePath(filled.path));
    ASSERT_NO_THROW(checkInvariants(drv, *store));
}

TEST_F(FillInOutputPathsTest, throwsOnMixedOutputTypes)
{
    Derivation drv{
        .outputs =
            {
                {"dev",
                 DerivationOutput{DerivationOutput::CAFloating{
                     .method = ContentAddressMethod::Raw::NixArchive,
                     .hashAlgo = HashAlgorithm::SHA256,
                 }}},
                {"out", DerivationOutput{DerivationOutput::Deferred{}}},
            },
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", "Mixed output types are a catchable error, not an abort"}, {"out", ""}, {"dev", ""}},
        .name = "mixed-output-types",
    };

    // Must throw, not panic (std::terminate) inside hashModulo.
    ASSERT_THROW(fillInOutputPaths(drv, *store), Error);
    ASSERT_THROW(checkInvariants(drv, *store), Error);
}

TEST_F(FillInOutputPathsTest, skipsEnvVarsWithBuilderRpc)
{
    Derivation drv{
        .outputs =
            {{"out",
              DerivationOutput{DerivationOutput::CAFixed{
                  .ca =
                      ContentAddress{
                          .method = ContentAddressMethod::Raw::NixArchive,
                          .hash = hashString(HashAlgorithm::SHA256, "foo"),
                      },
              }}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env =
            {{"__doc", "builder-rpc-v0 derivations have no output env vars"},
             {"requiredSystemFeatures", "builder-rpc-v0"}},
        .name = "rpc-outputs",
    };

    auto drvBefore = drv;

    /* With `builder-rpc-v0`, outputs are communicated to the builder
       over RPC, so there is deliberately no `out` env var: fill-in must
       not invent one, and validation must not demand one. */
    fillInOutputPaths(drv, *store);
    EXPECT_EQ(drv, drvBefore);

    EXPECT_NO_THROW(checkInvariants(drv, *store));
}

TEST_F(FillInOutputPathsTest, rejectsBuilderRpcForInputAddressed)
{
    Derivation drv{
        .outputs = {{"out", DerivationOutput{DerivationOutput::Deferred{}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env =
            {{"__doc", "builder-rpc-v0 requires content addressing"},
             {"requiredSystemFeatures", "builder-rpc-v0"},
             {"out", ""}},
        .name = "rpc-input-addressed",
    };

    /* `builder-rpc-v0` may only be used with content-addressing
       derivations; input-addressed (including deferred) derivations
       claiming it are rejected. */
    ASSERT_THROW(fillInOutputPaths(drv, *store), Error);
    ASSERT_THROW(checkInvariants(drv, *store), Error);
}

TEST_F(FillInOutputPathsTest, preservesDeferredWithInputDrvs)
{
    using nlohmann::json;

    // Create a CA floating dependency derivation
    auto depDrvPath = makeCAFloatingDependency("dependency");

    // Create a derivation that depends on the dependency
    Derivation drv{
        .outputs = {{"out", DerivationOutput{DerivationOutput::Deferred{}}}},
        .inputs = {.drvs = {.map = {{depDrvPath, {.value = {"out"}}}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", "Deferred stays deferred with CA dependencies"}, {"out", ""}},
        .name = "depends-on-drv",
    };

    // Serialize before state
    checkpointJson("depends-on-drv-pre", drv);

    auto drvBefore = drv;

    // Apply fillInOutputPaths
    fillInOutputPaths(drv, *store);

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
        .outputs = {{"out", DerivationOutput{DerivationOutput::InputAddressed{.path = wrongPath}}}},
        .inputs = {.drvs = {.map = {{depDrvPath, {.value = {"out"}}}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", "InputAddressed throws when should be deferred"}, {"out", ""}},
        .name = "depends-on-drv",
    };

    // Serialize before state
    checkpointJson("bad-depends-on-drv-pre", drv);

    // Apply fillInOutputPaths
    ASSERT_THROW(fillInOutputPaths(drv, *store), Error);
}

} // namespace nix
