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

    StorePath drvPath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv"};

    auto outputName = "foo";

    EXPECT_EQ(store->queryRealisation({drvPath, outputName}), nullptr);

    UnkeyedRealisation value{
        .outPath = StorePath{"g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo"},
    };

    store->buildTrace.insert({drvPath, {{outputName, make_ref<UnkeyedRealisation>(value)}}});

    auto value2 = store->queryRealisation({drvPath, outputName});

    ASSERT_TRUE(value2);
    EXPECT_EQ(*value2, value);
}

} // namespace nix
