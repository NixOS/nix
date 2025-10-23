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

static Derivation makeSimpleDrv()
{
    Derivation drv;
    drv.name = "simple-derivation";
    drv.platform = "system";
    drv.builder = "foo";
    drv.args = {"bar", "baz"};
    drv.env = StringPairs{{"BIG_BAD", "WOLF"}};
    return drv;
}

} // namespace

TEST_F(WriteDerivationTest, addToStoreFromDumpCalledOnce)
{
    auto drv = makeSimpleDrv();

    auto path1 = writeDerivation(*store, drv, NoRepair);
    config->readOnly = true;
    auto path2 = writeDerivation(*store, drv, NoRepair);
    EXPECT_EQ(path1, path2);
    EXPECT_THAT(
        [&] { writeDerivation(*store, drv, Repair); },
        ::testing::ThrowsMessage<Error>(
            testing::HasSubstrIgnoreANSIMatcher("operation 'writeDerivation' is not supported by store 'dummy://'")));
}

} // namespace nix
