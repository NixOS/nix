#include <gtest/gtest.h>

#include "nix/store/globals.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/tests/test-main.hh"

namespace nix {

TEST(HttpBinaryCacheStore, constructConfig)
{
    auto settings = getTestSettings();
    HttpBinaryCacheStoreConfig config{settings, "http", "foo.bar.baz", {}};

    EXPECT_EQ(config.cacheUri.to_string(), "http://foo.bar.baz");
}

TEST(HttpBinaryCacheStore, constructConfigNoTrailingSlash)
{
    auto settings = getTestSettings();
    HttpBinaryCacheStoreConfig config{settings, "https", "foo.bar.baz/a/b/", {}};

    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b");
}

TEST(HttpBinaryCacheStore, constructConfigWithParams)
{
    auto settings = getTestSettings();
    StoreConfig::Params params{{"compression", "xz"}};
    HttpBinaryCacheStoreConfig config{settings, "https", "foo.bar.baz/a/b/", params};
    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b");
    EXPECT_EQ(config.getReference().params, params);
}

TEST(HttpBinaryCacheStore, constructConfigWithParamsAndUrlWithParams)
{
    auto settings = getTestSettings();
    StoreConfig::Params params{{"compression", "xz"}};
    HttpBinaryCacheStoreConfig config{settings, "https", "foo.bar.baz/a/b?some-param=some-value", params};
    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b?some-param=some-value");
    EXPECT_EQ(config.getReference().params, params);
}

} // namespace nix
