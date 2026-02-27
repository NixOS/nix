#pragma once

#include <gtest/gtest.h>

#include "nix/util/experimental-features.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/characterization.hh"

namespace nix {

class DerivationTest : public virtual CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "derivation";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;
};

class CaDerivationTest : public DerivationTest
{
    void SetUp() override
    {
        mockXpSettings.set("experimental-features", "ca-derivations");
    }
};

class DynDerivationTest : public DerivationTest
{
    void SetUp() override
    {
        mockXpSettings.set("experimental-features", "dynamic-derivations ca-derivations");
    }
};

class ImpureDerivationTest : public DerivationTest
{
    void SetUp() override
    {
        mockXpSettings.set("experimental-features", "impure-derivations");
    }
};

class DerivationMetaTest : public DerivationTest
{
    void SetUp() override
    {
        mockXpSettings.set("experimental-features", "derivation-meta");
    }
};

} // namespace nix
