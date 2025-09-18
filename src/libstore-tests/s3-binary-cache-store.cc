#include "nix/store/s3-binary-cache-store.hh"

#if NIX_WITH_S3_SUPPORT

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

} // namespace nix

#endif
