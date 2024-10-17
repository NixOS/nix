#include <gtest/gtest.h>

#include "http-binary-cache-store.hh"

namespace nix {

TEST(HttpBinaryCacheStore, constructConfig)
{
    HttpBinaryCacheStoreConfig config{"http", "foo.bar.baz", {}};

    EXPECT_EQ(config.cacheUri, "http://foo.bar.baz");
}

TEST(HttpBinaryCacheStore, constructConfigNoTrailingSlash)
{
    HttpBinaryCacheStoreConfig config{"https", "foo.bar.baz/a/b/", {}};

    EXPECT_EQ(config.cacheUri, "https://foo.bar.baz/a/b");
}

} // namespace nix
