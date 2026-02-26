#include <gtest/gtest.h>

#include "nix/store/local-binary-cache-store.hh"

namespace nix {

TEST(LocalBinaryCacheStore, constructConfig)
{
    LocalBinaryCacheStoreConfig config{std::filesystem::path("/foo/bar/baz"), {}};
    EXPECT_EQ(config.binaryCacheDir, "/foo/bar/baz");
}

} // namespace nix
