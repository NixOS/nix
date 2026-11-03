#include <gtest/gtest.h>

#include "nix/store/local-binary-cache-store.hh"
#include "nix/util/file-system.hh"

namespace nix {

TEST(LocalBinaryCacheStore, storeDir_absolutePath)
{
    LocalBinaryCacheStoreConfig config{std::filesystem::path("/foo/bar/baz"), {{"store", "/my/store"}}};
    EXPECT_EQ(config.storeDir, "/my/store");
}

TEST(LocalBinaryCacheStore, storeDir_relativePath_rejected)
{
    EXPECT_THROW(
        LocalBinaryCacheStoreConfig(std::filesystem::path("/foo/bar/baz"), {{"store", "my/store"}}), UsageError);
}

TEST(LocalBinaryCacheStore, constructConfig)
{
    LocalBinaryCacheStoreConfig config{std::filesystem::path("/foo/bar/baz"), {}};
    EXPECT_EQ(config.binaryCacheDir, "/foo/bar/baz");
}

TEST(LocalBinaryCacheStore, fsAccessorsHandleMissingObject)
{
    auto cacheDir = createTempDir();
    AutoDelete delCacheDir{cacheDir};
    auto store = make_ref<LocalBinaryCacheStoreConfig>(cacheDir, LocalBinaryCacheStoreConfig::Params{})->openStore();
    EXPECT_EQ(store->getFSAccessor(StorePath::dummy), nullptr);
    EXPECT_EQ(store->getFSAccessor()->maybeLstat(CanonPath(StorePath::dummy.to_string())), std::nullopt);
}

} // namespace nix
