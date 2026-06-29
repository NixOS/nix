#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/tests/gmock-matchers.hh"
#include "nix/store/derivations.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {
namespace {

class WriteDerivationTest : public LibStoreTest
{
protected:
    WriteDerivationTest(ref<DummyStoreConfig> config_)
        : LibStoreTest(config_->openDummyStore())
        , config(std::move(config_))
    {
        config->readOnly = false;
    }

    WriteDerivationTest()
        : WriteDerivationTest(make_ref<DummyStoreConfig>(DummyStoreConfig::Params{}))
    {
    }

    ref<DummyStoreConfig> config;
};

} // namespace

Derivation makeSimpleDrv();

TEST_F(WriteDerivationTest, addToStoreFromDumpCalledOnce)
{
    auto drv = makeSimpleDrv();
    drv.options = derivationOptionsFromStructuredAttrs(
        StoreDirConfig{"/nix/store"}, drv.inputs, drv.env, drv.structuredAttrs ? &*drv.structuredAttrs : nullptr);

    auto path1 = store->writeDerivation(drv, NoRepair);
    config->readOnly = true;
    auto path2 = computeStorePath(*store, drv);
    EXPECT_EQ(path1, path2);
    EXPECT_THAT(
        [&] { store->writeDerivation(drv, Repair); },
        ::testing::ThrowsMessage<Error>(
            testing::HasSubstrIgnoreANSIMatcher("operation 'writeDerivation' is not supported by store 'dummy://'")));
}

} // namespace nix
