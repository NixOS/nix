#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/filetransfer.hh"
#include "nix/store/s3-url.hh"

#if NIX_WITH_CURL_S3

#  include <gtest/gtest.h>

namespace nix {

TEST(S3BinaryCacheStore, constructConfig)
{
    S3BinaryCacheStoreConfig config{"s3", "foobar", {}};

    // The bucket name is stored as the host part of the authority in cacheUri
    ASSERT_TRUE(config.cacheUri.authority.has_value());
    EXPECT_EQ(config.cacheUri.authority->host, "foobar");
    EXPECT_EQ(config.cacheUri.scheme, "s3");
}

TEST(S3BinaryCacheStore, constructConfigWithRegion)
{
    Store::Config::Params params{{"region", "eu-west-1"}};
    S3BinaryCacheStoreConfig config{"s3", "my-bucket", params};

    ASSERT_TRUE(config.cacheUri.authority.has_value());
    EXPECT_EQ(config.cacheUri.authority->host, "my-bucket");
    EXPECT_EQ(config.region.get(), "eu-west-1");
}

TEST(S3BinaryCacheStore, defaultSettings)
{
    S3BinaryCacheStoreConfig config{"s3", "test-bucket", {}};

    // Check default values
    EXPECT_EQ(config.region.get(), "us-east-1");
    EXPECT_EQ(config.profile.get(), "");
    EXPECT_EQ(config.endpoint.get(), "");
}

/**
 * Test that S3BinaryCacheStore properly preserves S3-specific parameters
 */
TEST(S3BinaryCacheStore, s3StoreConfigPreservesParameters)
{
    StringMap params;
    params["region"] = "eu-west-1";
    params["endpoint"] = "custom.s3.com";

    S3BinaryCacheStoreConfig config("s3", "test-bucket", params);

    // The config should preserve S3-specific parameters
    EXPECT_EQ(config.cacheUri.scheme, "s3");
    EXPECT_EQ(config.cacheUri.authority->host, "test-bucket");
    EXPECT_FALSE(config.cacheUri.query.empty());
    EXPECT_EQ(config.cacheUri.query["region"], "eu-west-1");
    EXPECT_EQ(config.cacheUri.query["endpoint"], "custom.s3.com");
}

/**
 * Test that S3 store scheme is properly registered
 */
TEST(S3BinaryCacheStore, s3SchemeRegistration)
{
    auto schemes = S3BinaryCacheStoreConfig::uriSchemes();
    EXPECT_TRUE(schemes.count("s3") > 0) << "S3 scheme should be supported";

    // Verify HttpBinaryCacheStoreConfig doesn't directly list S3
    auto httpSchemes = HttpBinaryCacheStoreConfig::uriSchemes();
    EXPECT_FALSE(httpSchemes.count("s3") > 0) << "HTTP store shouldn't directly list S3 scheme";
}

} // namespace nix

#endif
