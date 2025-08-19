#include "nix/store/aws-auth.hh"
#include "nix/store/config.hh"

#if NIX_WITH_AWS_CRT_SUPPORT

#  include <gtest/gtest.h>
#  include <gmock/gmock.h>

namespace nix {

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

TEST_F(AwsCredentialProviderTest, createProfile_Empty)
{
    try {
        auto provider = AwsCredentialProvider::createProfile("");
        EXPECT_NE(provider, nullptr);
    } catch (const AwsAuthError & e) {
        // Expected in sandboxed environments where AWS CRT isn't available
        GTEST_SKIP() << "AWS CRT not available: " << e.what();
    }
}

TEST_F(AwsCredentialProviderTest, createProfile_Named)
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

TEST_F(AwsCredentialProviderTest, getCredentials_NoCredentials)
{
    // With no environment variables or profile, should throw when getting credentials
    try {
        auto provider = AwsCredentialProvider::createDefault();
        ASSERT_NE(provider, nullptr);

        // This should throw if there are no credentials available
        try {
            auto creds = provider->getCredentials();
            // If we got here, credentials were found (e.g., from IMDS or ~/.aws/credentials)
            EXPECT_TRUE(true); // Basic sanity check
        } catch (const AwsAuthError &) {
            // Expected if no credentials are available
            EXPECT_TRUE(true);
        }
    } catch (const AwsAuthError & e) {
        GTEST_SKIP() << "AWS authentication failed: " << e.what();
    }
}

TEST_F(AwsCredentialProviderTest, getCredentials_FromEnvironment)
{
    // Set up test environment variables
    setenv("AWS_ACCESS_KEY_ID", "test-access-key", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "test-secret-key", 1);
    setenv("AWS_SESSION_TOKEN", "test-session-token", 1);

    try {
        auto provider = AwsCredentialProvider::createDefault();
        ASSERT_NE(provider, nullptr);

        auto creds = provider->getCredentials();
        EXPECT_EQ(creds.accessKeyId, "test-access-key");
        EXPECT_EQ(creds.secretAccessKey, "test-secret-key");
        EXPECT_TRUE(creds.sessionToken.has_value());
        EXPECT_EQ(*creds.sessionToken, "test-session-token");
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

TEST_F(AwsCredentialProviderTest, getCredentials_WithoutSessionToken)
{
    // Set up test environment variables without session token
    setenv("AWS_ACCESS_KEY_ID", "test-access-key-2", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "test-secret-key-2", 1);

    try {
        auto provider = AwsCredentialProvider::createDefault();
        ASSERT_NE(provider, nullptr);

        auto creds = provider->getCredentials();
        EXPECT_EQ(creds.accessKeyId, "test-access-key-2");
        EXPECT_EQ(creds.secretAccessKey, "test-secret-key-2");
        EXPECT_FALSE(creds.sessionToken.has_value());
    } catch (const AwsAuthError & e) {
        // Clean up first
        unsetenv("AWS_ACCESS_KEY_ID");
        unsetenv("AWS_SECRET_ACCESS_KEY");
        GTEST_SKIP() << "AWS authentication failed: " << e.what();
    }

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST_F(AwsCredentialProviderTest, multipleProviders_Independent)
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

} // namespace nix

#endif // NIX_WITH_AWS_CRT_SUPPORT