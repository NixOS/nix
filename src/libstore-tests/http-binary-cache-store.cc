#include <gtest/gtest.h>

#include "nix/store/http-binary-cache-store.hh"

namespace nix {

TEST(HttpBinaryCacheStore, constructConfig)
{
    HttpBinaryCacheStoreConfig config{"http", "foo.bar.baz", {}};

    EXPECT_EQ(config.cacheUri.to_string(), "http://foo.bar.baz");
}

TEST(HttpBinaryCacheStore, constructConfigNoTrailingSlash)
{
    HttpBinaryCacheStoreConfig config{"https", "foo.bar.baz/a/b/", {}};

    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b");
}

} // namespace nix
