#include <gtest/gtest.h>

#include "nix/store/dummy-store.hh"
#include "nix/store/globals.hh"
#include "nix/store/realisation.hh"

namespace nix {

TEST(DummyStore, realisation_read)
{
    initLibStore(/*loadConfig=*/false);

    auto store = [] {
        auto cfg = make_ref<DummyStoreConfig>(StoreReference::Params{});
        cfg->readOnly = false;
        return cfg->openStore();
    }();

    auto drvHash = Hash::parseExplicitFormatUnprefixed(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", HashAlgorithm::SHA256, HashFormat::Base16);

    auto outputName = "foo";

    EXPECT_EQ(store->queryRealisation({drvHash, outputName}), nullptr);
}

} // namespace nix
