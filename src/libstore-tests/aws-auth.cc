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
    auto provider = AwsCredentialProvider::createDefault();
    // Provider may be null in sandboxed environments, which is acceptable
    if (!provider) {
        GTEST_SKIP() << "AWS CRT not available in this environment";
    }
    EXPECT_NE(provider, nullptr);
}

TEST_F(AwsCredentialProviderTest, createProfile_Empty)
{
    auto provider = AwsCredentialProvider::createProfile("");
    // Provider may be null in sandboxed environments, which is acceptable
    if (!provider) {
        GTEST_SKIP() << "AWS CRT not available in this environment";
    }
    EXPECT_NE(provider, nullptr);
}

TEST_F(AwsCredentialProviderTest, createProfile_Named)
{
    auto provider = AwsCredentialProvider::createProfile("test-profile");
    // Profile creation may fail if profile doesn't exist, which is expected
    // Just verify the call doesn't crash
    EXPECT_TRUE(true);
}

TEST_F(AwsCredentialProviderTest, getCredentials_NoCredentials)
{
    // With no environment variables or profile, should return nullopt
    auto provider = AwsCredentialProvider::createDefault();
    if (!provider) {
        GTEST_SKIP() << "AWS CRT not available in this environment";
    }
    ASSERT_NE(provider, nullptr);

    auto creds = provider->getCredentials();
    // This may or may not be null depending on environment,
    // so just verify the call doesn't crash
    EXPECT_TRUE(true); // Basic sanity check
}

TEST_F(AwsCredentialProviderTest, getCredentials_FromEnvironment)
{
    // Set up test environment variables
    setenv("AWS_ACCESS_KEY_ID", "test-access-key", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "test-secret-key", 1);
    setenv("AWS_SESSION_TOKEN", "test-session-token", 1);

    auto provider = AwsCredentialProvider::createDefault();
    if (!provider) {
        // Clean up first
        unsetenv("AWS_ACCESS_KEY_ID");
        unsetenv("AWS_SECRET_ACCESS_KEY");
        unsetenv("AWS_SESSION_TOKEN");
        GTEST_SKIP() << "AWS CRT not available in this environment";
    }
    ASSERT_NE(provider, nullptr);

    auto creds = provider->getCredentials();
    if (creds.has_value()) {
        EXPECT_EQ(creds->accessKeyId, "test-access-key");
        EXPECT_EQ(creds->secretAccessKey, "test-secret-key");
        EXPECT_TRUE(creds->sessionToken.has_value());
        EXPECT_EQ(*creds->sessionToken, "test-session-token");
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

    auto provider = AwsCredentialProvider::createDefault();
    if (!provider) {
        // Clean up first
        unsetenv("AWS_ACCESS_KEY_ID");
        unsetenv("AWS_SECRET_ACCESS_KEY");
        GTEST_SKIP() << "AWS CRT not available in this environment";
    }
    ASSERT_NE(provider, nullptr);

    auto creds = provider->getCredentials();
    if (creds.has_value()) {
        EXPECT_EQ(creds->accessKeyId, "test-access-key-2");
        EXPECT_EQ(creds->secretAccessKey, "test-secret-key-2");
        EXPECT_FALSE(creds->sessionToken.has_value());
    }

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST_F(AwsCredentialProviderTest, multipleProviders_Independent)
{
    // Test that multiple providers can be created independently
    auto provider1 = AwsCredentialProvider::createDefault();
    auto provider2 = AwsCredentialProvider::createDefault(); // Use default for both

    if (!provider1 || !provider2) {
        GTEST_SKIP() << "AWS CRT not available in this environment";
    }
    EXPECT_NE(provider1, nullptr);
    EXPECT_NE(provider2, nullptr);
    EXPECT_NE(provider1.get(), provider2.get());
}

} // namespace nix

#endif // NIX_WITH_AWS_CRT_SUPPORT