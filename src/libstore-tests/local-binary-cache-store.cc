#include <gtest/gtest.h>

#include "nix/store/globals.hh"
#include "nix/store/local-binary-cache-store.hh"

namespace nix {

TEST(LocalBinaryCacheStore, constructConfig)
{
    Settings settings;
    LocalBinaryCacheStoreConfig config{settings, "local", "/foo/bar/baz", {}};

    EXPECT_EQ(config.binaryCacheDir, "/foo/bar/baz");
}

} // namespace nix
