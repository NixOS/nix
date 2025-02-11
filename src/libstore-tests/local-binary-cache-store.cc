#include <gtest/gtest.h>

#include "local-binary-cache-store.hh"

namespace nix {

TEST(LocalBinaryCacheStore, constructConfig)
{
    LocalBinaryCacheStoreConfig config{"local", "/foo/bar/baz", {}};

    EXPECT_EQ(config.binaryCacheDir, "/foo/bar/baz");
}

} // namespace nix
