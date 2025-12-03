#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/filetransfer.hh"
#include "nix/store/s3-url.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(S3BinaryCacheStore, constructConfig)
{
    S3BinaryCacheStoreConfig config{"s3", "foobar", {}};

    // The bucket name is stored as the host part of the authority in cacheUri
    EXPECT_EQ(
        config.cacheUri,
        (ParsedURL{
            .scheme = "s3",
            .authority = ParsedURL::Authority{.host = "foobar"},
        }));
}

TEST(S3BinaryCacheStore, constructConfigWithRegion)
{
    Store::Config::Params params{{"region", "eu-west-1"}};
    S3BinaryCacheStoreConfig config{"s3", "my-bucket", params};

    EXPECT_EQ(
        config.cacheUri,
        (ParsedURL{
            .scheme = "s3",
            .authority = ParsedURL::Authority{.host = "my-bucket"},
            .query = (StringMap) {{"region", "eu-west-1"}},
        }));
    EXPECT_EQ(config.region.get(), "eu-west-1");
}

TEST(S3BinaryCacheStore, defaultSettings)
{
    S3BinaryCacheStoreConfig config{"s3", "test-bucket", {}};

    EXPECT_EQ(
        config.cacheUri,
        (ParsedURL{
            .scheme = "s3",
            .authority = ParsedURL::Authority{.host = "test-bucket"},
        }));

    // Check default values
    EXPECT_EQ(config.region.get(), "us-east-1");
    EXPECT_EQ(config.profile.get(), "default");
    EXPECT_EQ(config.scheme.get(), "https");
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
    EXPECT_EQ(
        config.cacheUri,
        (ParsedURL{
            .scheme = "s3",
            .authority = ParsedURL::Authority{.host = "test-bucket"},
            .query = (StringMap) {{"region", "eu-west-1"}, {"endpoint", "custom.s3.com"}},
        }));
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

/**
 * Test that only S3-specific parameters are preserved in cacheUri,
 * while non-S3 store parameters are not propagated to the URL
 */
TEST(S3BinaryCacheStore, parameterFiltering)
{
    StringMap params;
    params["region"] = "eu-west-1";
    params["endpoint"] = "minio.local";
    params["want-mass-query"] = "true"; // Non-S3 store parameter
    params["priority"] = "10";          // Non-S3 store parameter

    S3BinaryCacheStoreConfig config("s3", "test-bucket", params);

    // Only S3-specific params should be in cacheUri.query
    EXPECT_EQ(
        config.cacheUri,
        (ParsedURL{
            .scheme = "s3",
            .authority = ParsedURL::Authority{.host = "test-bucket"},
            .query = (StringMap) {{"region", "eu-west-1"}, {"endpoint", "minio.local"}},
        }));

    // But the non-S3 params should still be set on the config
    EXPECT_EQ(config.wantMassQuery.get(), true);
    EXPECT_EQ(config.priority.get(), 10);

    // And all params (S3 and non-S3) should be returned by getReference()
    auto ref = config.getReference();
    EXPECT_EQ(ref.params["region"], "eu-west-1");
    EXPECT_EQ(ref.params["endpoint"], "minio.local");
    EXPECT_EQ(ref.params["want-mass-query"], "true");
    EXPECT_EQ(ref.params["priority"], "10");
}

/**
 * Test storage class configuration
 */
TEST(S3BinaryCacheStore, storageClassDefault)
{
    S3BinaryCacheStoreConfig config{"s3", "test-bucket", {}};
    EXPECT_EQ(config.storageClass.get(), std::nullopt);
}

TEST(S3BinaryCacheStore, storageClassConfiguration)
{
    StringMap params;
    params["storage-class"] = "GLACIER";

    S3BinaryCacheStoreConfig config("s3", "test-bucket", params);
    EXPECT_EQ(config.storageClass.get(), std::optional<std::string>("GLACIER"));
}

} // namespace nix
