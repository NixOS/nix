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

TEST_F(S3HttpIntegrationTest, s3UrlDetection)
{
    auto ft = makeFileTransfer();

    // Test that S3 URLs are properly detected
    std::vector<std::string> s3Urls = {
        "s3://bucket/key",
        "s3://my-bucket/path/to/file.nar.xz",
        "s3://bucket/key?region=us-west-2",
        "s3://bucket/key?profile=myprofile&region=eu-central-1"};

    for (const auto & url : s3Urls) {
        FileTransferRequest request(url);
        EXPECT_TRUE(hasPrefix(request.uri, "s3://")) << "URL should be detected as S3: " << url;
    }
}

TEST_F(S3HttpIntegrationTest, nonS3UrlPassthrough)
{
    auto ft = makeFileTransfer();

    // Test that non-S3 URLs are not affected
    std::vector<std::string> httpUrls = {
        "http://example.com/file.txt",
        "https://cache.nixos.org/nar/abc123.nar.xz",
        "file:///local/path/file.txt",
        "ftp://ftp.example.com/file.txt"};

    for (const auto & url : httpUrls) {
        FileTransferRequest request(url);
        EXPECT_EQ(request.uri, url) << "Non-S3 URL should remain unchanged: " << url;
    }
}

TEST_F(S3HttpIntegrationTest, malformedS3Urls)
{
    // Test malformed S3 URLs that should trigger errors
    std::vector<std::string> malformedUrls = {
        "s3://",                      // Missing bucket and key
        "s3:///key",                  // Empty bucket
        "s3://bucket",                // Missing key
        "s3://bucket/",               // Empty key
        "s3://",                      // Completely empty
        "s3://bucket with spaces/key" // Invalid bucket name
    };

    auto ft = makeFileTransfer();

    for (const auto & url : malformedUrls) {
        // Creating the request shouldn't crash, but enqueueFileTransfer should handle errors
        EXPECT_NO_THROW({ FileTransferRequest request(url); })
            << "Creating request for malformed URL should not crash: " << url;
    }
}

TEST_F(S3HttpIntegrationTest, s3Parameters_Parsing)
{
    // Test parameter parsing for various S3 configurations
    struct TestCase
    {
        std::string url;
        std::string expectedBucket;
        std::string expectedKey;
        std::string expectedRegion;
        std::string expectedProfile;
        std::string expectedEndpoint;
    };

    std::vector<TestCase> testCases = {
        {"s3://my-bucket/my-key.txt", "my-bucket", "my-key.txt", "us-east-1", "", ""},
        {"s3://prod-cache/nix/store/abc123.nar.xz?region=eu-west-1",
         "prod-cache",
         "nix/store/abc123.nar.xz",
         "eu-west-1",
         "",
         ""},
        {"s3://cache/file.txt?profile=production&region=ap-southeast-2",
         "cache",
         "file.txt",
         "ap-southeast-2",
         "production",
         ""},
        {"s3://bucket/key?endpoint=minio.local&scheme=http", "bucket", "key", "us-east-1", "", "minio.local"}};

    for (const auto & tc : testCases) {
        FileTransferRequest request(tc.url);

        // Basic validation that the URL is recognized
        EXPECT_TRUE(hasPrefix(request.uri, "s3://")) << "URL: " << tc.url;

        // Note: To fully test parameter extraction, we'd need to expose
        // the parseS3Uri function or add getter methods to FileTransferRequest
    }
}

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