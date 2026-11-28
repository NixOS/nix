#include <gtest/gtest.h>

#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/fetch-to-store.hh"
#include "nix/store/dummy-store.hh"
#include "nix/store/globals.hh"
#include "nix/store/tests/forwarding-store.hh"
#include "nix/util/file-system.hh"
#include "nix/util/source-accessor.hh"

namespace nix {

struct RootCountingStore : ForwardingStore
{
    size_t tempRoots = 0;

    using ForwardingStore::ForwardingStore;

    void addTempRoots(const StorePathSet & paths) override
    {
        tempRoots += paths.size();
        ForwardingStore::addTempRoots(paths);
    }
};

class FetchToStoreTest : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        initLibStore(/*loadConfig=*/false);
    }

protected:
    ref<Store> dummy = [] {
        auto config = make_ref<DummyStoreConfig>(StoreReference::Params{});
        config->readOnly = false;
        return config->openStore();
    }();
    fetchers::Settings settings;
    std::filesystem::path dir = createTempDir();
    SourcePath builder{getFSSourceAccessor(), CanonPath((dir / "builder.sh").string())};

    FetchToStoreTest()
    {
        writeFile(dir / "builder.sh", "echo");
    }

    StorePath fetch(Store & store)
    {
        return fetchToStore(settings, store, builder, FetchMode::Copy, "builder.sh");
    }
};

TEST_F(FetchToStoreTest, aCopiedSourceIsNotRootedAgainThroughTheSameStore)
{
    auto store = make_ref<RootCountingStore>(dummy);
    auto copied = fetch(*store);

    store->tempRoots = 0;
    EXPECT_EQ(fetch(*store), copied);
    EXPECT_EQ(fetch(*store), copied);
    EXPECT_EQ(store->tempRoots, 0);
}

TEST_F(FetchToStoreTest, anotherStoreRootsACopiedSourceOnce)
{
    auto copied = fetch(*make_ref<RootCountingStore>(dummy));

    auto other = make_ref<RootCountingStore>(dummy);
    EXPECT_EQ(fetch(*other), copied);
    EXPECT_EQ(fetch(*other), copied);
    EXPECT_EQ(other->tempRoots, 1);
}

} // namespace nix
