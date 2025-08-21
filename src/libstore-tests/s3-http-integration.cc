#include "nix/store/filetransfer.hh"
#include "nix/store/config.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/s3-binary-cache-store.hh"

#if NIX_WITH_S3_SUPPORT

#  include <gtest/gtest.h>
#  include <gmock/gmock.h>

namespace nix {

class S3HttpIntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Set up clean test environment
        unsetenv("AWS_ACCESS_KEY_ID");
        unsetenv("AWS_SECRET_ACCESS_KEY");
        unsetenv("AWS_SESSION_TOKEN");
        unsetenv("AWS_PROFILE");
    }
};

// Parameterized test for S3 URL detection
struct UrlTestCase
{
    std::string url;
    bool isS3;
    std::string description;
};

class S3UrlDetectionTest : public S3HttpIntegrationTest, public ::testing::WithParamInterface<UrlTestCase>
{};

TEST_P(S3UrlDetectionTest, DetectsUrlCorrectly)
{
    const auto & testCase = GetParam();
    auto parsedUrl = parseURL(testCase.url);
    FileTransferRequest request(parsedUrl);

    if (testCase.isS3) {
        EXPECT_EQ(request.uri.scheme(), "s3") << "URL should be detected as S3: " << testCase.url;
    } else {
        EXPECT_EQ(request.uri.to_string(), testCase.url) << "Non-S3 URL should remain unchanged: " << testCase.url;
    }
}

INSTANTIATE_TEST_SUITE_P(
    UrlDetection,
    S3UrlDetectionTest,
    ::testing::Values(
        // S3 URLs
        UrlTestCase{"s3://bucket/key", true, "basic_s3"},
        UrlTestCase{"s3://my-bucket/path/to/file.nar.xz", true, "s3_with_path"},
        UrlTestCase{"s3://bucket/key?region=us-west-2", true, "s3_with_region"},
        UrlTestCase{"s3://bucket/key?profile=myprofile&region=eu-central-1", true, "s3_multiple_params"},
        // Non-S3 URLs
        UrlTestCase{"http://example.com/file.txt", false, "http_url"},
        UrlTestCase{"https://cache.nixos.org/nar/abc123.nar.xz", false, "https_url"},
        UrlTestCase{"file:///local/path/file.txt", false, "file_url"},
        UrlTestCase{"ftp://ftp.example.com/file.txt", false, "ftp_url"}),
    [](const ::testing::TestParamInfo<UrlTestCase> & info) { return info.param.description; });

// Parameterized test for malformed S3 URLs
class S3MalformedUrlTest : public S3HttpIntegrationTest, public ::testing::WithParamInterface<std::string>
{};

TEST_P(S3MalformedUrlTest, HandlesGracefully)
{
    const auto & url = GetParam();

    // These URLs might be malformed and parseURL might throw
    try {
        auto parsedUrl = parseURL(url);
        FileTransferRequest request(parsedUrl);
        EXPECT_TRUE(request.uri.scheme() == "s3" || request.uri.to_string() == url);
    } catch (const BadURL & e) {
        // Expected for some malformed URLs
        SUCCEED() << "Correctly rejected malformed URL: " << url;
    }
}

INSTANTIATE_TEST_SUITE_P(
    MalformedUrls,
    S3MalformedUrlTest,
    ::testing::Values(
        "s3://",                      // Missing bucket and key
        "s3:///key",                  // Empty bucket
        "s3://bucket",                // Missing key
        "s3://bucket/",               // Empty key
        "s3://bucket with spaces/key" // Invalid bucket name
        ),
    [](const ::testing::TestParamInfo<std::string> & info) {
        std::string name = info.param;
        if (name == "s3://")
            return "completely_empty";
        if (name == "s3:///key")
            return "empty_bucket";
        if (name == "s3://bucket")
            return "missing_key";
        if (name == "s3://bucket/")
            return "empty_key";
        if (name.find("spaces") != std::string::npos)
            return "spaces_in_bucket";
        return "unknown";
    });

// Parameterized test for S3 parameter parsing
struct S3ParamsTestCase
{
    std::string url;
    std::string expectedBucket;
    std::string expectedKey;
    std::string expectedRegion;
    std::string description;
};

class S3ParameterParsingTest : public S3HttpIntegrationTest, public ::testing::WithParamInterface<S3ParamsTestCase>
{};

TEST_P(S3ParameterParsingTest, ParsesParametersCorrectly)
{
    const auto & testCase = GetParam();

    // Parse the URL
    auto parsed = parseURL(testCase.url);

    // Check basic parsing
    EXPECT_EQ(parsed.scheme, "s3");
    EXPECT_EQ(parsed.authority->host, testCase.expectedBucket);

    // Check region parameter if expected
    if (!testCase.expectedRegion.empty() && testCase.expectedRegion != "us-east-1") {
        EXPECT_EQ(parsed.query["region"], testCase.expectedRegion);
    }
}

INSTANTIATE_TEST_SUITE_P(
    ParameterParsing,
    S3ParameterParsingTest,
    ::testing::Values(
        S3ParamsTestCase{"s3://my-bucket/my-key.txt", "my-bucket", "my-key.txt", "us-east-1", "basic_s3_url"},
        S3ParamsTestCase{
            "s3://prod-cache/nix/store/abc123.nar.xz?region=eu-west-1",
            "prod-cache",
            "nix/store/abc123.nar.xz",
            "eu-west-1",
            "s3_with_region"},
        S3ParamsTestCase{
            "s3://cache/file.txt?profile=production&region=ap-southeast-2",
            "cache",
            "file.txt",
            "ap-southeast-2",
            "s3_with_profile_and_region"},
        S3ParamsTestCase{
            "s3://bucket/key?endpoint=minio.local&scheme=http",
            "bucket",
            "key",
            "us-east-1",
            "s3_with_custom_endpoint"}),
    [](const ::testing::TestParamInfo<S3ParamsTestCase> & info) { return info.param.description; });

// Non-parameterized tests for complex scenarios

TEST_F(S3HttpIntegrationTest, s3RequestHeaders)
{
    // Test that S3 requests get proper headers set
    setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "secret", 1);

    FileTransferRequest request(parseURL("s3://test-bucket/test-key.txt?region=us-west-2"));

    // Headers should be added
    // Note: We can't test the actual AWS SigV4 headers without exposing internals
    EXPECT_EQ(request.uri.scheme(), "s3");

    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST_F(S3HttpIntegrationTest, s3StoreUrlHandling)
{
    // Test that S3 store URLs are properly handled
    try {
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
    } catch (const std::exception & e) {
        FAIL() << "Should be able to create S3 store config: " << e.what();
    }
}

TEST_F(S3HttpIntegrationTest, httpStoreAcceptsS3Urls)
{
    // Test that HttpBinaryCacheStoreConfig handles S3 URLs
    auto schemes = S3BinaryCacheStoreConfig::uriSchemes();
    EXPECT_TRUE(schemes.count("s3") > 0) << "S3 scheme should be supported";

    // Verify HttpBinaryCacheStoreConfig doesn't directly list S3
    auto httpSchemes = HttpBinaryCacheStoreConfig::uriSchemes();
    EXPECT_FALSE(httpSchemes.count("s3") > 0) << "HTTP store shouldn't directly list S3 scheme";
}

TEST_F(S3HttpIntegrationTest, s3UploadRequest)
{
    // Test that S3 upload requests are properly configured
    FileTransferRequest uploadReq(parseURL("s3://test-bucket/test-file"));
    uploadReq.data = std::string("test data");

    // Basic validation
    EXPECT_EQ(uploadReq.uri.scheme(), "s3");
    EXPECT_TRUE(uploadReq.data.has_value());
    EXPECT_EQ(*uploadReq.data, "test data");
}

TEST_F(S3HttpIntegrationTest, regionPriority)
{
    // Test that explicit region parameter takes precedence
    auto url = parseURL("s3://bucket/key?region=eu-west-1");
    FileTransferRequest request(url);

    EXPECT_EQ(url.query["region"], "eu-west-1");
}

TEST_F(S3HttpIntegrationTest, customHeaders)
{
    // Test that custom headers are preserved through request creation
    FileTransferRequest request(parseURL("s3://bucket/key"));
    request.headers.emplace_back("Custom-Header", "custom-value");
    request.headers.emplace_back("X-Test", "test-value");

    // Check that custom header was added
    bool foundCustomHeader = false;
    bool foundTestHeader = false;
    for (const auto & [key, value] : request.headers) {
        if (key == "Custom-Header" && value == "custom-value") {
            foundCustomHeader = true;
        }
        if (key == "X-Test" && value == "test-value") {
            foundTestHeader = true;
        }
    }

    EXPECT_TRUE(foundCustomHeader) << "Custom header should be preserved";
    EXPECT_TRUE(foundTestHeader) << "Test header should be preserved";
}

TEST_F(S3HttpIntegrationTest, s3ProfileHandling)
{
    // Test that profile parameter is handled correctly
    auto url = parseURL("s3://bucket/key?profile=production");
    FileTransferRequest request(url);

    EXPECT_EQ(url.query["profile"], "production");
}

TEST_F(S3HttpIntegrationTest, endpointOverride)
{
    // Test custom endpoint handling (e.g., for MinIO)
    auto url = parseURL("s3://bucket/key?endpoint=minio.local:9000&scheme=http");
    FileTransferRequest request(url);

    EXPECT_EQ(url.query["endpoint"], "minio.local:9000");
    EXPECT_EQ(url.query["scheme"], "http");
}

} // namespace nix

#endif // NIX_WITH_S3_SUPPORT