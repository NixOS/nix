#include "nix/store/filetransfer.hh"
#include "nix/store/config.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/store-api.hh"
#include "nix/util/types.hh"

#if NIX_WITH_S3_SUPPORT

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

// Note: Basic S3 URL parsing tests are covered by ParsedS3URL tests in s3.cc
// These tests focus on FileTransfer-specific S3 functionality

TEST_F(S3FileTransferTest, s3RequestWithMockCredentials)
{
    // Set up mock credentials for testing
    setenv("AWS_ACCESS_KEY_ID", "AKIAIOSFODNN7EXAMPLE", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", 1);

    auto s3Uri = parseURL("s3://test-bucket/test-key.txt?region=us-east-1");
    FileTransferRequest request(s3Uri);

    // Test that request setup works with credentials
    EXPECT_EQ(request.uri.scheme, "s3");

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST_F(S3FileTransferTest, s3RequestWithSessionToken)
{
    // Test session token handling
    setenv("AWS_ACCESS_KEY_ID", "ASIAIOSFODNN7EXAMPLE", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", 1);
    setenv("AWS_SESSION_TOKEN", "AQoDYXdzEJr1K...example-session-token", 1);

    auto s3Uri = parseURL("s3://test-bucket/test-key.txt");
    FileTransferRequest request(s3Uri);

    EXPECT_EQ(request.uri.scheme, "s3");

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
}

/**
 * Regression test for commit 7a2f2891e
 * Test that S3 store URLs are properly recognized and handled
 */
TEST_F(S3FileTransferTest, s3StoreRegistration)
{
    // Test that S3 URI scheme is in the supported schemes (now in S3BinaryCacheStoreConfig)
    auto s3Schemes = S3BinaryCacheStoreConfig::uriSchemes();
    EXPECT_TRUE(s3Schemes.count("s3") > 0) << "S3 scheme should be in S3BinaryCacheStoreConfig URI schemes";

    // Verify that HttpBinaryCacheStoreConfig does NOT include S3
    auto httpSchemes = HttpBinaryCacheStoreConfig::uriSchemes();
    EXPECT_FALSE(httpSchemes.count("s3") > 0) << "S3 scheme should NOT be in HttpBinaryCacheStoreConfig URI schemes";

    // Test that S3 store can be opened without error
    try {
        auto storeUrl = "s3://test-bucket";
        auto parsedUrl = parseURL(storeUrl);
        EXPECT_EQ(parsedUrl.scheme, "s3");

        // Verify that S3BinaryCacheStoreConfig accepts S3 URLs
        S3BinaryCacheStoreConfig config("s3", "test-bucket", {});
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
    FileTransferRequest uploadReq(parseURL("s3://test-bucket/test-file"));
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

    S3BinaryCacheStoreConfig config("s3", "test-bucket", params);

    // For S3 stores, query parameters should be preserved
    EXPECT_FALSE(config.cacheUri.query.empty()) << "S3 store should preserve query parameters";
    EXPECT_EQ(config.cacheUri.query["region"], "us-west-2") << "Region parameter should be preserved";

    // Test with different regions
    StringMap params2;
    params2["region"] = "eu-central-1";

    S3BinaryCacheStoreConfig config2("s3", "another-bucket", params2);
    EXPECT_EQ(config2.cacheUri.query["region"], "eu-central-1") << "Different region parameter should be preserved";
}

} // namespace nix

#endif // NIX_WITH_S3_SUPPORT