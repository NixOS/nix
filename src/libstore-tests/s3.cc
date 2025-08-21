#include "nix/store/s3.hh"
#include "nix/store/config.hh"
#include "nix/util/tests/gmock-matchers.hh"

#if NIX_WITH_S3_SUPPORT

#  include <gtest/gtest.h>
#  include <gmock/gmock.h>

namespace nix {

struct ParsedS3URLTestCase
{
    std::string url;
    ParsedS3URL expected;
    std::string description;
};

class ParsedS3URLTest : public ::testing::WithParamInterface<ParsedS3URLTestCase>, public ::testing::Test
{};

TEST_P(ParsedS3URLTest, parseS3URLSuccessfully)
{
    const auto & testCase = GetParam();
    auto parsed = ParsedS3URL::parse(parseURL(testCase.url));
    ASSERT_EQ(parsed, testCase.expected);
}

INSTANTIATE_TEST_SUITE_P(
    QueryParams,
    ParsedS3URLTest,
    ::testing::Values(
        ParsedS3URLTestCase{
            "s3://my-bucket/my-key.txt",
            {
                .bucket = "my-bucket",
                .key = "my-key.txt",
            },
            "basic_s3_bucket",
        },
        ParsedS3URLTestCase{
            "s3://prod-cache/nix/store/abc123.nar.xz?region=eu-west-1",
            {
                .bucket = "prod-cache",
                .key = "nix/store/abc123.nar.xz",
                .region = "eu-west-1",
            },
            "with_region",
        },
        ParsedS3URLTestCase{
            "s3://bucket/key?region=us-west-2&profile=prod&endpoint=custom.s3.com&scheme=https&region=us-east-1",
            {
                .bucket = "bucket",
                .key = "key",
                .profile = "prod",
                .region = "us-west-2", //< using the first parameter (decodeQuery ignores dupicates)
                .scheme = "https",
                .endpoint = ParsedURL::Authority{.host = "custom.s3.com"},
            },
            "complex",
        },
        ParsedS3URLTestCase{
            "s3://cache/file.txt?profile=production&region=ap-southeast-2",
            {
                .bucket = "cache",
                .key = "file.txt",
                .profile = "production",
                .region = "ap-southeast-2",
            },
            "with_profile_and_region",
        },
        ParsedS3URLTestCase{
            "s3://bucket/key?endpoint=https://minio.local&scheme=http",
            {
                .bucket = "bucket",
                .key = "key",
                /* TODO: Figure out what AWS SDK is doing when both endpointOverride and scheme are set. */
                .scheme = "http",
                .endpoint =
                    ParsedURL{
                        .scheme = "https",
                        .authority = ParsedURL::Authority{.host = "minio.local"},
                    },
            },
            "with_absolute_endpoint_uri",
        }),
    [](const ::testing::TestParamInfo<ParsedS3URLTestCase> & info) { return info.param.description; });

// Parameterized test for invalid S3 URLs
struct InvalidS3URLTestCase
{
    std::string url;
    std::string expectedErrorSubstring;
    std::string description;
};

class InvalidParsedS3URLTest : public ::testing::WithParamInterface<InvalidS3URLTestCase>, public ::testing::Test
{};

TEST_P(InvalidParsedS3URLTest, parseS3URLErrors)
{
    const auto & testCase = GetParam();

    ASSERT_THAT(
        [&testCase]() { ParsedS3URL::parse(parseURL(testCase.url)); },
        ::testing::ThrowsMessage<BadURL>(testing::HasSubstrIgnoreANSIMatcher(testCase.expectedErrorSubstring)));
}

INSTANTIATE_TEST_SUITE_P(
    InvalidUrls,
    InvalidParsedS3URLTest,
    ::testing::Values(
        InvalidS3URLTestCase{"s3:///key", "error: URI has a missing or invalid bucket name", "empty_bucket"},
        InvalidS3URLTestCase{"s3://127.0.0.1", "error: URI has a missing or invalid bucket name", "ip_address_bucket"}),
    [](const ::testing::TestParamInfo<InvalidS3URLTestCase> & info) { return info.param.description; });

// AWS Credential Provider Tests

class AwsCredentialProviderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Clear any existing AWS environment variables for clean tests
        unsetenv("AWS_ACCESS_KEY_ID");
        unsetenv("AWS_SECRET_ACCESS_KEY");
        unsetenv("AWS_SESSION_TOKEN");
        unsetenv("AWS_PROFILE");
    }
};

TEST_F(AwsCredentialProviderTest, createDefault)
{
    try {
        auto provider = AwsCredentialProvider::createDefault();
        EXPECT_NE(provider, nullptr);
    } catch (const AwsAuthError & e) {
        // Expected in sandboxed environments where AWS CRT isn't available
        GTEST_SKIP() << "AWS CRT not available: " << e.what();
    }
}

TEST_F(AwsCredentialProviderTest, createProfileEmpty)
{
    try {
        auto provider = AwsCredentialProvider::createProfile("");
        EXPECT_NE(provider, nullptr);
    } catch (const AwsAuthError & e) {
        // Expected in sandboxed environments where AWS CRT isn't available
        GTEST_SKIP() << "AWS CRT not available: " << e.what();
    }
}

TEST_F(AwsCredentialProviderTest, createProfileNamed)
{
    // Creating a non-existent profile should throw
    try {
        auto provider = AwsCredentialProvider::createProfile("test-profile");
        // If we got here, the profile exists (unlikely in test environment)
        EXPECT_NE(provider, nullptr);
    } catch (const AwsAuthError & e) {
        // Expected - profile doesn't exist
        EXPECT_TRUE(std::string(e.what()).find("test-profile") != std::string::npos);
    }
}

TEST_F(AwsCredentialProviderTest, getCredentialsNoCredentials)
{
    // With no environment variables or profile, should throw when getting credentials
    try {
        auto provider = AwsCredentialProvider::createDefault();
        ASSERT_NE(provider, nullptr);

        // This should throw if there are no credentials available
        try {
            auto creds = provider->getCredentials();
            // If we got here, credentials were found (e.g., from IMDS or ~/.aws/credentials)
            EXPECT_FALSE(creds.accessKeyId.empty()) << "Should have access key ID if credentials available";
            EXPECT_FALSE(creds.secretAccessKey.empty()) << "Should have secret access key if credentials available";
        } catch (const AwsAuthError &) {
            // Expected if no credentials are available - this is fine
        }
    } catch (const AwsAuthError & e) {
        GTEST_SKIP() << "AWS authentication failed: " << e.what();
    }
}

// Parameterized test for environment credentials
struct EnvironmentCredentialTestCase
{
    std::string accessKeyId;
    std::string secretAccessKey;
    std::optional<std::string> sessionToken;
    std::string description;
};

class AwsCredentialProviderEnvTest : public AwsCredentialProviderTest,
                                     public ::testing::WithParamInterface<EnvironmentCredentialTestCase>
{};

TEST_P(AwsCredentialProviderEnvTest, getCredentialsFromEnvironment)
{
    const auto & testCase = GetParam();

    // Set up test environment variables
    setenv("AWS_ACCESS_KEY_ID", testCase.accessKeyId.c_str(), 1);
    setenv("AWS_SECRET_ACCESS_KEY", testCase.secretAccessKey.c_str(), 1);
    if (testCase.sessionToken.has_value()) {
        setenv("AWS_SESSION_TOKEN", testCase.sessionToken->c_str(), 1);
    }

    try {
        auto provider = AwsCredentialProvider::createDefault();
        ASSERT_NE(provider, nullptr);

        auto creds = provider->getCredentials();
        EXPECT_EQ(creds.accessKeyId, testCase.accessKeyId);
        EXPECT_EQ(creds.secretAccessKey, testCase.secretAccessKey);

        if (testCase.sessionToken.has_value()) {
            EXPECT_TRUE(creds.sessionToken.has_value());
            EXPECT_EQ(*creds.sessionToken, *testCase.sessionToken);
        } else {
            EXPECT_FALSE(creds.sessionToken.has_value());
        }
    } catch (const AwsAuthError & e) {
        // Clean up first
        unsetenv("AWS_ACCESS_KEY_ID");
        unsetenv("AWS_SECRET_ACCESS_KEY");
        unsetenv("AWS_SESSION_TOKEN");
        GTEST_SKIP() << "AWS authentication failed: " << e.what();
    }

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
}

INSTANTIATE_TEST_SUITE_P(
    EnvironmentCredentials,
    AwsCredentialProviderEnvTest,
    ::testing::Values(
        EnvironmentCredentialTestCase{"test-access-key", "test-secret-key", "test-session-token", "with_session_token"},
        EnvironmentCredentialTestCase{"test-access-key-2", "test-secret-key-2", std::nullopt, "without_session_token"}),
    [](const ::testing::TestParamInfo<EnvironmentCredentialTestCase> & info) { return info.param.description; });

TEST_F(AwsCredentialProviderTest, multipleProvidersIndependent)
{
    // Test that multiple providers can be created independently
    try {
        auto provider1 = AwsCredentialProvider::createDefault();
        auto provider2 = AwsCredentialProvider::createDefault(); // Use default for both

        EXPECT_NE(provider1, nullptr);
        EXPECT_NE(provider2, nullptr);
        EXPECT_NE(provider1.get(), provider2.get());
    } catch (const AwsAuthError & e) {
        GTEST_SKIP() << "AWS authentication failed: " << e.what();
    }
}

// Parameterized test for toHttpsUrl() method
struct S3ToHttpsConversionTestCase
{
    ParsedS3URL input;
    ParsedURL expected;
    std::string description;
};

class S3ToHttpsConversionTest : public ::testing::WithParamInterface<S3ToHttpsConversionTestCase>,
                                public ::testing::Test
{};

TEST_P(S3ToHttpsConversionTest, ConvertsCorrectly)
{
    const auto & testCase = GetParam();
    auto result = testCase.input.toHttpsUrl();
    EXPECT_EQ(result, testCase.expected) << "Failed for: " << testCase.description;
}

INSTANTIATE_TEST_SUITE_P(
    S3ToHttpsConversion,
    S3ToHttpsConversionTest,
    ::testing::Values(
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "my-bucket",
                .key = "my-key.txt",
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "s3.us-east-1.amazonaws.com"},
                .path = "/my-bucket/my-key.txt",
            },
            "basic_s3_default_region",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "prod-cache",
                .key = "nix/store/abc123.nar.xz",
                .region = "eu-west-1",
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "s3.eu-west-1.amazonaws.com"},
                .path = "/prod-cache/nix/store/abc123.nar.xz",
            },
            "with_eu_west_1_region",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "bucket",
                .key = "key",
                .scheme = "http",
                .endpoint = ParsedURL::Authority{.host = "custom.s3.com"},
            },
            ParsedURL{
                .scheme = "http",
                .authority = ParsedURL::Authority{.host = "custom.s3.com"},
                .path = "/bucket/key",
            },
            "custom_endpoint_authority",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "bucket",
                .key = "key",
                .endpoint =
                    ParsedURL{
                        .scheme = "http",
                        .authority = ParsedURL::Authority{.host = "server", .port = 9000},
                    },
            },
            ParsedURL{
                .scheme = "http",
                .authority = ParsedURL::Authority{.host = "server", .port = 9000},
                .path = "/bucket/key",
            },
            "custom_endpoint_with_port",
        },
        S3ToHttpsConversionTestCase{
            ParsedS3URL{
                .bucket = "bucket",
                .key = "path/to/file.txt",
                .region = "ap-southeast-2",
                .scheme = "https",
            },
            ParsedURL{
                .scheme = "https",
                .authority = ParsedURL::Authority{.host = "s3.ap-southeast-2.amazonaws.com"},
                .path = "/bucket/path/to/file.txt",
            },
            "complex_path_and_region",
        }),
    [](const ::testing::TestParamInfo<S3ToHttpsConversionTestCase> & info) { return info.param.description; });

} // namespace nix

#endif
