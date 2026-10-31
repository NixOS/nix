#pragma once

#include <gtest/gtest.h>

#include "nix/util/experimental-features.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/characterization.hh"

namespace nix {

class DerivationTest : public virtual CharacterizationTest, public LibStoreTest
{
protected:
    std::filesystem::path unitTestData = getUnitTestData() / "derivation";

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

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

} // namespace nix
