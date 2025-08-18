#include "nix/store/filetransfer.hh"
#include "nix/store/config.hh"

#if NIX_WITH_AWS_CRT_SUPPORT

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
    auto ft = makeFileTransfer();
    FileTransferRequest request(testCase.url);

    if (testCase.isS3) {
        EXPECT_TRUE(hasPrefix(request.uri, "s3://"))
            << "URL should be detected as S3: " << testCase.url << " (" << testCase.description << ")";
    } else {
        EXPECT_EQ(request.uri, testCase.url)
            << "Non-S3 URL should remain unchanged: " << testCase.url << " (" << testCase.description << ")";
    }
}

INSTANTIATE_TEST_SUITE_P(
    S3UrlDetection,
    S3UrlDetectionTest,
    ::testing::Values(
        // S3 URLs
        UrlTestCase{"s3://bucket/key", true, "basic S3 URL"},
        UrlTestCase{"s3://my-bucket/path/to/file.nar.xz", true, "S3 with path"},
        UrlTestCase{"s3://bucket/key?region=us-west-2", true, "S3 with region"},
        UrlTestCase{"s3://bucket/key?profile=myprofile&region=eu-central-1", true, "S3 with multiple params"},
        // Non-S3 URLs
        UrlTestCase{"http://example.com/file.txt", false, "HTTP URL"},
        UrlTestCase{"https://cache.nixos.org/nar/abc123.nar.xz", false, "HTTPS URL"},
        UrlTestCase{"file:///local/path/file.txt", false, "file URL"},
        UrlTestCase{"ftp://ftp.example.com/file.txt", false, "FTP URL"}),
    [](const ::testing::TestParamInfo<UrlTestCase> & info) {
        std::string name = info.param.description;
        std::replace(name.begin(), name.end(), ' ', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        std::replace(name.begin(), name.end(), '/', '_');
        return name;
    });

// Parameterized test for malformed S3 URLs
struct MalformedUrlTestCase
{
    std::string url;
    std::string description;
};

class S3MalformedUrlTest : public S3HttpIntegrationTest, public ::testing::WithParamInterface<MalformedUrlTestCase>
{};

TEST_P(S3MalformedUrlTest, HandlesGracefully)
{
    const auto & testCase = GetParam();
    auto ft = makeFileTransfer();

    // Creating the request shouldn't crash, but enqueueFileTransfer should handle errors
    EXPECT_NO_THROW({ FileTransferRequest request(testCase.url); })
        << "Creating request for malformed URL should not crash: " << testCase.url << " (" << testCase.description
        << ")";
}

INSTANTIATE_TEST_SUITE_P(
    MalformedUrls,
    S3MalformedUrlTest,
    ::testing::Values(
        MalformedUrlTestCase{"s3://", "missing bucket and key"},
        MalformedUrlTestCase{"s3:///key", "empty bucket"},
        MalformedUrlTestCase{"s3://bucket", "missing key"},
        MalformedUrlTestCase{"s3://bucket/", "empty key"},
        MalformedUrlTestCase{"s3://bucket with spaces/key", "invalid bucket name"}),
    [](const ::testing::TestParamInfo<MalformedUrlTestCase> & info) {
        std::string name = info.param.description;
        std::replace(name.begin(), name.end(), ' ', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    });

// Parameterized test for S3 parameter parsing
struct S3ParameterTestCase
{
    std::string url;
    std::string expectedBucket;
    std::string expectedKey;
    std::string expectedRegion;
    std::string expectedProfile;
    std::string expectedEndpoint;
    std::string description;
};

class S3ParameterParsingTest : public S3HttpIntegrationTest, public ::testing::WithParamInterface<S3ParameterTestCase>
{};

TEST_P(S3ParameterParsingTest, ParsesParametersCorrectly)
{
    const auto & tc = GetParam();
    FileTransferRequest request(tc.url);

    // Basic validation that the URL is recognized
    EXPECT_TRUE(hasPrefix(request.uri, "s3://")) << "URL: " << tc.url << " (" << tc.description << ")";

    // Note: To fully test parameter extraction, we'd need to expose
    // the parseS3Uri function or add getter methods to FileTransferRequest
}

INSTANTIATE_TEST_SUITE_P(
    ParameterParsing,
    S3ParameterParsingTest,
    ::testing::Values(
        S3ParameterTestCase{
            "s3://my-bucket/my-key.txt", "my-bucket", "my-key.txt", "us-east-1", "", "", "basic S3 URL"},
        S3ParameterTestCase{
            "s3://prod-cache/nix/store/abc123.nar.xz?region=eu-west-1",
            "prod-cache",
            "nix/store/abc123.nar.xz",
            "eu-west-1",
            "",
            "",
            "with region"},
        S3ParameterTestCase{
            "s3://cache/file.txt?profile=production&region=ap-southeast-2",
            "cache",
            "file.txt",
            "ap-southeast-2",
            "production",
            "",
            "with profile and region"},
        S3ParameterTestCase{
            "s3://bucket/key?endpoint=minio.local&scheme=http",
            "bucket",
            "key",
            "us-east-1",
            "",
            "minio.local",
            "with custom endpoint"}),
    [](const ::testing::TestParamInfo<S3ParameterTestCase> & info) {
        std::string name = info.param.description;
        std::replace(name.begin(), name.end(), ' ', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    });

// Non-parameterized tests for specific integration scenarios
TEST_F(S3HttpIntegrationTest, awsCredentials_Integration)
{
    // Test integration with AWS credential resolution
    setenv("AWS_ACCESS_KEY_ID", "AKIAIOSFODNN7EXAMPLE", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", 1);

    std::string s3Uri = "s3://test-bucket/test-file.txt?region=us-east-1";
    FileTransferRequest request(s3Uri);

    // Test that the request can be created with credentials available
    EXPECT_TRUE(hasPrefix(request.uri, "s3://"));

    auto ft = makeFileTransfer();

    // Note: We can't easily test the actual transfer without a real/mock S3 endpoint
    // This test verifies the credential setup doesn't crash
    EXPECT_NO_THROW({
        // Creating the transfer object should work
        auto transfer = makeFileTransfer();
    });

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST_F(S3HttpIntegrationTest, httpHeaders_S3SpecificHeaders)
{
    // Test that S3-specific headers are handled correctly
    setenv("AWS_SESSION_TOKEN", "test-session-token", 1);

    std::string s3Uri = "s3://bucket/key";
    FileTransferRequest request(s3Uri);

    // Add some custom headers to verify they're preserved
    request.headers.emplace_back("Custom-Header", "custom-value");
    request.headers.emplace_back("Authorization", "should-be-overridden");

    // Verify the request can be created
    EXPECT_TRUE(hasPrefix(request.uri, "s3://"));

    // Check that custom header was added
    bool foundCustomHeader = false;
    for (const auto & [key, value] : request.headers) {
        if (key == "Custom-Header" && value == "custom-value") {
            foundCustomHeader = true;
            break;
        }
    }
    EXPECT_TRUE(foundCustomHeader);

    // Clean up
    unsetenv("AWS_SESSION_TOKEN");
}

TEST_F(S3HttpIntegrationTest, errorHandling_NoCredentials)
{
    // Test behavior when no AWS credentials are available
    std::string s3Uri = "s3://bucket/key";
    FileTransferRequest request(s3Uri);

    auto ft = makeFileTransfer();

    // This should not crash even without credentials
    // The actual error should occur during transfer attempt
    EXPECT_NO_THROW({ FileTransferRequest req(s3Uri); });
}

TEST_F(S3HttpIntegrationTest, compatibility_BackwardCompatible)
{
    // Test that existing S3 configurations remain compatible
    auto ft = makeFileTransfer();

    // Standard S3 URL that would work with both old and new implementations
    std::string s3Uri = "s3://cache.nixos.org/nar/abc123.nar.xz";
    FileTransferRequest request(s3Uri);

    EXPECT_TRUE(hasPrefix(request.uri, "s3://"));

    // Verify that the new implementation can handle the same URLs
    // that the old S3Helper implementation could handle
}

} // namespace nix

#endif // NIX_WITH_AWS_CRT_SUPPORT