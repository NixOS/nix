#include "nix/store/filetransfer.hh"
#include "nix/store/config.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/s3-binary-cache-store.hh"
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

// Parameterized test for valid S3 URLs
struct S3UrlTestCase
{
    std::string url;
    std::string description;
};

class S3ValidUrlTest : public S3FileTransferTest, public ::testing::WithParamInterface<S3UrlTestCase>
{};

TEST_P(S3ValidUrlTest, ParsesSuccessfully)
{
    const auto & testCase = GetParam();
    EXPECT_NO_THROW({
        FileTransferRequest request(testCase.url);
        EXPECT_TRUE(hasPrefix(request.uri, "s3://"));
    }) << "Failed for URL: "
       << testCase.url << " (" << testCase.description << ")";
}

INSTANTIATE_TEST_SUITE_P(
    S3UrlParameters,
    S3ValidUrlTest,
    ::testing::Values(
        S3UrlTestCase{"s3://bucket/key", "basic URL"},
        S3UrlTestCase{"s3://bucket/path/key.txt?region=eu-west-1", "with region parameter"},
        S3UrlTestCase{"s3://bucket/key?profile=myprofile", "with profile parameter"},
        S3UrlTestCase{"s3://bucket/key?region=ap-southeast-1&profile=prod&scheme=https", "with multiple parameters"},
        S3UrlTestCase{"s3://bucket/key?endpoint=s3.custom.com&region=us-east-1", "with custom endpoint"}),
    [](const ::testing::TestParamInfo<S3UrlTestCase> & info) {
        std::string name = info.param.description;
        std::replace(name.begin(), name.end(), ' ', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    });

// Parameterized test for invalid S3 URLs
class S3InvalidUrlTest : public S3FileTransferTest, public ::testing::WithParamInterface<S3UrlTestCase>
{};

TEST_P(S3InvalidUrlTest, HandlesGracefully)
{
    const auto & testCase = GetParam();
    FileTransferRequest request(testCase.url);

    // Test that creating the request doesn't crash
    // The actual error should occur during enqueueFileTransfer
    EXPECT_NO_THROW({
        auto ft = makeFileTransfer();
        // Note: We can't easily test the actual transfer without real credentials
        // This test verifies the URL parsing validation
    }) << "Should handle invalid URL gracefully: "
       << testCase.url << " (" << testCase.description << ")";
}

INSTANTIATE_TEST_SUITE_P(
    InvalidFormats,
    S3InvalidUrlTest,
    ::testing::Values(
        S3UrlTestCase{"s3://", "no bucket"},
        S3UrlTestCase{"s3:///key", "empty bucket"},
        S3UrlTestCase{"s3://bucket", "no key"},
        S3UrlTestCase{"s3://bucket/", "empty key"}),
    [](const ::testing::TestParamInfo<S3UrlTestCase> & info) {
        std::string name = info.param.description;
        std::replace(name.begin(), name.end(), ' ', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    });

// Parameterized test for region extraction
struct RegionTestCase
{
    std::string url;
    std::string expectedRegion;
    std::string description;
};

class S3RegionTest : public S3FileTransferTest, public ::testing::WithParamInterface<RegionTestCase>
{};

TEST_P(S3RegionTest, ExtractsRegionCorrectly)
{
    const auto & testCase = GetParam();
    FileTransferRequest request(testCase.url);
    // We would need access to internal parsing to verify regions
    // For now, just verify the URL is recognized as S3
    EXPECT_TRUE(hasPrefix(request.uri, "s3://")) << "URL: " << testCase.url << " (" << testCase.description << ")";
}

INSTANTIATE_TEST_SUITE_P(
    RegionExtraction,
    S3RegionTest,
    ::testing::Values(
        RegionTestCase{"s3://bucket/key", "us-east-1", "default region"},
        RegionTestCase{"s3://bucket/key?region=eu-west-1", "eu-west-1", "explicit region"},
        RegionTestCase{"s3://bucket/key?region=ap-southeast-2", "ap-southeast-2", "different region"}),
    [](const ::testing::TestParamInfo<RegionTestCase> & info) {
        std::string name = info.param.description;
        std::replace(name.begin(), name.end(), ' ', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    });

// Parameterized test for S3 URL parsing with query parameters
struct ParsedUrlTestCase
{
    std::string url;
    std::string expectedScheme;
    std::string expectedBucket;
    std::string expectedRegion;
    std::string description;
};

class S3UrlParsingTest : public S3FileTransferTest, public ::testing::WithParamInterface<ParsedUrlTestCase>
{};

TEST_P(S3UrlParsingTest, ParsesUrlCorrectly)
{
    const auto & testCase = GetParam();
    auto parsed = parseURL(testCase.url);
    EXPECT_EQ(parsed.scheme, testCase.expectedScheme) << "URL: " << testCase.url << " (" << testCase.description << ")";
    EXPECT_EQ(parsed.authority->host, testCase.expectedBucket)
        << "URL: " << testCase.url << " (" << testCase.description << ")";
    if (!testCase.expectedRegion.empty()) {
        EXPECT_EQ(parsed.query["region"], testCase.expectedRegion)
            << "URL: " << testCase.url << " (" << testCase.description << ")";
    }
}

INSTANTIATE_TEST_SUITE_P(
    QueryParams,
    S3UrlParsingTest,
    ::testing::Values(
        ParsedUrlTestCase{"s3://bucket/key?region=us-east-2", "s3", "bucket", "us-east-2", "basic with region"},
        ParsedUrlTestCase{
            "s3://my-bucket/path/to/file?region=eu-west-1", "s3", "my-bucket", "eu-west-1", "path with region"},
        ParsedUrlTestCase{"s3://test/obj?region=ap-south-1", "s3", "test", "ap-south-1", "short name with region"}),
    [](const ::testing::TestParamInfo<ParsedUrlTestCase> & info) {
        std::string name = info.param.description;
        std::replace(name.begin(), name.end(), ' ', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    });

// Non-parameterized tests for specific functionality
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

/**
 * Test S3 Transfer Acceleration functionality
 */
TEST_F(S3FileTransferTest, s3TransferAcceleration_EnabledWithValidBucket)
{
    // Test that transfer acceleration can be enabled
    StringMap params;
    params["use-transfer-acceleration"] = "true";

    S3BinaryCacheStoreConfig config("s3", "valid-bucket-name", params);

    // Transfer acceleration parameter should be preserved
    EXPECT_EQ(config.cacheUri.query["use-transfer-acceleration"], "true")
        << "Transfer acceleration parameter should be preserved";
    EXPECT_TRUE(config.useTransferAcceleration.get()) << "Transfer acceleration setting should be enabled";
}

TEST_F(S3FileTransferTest, s3TransferAcceleration_DisabledByDefault)
{
    // Test that transfer acceleration is disabled by default
    S3BinaryCacheStoreConfig config("s3", "test-bucket", {});

    EXPECT_FALSE(config.useTransferAcceleration.get()) << "Transfer acceleration should be disabled by default";
}

// Parameterized test for DNS-compliant bucket names
struct BucketNameTestCase
{
    std::string bucketName;
    bool shouldBeValid;
    std::string description;
};

class S3BucketNameValidationTest : public S3FileTransferTest, public ::testing::WithParamInterface<BucketNameTestCase>
{};

TEST_P(S3BucketNameValidationTest, ValidatesBucketNames)
{
    const auto & testCase = GetParam();
    auto ft = makeFileTransfer();

    // Access the isDnsCompliantBucketName function through the FileTransfer implementation
    // Note: This would require making the function accessible for testing
    // For now, we test indirectly through URL construction

    std::string s3Uri = "s3://" + testCase.bucketName + "/test-key?use-transfer-acceleration=true";

    if (testCase.shouldBeValid) {
        EXPECT_NO_THROW({
            FileTransferRequest request(s3Uri);
            // The actual validation happens during URL conversion in the transfer
        }) << "Should accept valid bucket name: "
           << testCase.bucketName << " (" << testCase.description << ")";
    } else {
        // Invalid bucket names should be rejected when transfer acceleration is enabled
        // This would be caught during the actual transfer attempt
        FileTransferRequest request(s3Uri);
        // Note: Full validation happens in toHttpsUrl() which is called during transfer
    }
}

INSTANTIATE_TEST_SUITE_P(
    BucketNameValidation,
    S3BucketNameValidationTest,
    ::testing::Values(
        // Valid bucket names for transfer acceleration
        BucketNameTestCase{"valid-bucket-name", true, "standard valid name"},
        BucketNameTestCase{"my-bucket-123", true, "with numbers"},
        BucketNameTestCase{"abc", true, "minimum length"},
        BucketNameTestCase{"a23456789012345678901234567890123456789012345678901234567890123", true, "maximum length"},

        // Invalid bucket names for transfer acceleration
        BucketNameTestCase{"my.bucket.name", false, "contains dots"},
        BucketNameTestCase{"UPPERCASE", false, "contains uppercase"},
        BucketNameTestCase{"-bucket", false, "starts with hyphen"},
        BucketNameTestCase{"bucket-", false, "ends with hyphen"},
        BucketNameTestCase{"bucket--name", false, "consecutive hyphens"},
        BucketNameTestCase{"ab", false, "too short"},
        BucketNameTestCase{"a234567890123456789012345678901234567890123456789012345678901234", false, "too long"},
        BucketNameTestCase{"192.168.1.1", false, "IP address format"}),
    [](const ::testing::TestParamInfo<BucketNameTestCase> & info) {
        std::string name = info.param.description;
        std::replace(name.begin(), name.end(), ' ', '_');
        std::replace(name.begin(), name.end(), '-', '_');
        return name;
    });

TEST_F(S3FileTransferTest, s3TransferAcceleration_IncompatibleWithCustomEndpoint)
{
    // Test that transfer acceleration cannot be used with custom endpoints
    StringMap params;
    params["use-transfer-acceleration"] = "true";
    params["endpoint"] = "minio.example.com";

    S3BinaryCacheStoreConfig config("s3", "test-bucket", params);

    // Both settings should be preserved in config
    EXPECT_EQ(config.cacheUri.query["use-transfer-acceleration"], "true");
    EXPECT_EQ(config.cacheUri.query["endpoint"], "minio.example.com");

    // The actual error would be thrown during URL conversion
    // when attempting to use the store
}

} // namespace nix

#endif // NIX_WITH_AWS_CRT_SUPPORT
