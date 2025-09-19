#include "nix/store/filetransfer.hh"
#include "nix/store/config.hh"
#include "nix/store/s3-binary-cache-store.hh"
#include "nix/store/s3-url.hh"

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

/**
 * Regression test: Verify that S3 uploads are supported (not rejected with "not supported" error)
 * This test ensures the S3 upload functionality is properly implemented
 */
TEST_F(S3FileTransferTest, s3UploadsAreSupported)
{
    auto ft = makeFileTransfer();

    // Parse S3 URL and convert to HTTPS for curl-based transfer
    auto s3Url = ParsedS3URL::parse(parseURL("s3://test-bucket/test-file"));
    auto httpsUrl = s3Url.toHttpsUrl();

    // Create a mock upload request
    FileTransferRequest uploadReq(httpsUrl);
    uploadReq.data = std::string("test data");

    // This should not throw "uploading to '...' is not supported"
    // The request should be accepted (though it may fail later due to auth/network)
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

    EXPECT_FALSE(gotNotSupportedError) << "S3 uploads should be supported";
}

/**
 * Test that S3BinaryCacheStoreConfig properly stores region parameters
 */
TEST_F(S3FileTransferTest, regionParametersArePreserved)
{
    StringMap params;
    params["region"] = "us-west-2";

    S3BinaryCacheStoreConfig config("s3", "test-bucket", params);

    EXPECT_EQ(config.cacheUri.query["region"], "us-west-2");

    // Test with a different region
    StringMap params2;
    params2["region"] = "eu-central-1";

    S3BinaryCacheStoreConfig config2("s3", "another-bucket", params2);
    EXPECT_EQ(config2.cacheUri.query["region"], "eu-central-1");
}

/**
 * Test that S3 scheme registration works correctly
 */
TEST_F(S3FileTransferTest, s3SchemeIsRegistered)
{
    // S3 should be registered in S3BinaryCacheStoreConfig
    auto s3Schemes = S3BinaryCacheStoreConfig::uriSchemes();
    EXPECT_TRUE(s3Schemes.count("s3") > 0) << "S3 scheme should be registered";

    // S3 should NOT be in HttpBinaryCacheStoreConfig
    auto httpSchemes = HttpBinaryCacheStoreConfig::uriSchemes();
    EXPECT_FALSE(httpSchemes.count("s3") > 0) << "S3 should not be in HTTP config schemes";
}

} // namespace nix

#endif
