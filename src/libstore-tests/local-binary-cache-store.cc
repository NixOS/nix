#include <gtest/gtest.h>

#include "nix/store/local-binary-cache-store.hh"

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

} // namespace nix
