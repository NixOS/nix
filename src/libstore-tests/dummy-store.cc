#include <gtest/gtest.h>

#include "nix/store/dummy-store-impl.hh"
#include "nix/store/globals.hh"
#include "nix/store/realisation.hh"

namespace nix {

TEST(DummyStore, realisation_read)
{
    initLibStore(/*loadConfig=*/false);

    auto store = [] {
        auto cfg = make_ref<DummyStoreConfig>(StoreReference::Params{});
        cfg->readOnly = false;
        return cfg->openDummyStore();
    }();

    auto drvHash = Hash::parseExplicitFormatUnprefixed(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", HashAlgorithm::SHA256, HashFormat::Base16);

    auto outputName = "foo";

    EXPECT_EQ(store->queryRealisation({drvHash, outputName}), nullptr);

    UnkeyedRealisation value{
        .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv"},
    };

    store->buildTrace.insert({drvHash, {{outputName, make_ref<UnkeyedRealisation>(value)}}});

    auto value2 = store->queryRealisation({drvHash, outputName});

    ASSERT_TRUE(value2);
    EXPECT_EQ(*value2, value);
}

} // namespace nix
