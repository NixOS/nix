#include <gtest/gtest.h>

#include "nix/store/derivations.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/realisation.hh"
#include "nix/store/store-api.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

class ResolveDerivedPathTest : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        initLibStore(false);
    }

protected:
    EnableExperimentalFeature caFeature{"ca-derivations"};
    EnableExperimentalFeature dynFeature{"dynamic-derivations"};

    ref<DummyStore> store = [] {
        auto cfg = make_ref<DummyStoreConfig>(StoreReference::Params{});
        cfg->readOnly = false;
        return cfg->openDummyStore();
    }();

    Derivation textDrv(std::string name)
    {
        return Derivation{
            .outputs =
                {{"out",
                  DerivationOutput{DerivationOutput::CAFloating{
                      .method = ContentAddressMethod::Raw::Text,
                      .hashAlgo = HashAlgorithm::SHA256,
                  }}}},
            .platform = "x86_64-linux",
            .builder = "/bin/sh",
            .name = std::move(name),
        };
    }
};

// producer^out is realised but its path, the inner derivation, was garbage collected
TEST_F(ResolveDerivedPathTest, nestedOutputOfCollectedDerivation)
{
    auto producer = store->writeDerivation(textDrv("inner.drv"));
    StorePath inner{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-inner.drv"};

    store->registerDrvOutput(Realisation{{.outPath = inner}, DrvOutput{producer, "out"}}, NoCheckSigs);
    ASSERT_FALSE(store->isValidPath(inner));

    auto req = SingleDerivedPath::Built{
        .drvPath = make_ref<SingleDerivedPath>(SingleDerivedPath::Built{
            .drvPath = makeConstantStorePathRef(producer),
            .output = "out",
        }),
        .output = "out",
    };

    EXPECT_THROW(resolveDerivedPath(*store, req), MissingRealisation);
}

} // namespace nix
