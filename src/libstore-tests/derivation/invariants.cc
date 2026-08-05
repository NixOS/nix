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
        .name = "filled-in-deferred-empty-env-var",
        .outputs = {{"out", {.output = DerivationOutput::Deferred{}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env = {{"__doc", {.value = "Fill in deferred output with empty env var"}}, {"out", {}}},
    };

    // Serialize before state
    checkpointJson("filled-in-deferred-empty-env-var-pre", drv);

    fillInOutputPaths(drv, *store);

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

    fillInOutputPaths(drv, *store);

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

    fillInOutputPaths(drv, *store);

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

    ASSERT_THROW(fillInOutputPaths(drv, *store), Error);
}

TEST_F(FillInOutputPathsTest, warnsOnIncorrectEnvVarForDeferred)
{
    auto wrongPath = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-wrong-name"};

    Derivation drv{
        .name = "bad-env-var",
        .outputs = {{"out", {.output = DerivationOutput{DerivationOutput::Deferred{}}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env =
            {{"__doc", {.value = "Wrong env var for deferred output only warns"}},
             {"out", {.value = store->printStorePath(wrongPath)}}},
    };

    /* An incorrect pre-existing env var for a formerly-deferred output
       only warns, for compatibility with derivations produced by older
       versions of Nix. This will become an error in future versions of
       Nix; then this test should `ASSERT_THROW` instead. */
    fillInOutputPaths(drv, *store);

    // The output itself is still filled in...
    ASSERT_TRUE(std::get_if<DerivationOutput::InputAddressed>(&drv.outputs.at("out").output.raw));

    // ...and the env var is corrected so later validation passes.
    auto & filled = std::get<DerivationOutput::InputAddressed>(drv.outputs.at("out").output.raw);
    EXPECT_EQ(drv.env.at("out").value, store->printStorePath(filled.path));
    ASSERT_NO_THROW(checkInvariants(drv, *store));
}

TEST_F(FillInOutputPathsTest, throwsOnMixedOutputTypes)
{
    Derivation drv{
        .name = "mixed-output-types",
        .outputs =
            {
                {"dev",
                 {.output = DerivationOutput{DerivationOutput::CAFloating{
                      .method = ContentAddressMethod::Raw::NixArchive,
                      .hashAlgo = HashAlgorithm::SHA256,
                  }}}},
                {"out", {.output = DerivationOutput{DerivationOutput::Deferred{}}}},
            },
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env =
            {{"__doc", {.value = "Mixed output types are a catchable error, not an abort"}},
             {"out", {.value = ""}},
             {"dev", {.value = ""}}},
    };

    // Must throw, not panic (std::terminate) inside bothMaskedDerivation.
    ASSERT_THROW(fillInOutputPaths(drv, *store), Error);
    ASSERT_THROW(checkInvariants(drv, *store), Error);
}

TEST_F(FillInOutputPathsTest, skipsEnvVarsWithBuilderRpc)
{
    Derivation drv{
        .name = "rpc-outputs",
        .outputs{
            {
                "out",
                {.output = DerivationOutput{DerivationOutput::CAFixed{
                     .ca =
                         ContentAddress{
                             .method = ContentAddressMethod::Raw::NixArchive,
                             .hash = hashString(HashAlgorithm::SHA256, "foo"),
                         },
                 }}},
            },
        },
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env =
            {{"__doc", {.value = "builder-rpc-v0 derivations have no output env vars"}},
             {"requiredSystemFeatures", {.value = "builder-rpc-v0"}}},
        .options{
            .requiredSystemFeatures = {"builder-rpc-v0"},
        },
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
        .name = "rpc-input-addressed",
        .outputs = {{"out", {.output = DerivationOutput{DerivationOutput::Deferred{}}}}},
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .env =
            {{"__doc", {.value = "builder-rpc-v0 requires content addressing"}},
             {"requiredSystemFeatures", {.value = "builder-rpc-v0"}},
             {"out", {}}},
        .options{
            .requiredSystemFeatures = {"builder-rpc-v0"},
        },
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
        .name = "depends-on-drv",
        .outputs{
            {
                "out",
                {.output = DerivationOutput{DerivationOutput::InputAddressed{
                     .path = wrongPath,
                 }}},
            },
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
    ASSERT_THROW(fillInOutputPaths(drv, *store), Error);
}

} // namespace nix
