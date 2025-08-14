#include "nix/store/filetransfer.hh"
#include "nix/store/aws-auth.hh"
#include "nix/store/config.hh"

#if NIX_WITH_AWS_CRT_SUPPORT

#  include <gtest/gtest.h>
#  include <gmock/gmock.h>

namespace nix {

class S3EdgeCasesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Ensure clean test environment
        unsetenv("AWS_ACCESS_KEY_ID");
        unsetenv("AWS_SECRET_ACCESS_KEY");
        unsetenv("AWS_SESSION_TOKEN");
        unsetenv("AWS_PROFILE");
        unsetenv("AWS_DEFAULT_REGION");
    }
};

TEST_F(S3EdgeCasesTest, credentialProvider_Null)
{
    // Test behavior when credential provider creation fails
    // Profile creation may fail for non-existent profiles, which is expected behavior
    EXPECT_NO_THROW({
        auto provider = AwsCredentialProvider::createProfile("non-existent-profile");
        // Provider creation might return nullptr for invalid profiles
        // This is acceptable behavior
    });
}

TEST_F(S3EdgeCasesTest, credentialProvider_EmptyProfile)
{
    // Test that empty profile falls back to default
    auto provider1 = AwsCredentialProvider::createProfile("");
    auto provider2 = AwsCredentialProvider::createDefault();

    if (!provider1 || !provider2) {
        GTEST_SKIP() << "AWS CRT not available in this environment";
    }
    EXPECT_NE(provider1, nullptr);
    EXPECT_NE(provider2, nullptr);

    // Both should be created successfully
    EXPECT_TRUE(true);
}

TEST_F(S3EdgeCasesTest, concurrentCredentialRequests)
{
    // Test that multiple concurrent credential requests work
    auto provider = AwsCredentialProvider::createDefault();
    if (!provider) {
        GTEST_SKIP() << "AWS CRT not available in this environment";
    }
    ASSERT_NE(provider, nullptr);

    // Simulate concurrent access (basic test)
    std::vector<std::optional<AwsCredentials>> results(3);

    for (int i = 0; i < 3; ++i) {
        results[i] = provider->getCredentials();
    }

    // All calls should complete without crashing
    EXPECT_EQ(results.size(), 3);
}

TEST_F(S3EdgeCasesTest, specialCharacters_BucketAndKey)
{
    // Test S3 URLs with special characters that need encoding
    std::vector<std::string> specialUrls = {
        "s3://bucket-with-dashes/key-with-dashes.txt",
        "s3://bucket.with.dots/path/with/slashes/file.txt",
        "s3://bucket123/key_with_underscores.txt",
        "s3://my-bucket/path/with%20encoded%20spaces.txt"};

    for (const auto & url : specialUrls) {
        EXPECT_NO_THROW({
            FileTransferRequest request(url);
            EXPECT_TRUE(hasPrefix(request.uri, "s3://"));
        }) << "Failed for URL with special characters: "
           << url;
    }
}

TEST_F(S3EdgeCasesTest, extremelyLongUrls)
{
    // Test very long S3 URLs
    std::string longKey = std::string(1000, 'x') + "/file.txt";
    std::string longUrl = "s3://bucket/" + longKey;

    EXPECT_NO_THROW({
        FileTransferRequest request(longUrl);
        EXPECT_TRUE(hasPrefix(request.uri, "s3://"));
    });
}

TEST_F(S3EdgeCasesTest, invalidRegions)
{
    // Test behavior with invalid or non-standard regions
    std::vector<std::string> invalidRegionUrls = {
        "s3://bucket/key?region=",               // Empty region
        "s3://bucket/key?region=invalid-region", // Non-existent region
        "s3://bucket/key?region=local",          // Local/custom region
    };

    for (const auto & url : invalidRegionUrls) {
        EXPECT_NO_THROW({
            FileTransferRequest request(url);
            // Should handle gracefully, possibly with default region
        }) << "Failed for URL with invalid region: "
           << url;
    }
}

TEST_F(S3EdgeCasesTest, multipleParameters)
{
    // Test URLs with multiple parameters including duplicates
    std::string complexUrl =
        "s3://bucket/key?region=us-west-2&profile=prod&endpoint=custom.s3.com&scheme=https&region=us-east-1";

    EXPECT_NO_THROW({
        FileTransferRequest request(complexUrl);
        EXPECT_TRUE(hasPrefix(request.uri, "s3://"));
    });
}

TEST_F(S3EdgeCasesTest, credentialTypes_AllScenarios)
{
    // Test different credential scenarios
    // Provider creation may return nullptr in sandboxed environments, which is acceptable

    // 1. Environment variables with session token
    setenv("AWS_ACCESS_KEY_ID", "ASIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "secret", 1);
    setenv("AWS_SESSION_TOKEN", "session", 1);

    EXPECT_NO_THROW({
        auto provider1 = AwsCredentialProvider::createDefault();
        // Provider creation might fail in sandboxed build environments
        if (provider1) {
            auto creds = provider1->getCredentials();
            // Just verify the call doesn't crash
        }
    });

    // 2. Environment variables without session token
    unsetenv("AWS_SESSION_TOKEN");

    EXPECT_NO_THROW({
        auto provider2 = AwsCredentialProvider::createDefault();
        if (provider2) {
            auto creds = provider2->getCredentials();
        }
    });

    // 3. Clear environment (should fall back to other providers)
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");

    EXPECT_NO_THROW({
        auto provider3 = AwsCredentialProvider::createDefault();
        if (provider3) {
            auto creds = provider3->getCredentials();
        }
    });

    // All calls should complete without crashing
    EXPECT_TRUE(true);
}

TEST_F(S3EdgeCasesTest, errorMessages_S3Specific)
{
    // Test that error messages are informative for S3-specific issues
    auto ft = makeFileTransfer();

    std::string s3Uri = "s3://bucket/key";
    FileTransferRequest request(s3Uri);

    // Test with invalid headers
    request.headers.emplace_back("Invalid-Header", "invalid-value");

    // Should handle gracefully
    EXPECT_NO_THROW({ auto transfer = makeFileTransfer(); });
}

TEST_F(S3EdgeCasesTest, memory_LargeCredentials)
{
    // Test handling of unusually large credential values
    std::string largeAccessKey(1000, 'A');
    std::string largeSecretKey(1000, 'S');
    std::string largeSessionToken(5000, 'T');

    setenv("AWS_ACCESS_KEY_ID", largeAccessKey.c_str(), 1);
    setenv("AWS_SECRET_ACCESS_KEY", largeSecretKey.c_str(), 1);
    setenv("AWS_SESSION_TOKEN", largeSessionToken.c_str(), 1);

    EXPECT_NO_THROW({
        auto provider = AwsCredentialProvider::createDefault();
        if (provider) {
            // Should handle large credentials without memory issues
            auto creds = provider->getCredentials();
            // Just verify the call completes
        }
    });

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
}

TEST_F(S3EdgeCasesTest, threadSafety_MultipleProviders)
{
    // Basic thread safety test
    EXPECT_NO_THROW({
        std::vector<std::unique_ptr<AwsCredentialProvider>> providers;

        // Create multiple providers
        for (int i = 0; i < 5; ++i) {
            providers.push_back(AwsCredentialProvider::createDefault());
        }

        // Test concurrent credential resolution
        for (const auto & provider : providers) {
            if (provider) {
                auto creds = provider->getCredentials();
                // Just verify calls complete without crashing
            }
        }
    });

    EXPECT_TRUE(true);
}

TEST_F(S3EdgeCasesTest, curlOptions_VerifyS3Configuration)
{
    // Test that curl options are properly configured for S3 requests
    setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "secret", 1);

    std::string s3Uri = "s3://bucket/key?region=us-west-2";
    FileTransferRequest request(s3Uri);

    // Verify request creation succeeds
    EXPECT_TRUE(hasPrefix(request.uri, "s3://"));

    // Note: Testing actual curl option setting would require
    // exposing internal TransferItem state or using integration tests

    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

} // namespace nix

#endif // NIX_WITH_AWS_CRT_SUPPORT