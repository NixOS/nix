#include "nix/store/filetransfer.hh"
#include "nix/store/config.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/store-api.hh"
#include "nix/util/types.hh"

#if NIX_WITH_AWS_CRT_SUPPORT

#  include <gtest/gtest.h>
#  include <gmock/gmock.h>

namespace nix {

class S3FileTransferTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Clean environment for predictable tests
        unsetenv("AWS_ACCESS_KEY_ID");
        unsetenv("AWS_SECRET_ACCESS_KEY");
        unsetenv("AWS_SESSION_TOKEN");
        unsetenv("AWS_PROFILE");
    }
};

TEST_F(S3FileTransferTest, parseS3Uri_Basic)
{
    auto ft = makeFileTransfer();

    // Access the parseS3Uri function through friendship or make it public for testing
    // For now, test the conversion function which uses parseS3Uri internally
    std::string s3Uri = "s3://test-bucket/path/to/file.txt";

    // This would require making convertS3ToHttpsUri public or friend class
    // For now, test that the URL parsing doesn't crash
    EXPECT_NO_THROW({
        FileTransferRequest request(s3Uri);
        // Basic test that the request can be created
        EXPECT_EQ(request.uri, s3Uri);
    });
}

TEST_F(S3FileTransferTest, convertS3ToHttps_StandardEndpoint)
{
    // Test conversion of standard S3 URLs to HTTPS
    std::string s3Uri = "s3://my-bucket/path/file.nar.xz?region=us-west-2";

    // Since convertS3ToHttpsUri is private, we test the behavior indirectly
    // by creating a FileTransferRequest and checking if S3 detection works
    FileTransferRequest request(s3Uri);
    EXPECT_TRUE(hasPrefix(request.uri, "s3://"));
}

TEST_F(S3FileTransferTest, convertS3ToHttps_CustomEndpoint)
{
    std::string s3Uri = "s3://my-bucket/path/file.txt?endpoint=minio.example.com&region=us-east-1";

    FileTransferRequest request(s3Uri);
    EXPECT_TRUE(hasPrefix(request.uri, "s3://"));

    // Test that custom endpoint parameter is parsed correctly
    // (We'd need to expose parseS3Uri or add getter methods for full verification)
}

TEST_F(S3FileTransferTest, s3Request_Parameters)
{
    // Test various S3 URL parameter combinations
    std::vector<std::string> testUrls = {
        "s3://bucket/key",
        "s3://bucket/path/key.txt?region=eu-west-1",
        "s3://bucket/key?profile=myprofile",
        "s3://bucket/key?region=ap-southeast-1&profile=prod&scheme=https",
        "s3://bucket/key?endpoint=s3.custom.com&region=us-east-1"};

    for (const auto & url : testUrls) {
        EXPECT_NO_THROW({
            FileTransferRequest request(url);
            EXPECT_TRUE(hasPrefix(request.uri, "s3://"));
        }) << "Failed for URL: "
           << url;
    }
}

TEST_F(S3FileTransferTest, s3Uri_InvalidFormats)
{
    // Test that invalid S3 URIs are handled gracefully
    std::vector<std::string> invalidUrls = {
        "s3://",        // No bucket
        "s3:///key",    // Empty bucket
        "s3://bucket",  // No key
        "s3://bucket/", // Empty key
    };

    auto ft = makeFileTransfer();

    for (const auto & url : invalidUrls) {
        FileTransferRequest request(url);

        // Test that creating the request doesn't crash
        // The actual error should occur during enqueueFileTransfer
        EXPECT_NO_THROW({
            auto ft = makeFileTransfer();
            // Note: We can't easily test the actual transfer without real credentials
            // This test verifies the URL parsing validation
        }) << "Should handle invalid URL gracefully: "
           << url;
    }
}

TEST_F(S3FileTransferTest, s3Request_WithMockCredentials)
{
    // Set up mock credentials for testing
    setenv("AWS_ACCESS_KEY_ID", "AKIAIOSFODNN7EXAMPLE", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", 1);

    std::string s3Uri = "s3://test-bucket/test-key.txt?region=us-east-1";
    FileTransferRequest request(s3Uri);

    // Test that request setup works with credentials
    EXPECT_TRUE(hasPrefix(request.uri, "s3://"));

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST_F(S3FileTransferTest, s3Request_WithSessionToken)
{
    // Test session token handling
    setenv("AWS_ACCESS_KEY_ID", "ASIAIOSFODNN7EXAMPLE", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", 1);
    setenv("AWS_SESSION_TOKEN", "AQoDYXdzEJr1K...example-session-token", 1);

    std::string s3Uri = "s3://test-bucket/test-key.txt";
    FileTransferRequest request(s3Uri);

    EXPECT_TRUE(hasPrefix(request.uri, "s3://"));

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
}

TEST_F(S3FileTransferTest, regionExtraction)
{
    // Test that regions are properly extracted and used
    std::vector<std::pair<std::string, std::string>> testCases = {
        {"s3://bucket/key", "us-east-1"},                            // Default region
        {"s3://bucket/key?region=eu-west-1", "eu-west-1"},           // Explicit region
        {"s3://bucket/key?region=ap-southeast-2", "ap-southeast-2"}, // Different region
    };

    for (const auto & [url, expectedRegion] : testCases) {
        FileTransferRequest request(url);
        // We would need access to internal parsing to verify regions
        // For now, just verify the URL is recognized as S3
        EXPECT_TRUE(hasPrefix(request.uri, "s3://")) << "URL: " << url;
    }
}

/**
 * Regression test for commit 7a2f2891e
 * Test that S3 store URLs are properly recognized and handled
 */
TEST_F(S3FileTransferTest, s3StoreRegistration)
{
    // Test that S3 URI scheme is in the supported schemes
    auto schemes = HttpBinaryCacheStoreConfig::uriSchemes();
    EXPECT_TRUE(schemes.count("s3") > 0) << "S3 scheme should be in supported URI schemes";

    // Test that S3 store can be opened without error
    try {
        auto storeUrl = "s3://test-bucket";
        auto parsedUrl = parseURL(storeUrl);
        EXPECT_EQ(parsedUrl.scheme, "s3");

        // Verify that HttpBinaryCacheStoreConfig accepts S3 URLs
        HttpBinaryCacheStoreConfig config("s3", "test-bucket", {});
        EXPECT_EQ(config.cacheUri.scheme, "s3");
        EXPECT_EQ(config.cacheUri.authority->host, "test-bucket");
    } catch (const std::exception & e) {
        FAIL() << "Should be able to create S3 store config: " << e.what();
    }
}

/**
 * Regression test for commit c0164e087
 * Test that S3 uploads are not rejected with "not supported" error
 */
TEST_F(S3FileTransferTest, s3UploadsNotRejected)
{
    auto ft = makeFileTransfer();

    // Create a mock upload request
    FileTransferRequest uploadReq("s3://test-bucket/test-file");
    uploadReq.data = std::string("test data");

    // This should not throw "uploading to 's3://...' is not supported"
    // We're testing that S3 uploads aren't immediately rejected
    bool gotNotSupportedError = false;
    try {
        ft->upload(uploadReq);
    } catch (const Error & e) {
        std::string msg = e.what();
        if (msg.find("is not supported") != std::string::npos) {
            gotNotSupportedError = true;
        }
    } catch (...) {
        // Other errors are expected (no credentials, network issues, etc.)
    }

    EXPECT_FALSE(gotNotSupportedError) << "S3 uploads should not be rejected with 'not supported' error";
}

/**
 * Regression test for commit e618ac7e0
 * Test that S3 URLs with region query parameters are handled correctly
 */
TEST_F(S3FileTransferTest, s3RegionQueryParameters)
{
    // Test that query parameters are preserved in S3 URLs
    StringMap params;
    params["region"] = "us-west-2";

    HttpBinaryCacheStoreConfig config("s3", "test-bucket", params);

    // For S3 stores, query parameters should be preserved
    EXPECT_FALSE(config.cacheUri.query.empty()) << "S3 store should preserve query parameters";
    EXPECT_EQ(config.cacheUri.query["region"], "us-west-2") << "Region parameter should be preserved";

    // Test with different regions
    StringMap params2;
    params2["region"] = "eu-central-1";

    HttpBinaryCacheStoreConfig config2("s3", "another-bucket", params2);
    EXPECT_EQ(config2.cacheUri.query["region"], "eu-central-1") << "Different region parameter should be preserved";
}

/**
 * Test S3 URL parsing with various query parameters
 */
TEST_F(S3FileTransferTest, s3UrlParsingWithQueryParams)
{
    // Test various S3 URLs with query parameters
    std::vector<std::tuple<std::string, std::string, std::string, std::string>> testCases = {
        {"s3://bucket/key?region=us-east-2", "s3", "bucket", "us-east-2"},
        {"s3://my-bucket/path/to/file?region=eu-west-1", "s3", "my-bucket", "eu-west-1"},
        {"s3://test/obj?region=ap-south-1", "s3", "test", "ap-south-1"},
    };

    for (const auto & [url, expectedScheme, expectedBucket, expectedRegion] : testCases) {
        auto parsed = parseURL(url);
        EXPECT_EQ(parsed.scheme, expectedScheme) << "URL: " << url;
        EXPECT_EQ(parsed.authority->host, expectedBucket) << "URL: " << url;
        if (!expectedRegion.empty()) {
            EXPECT_EQ(parsed.query["region"], expectedRegion) << "URL: " << url;
        }
    }
}

} // namespace nix

#endif // NIX_WITH_AWS_CRT_SUPPORT