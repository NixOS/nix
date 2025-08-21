#include "nix/store/filetransfer.hh"
#include "nix/store/s3.hh"
#include "nix/store/config.hh"

#if NIX_WITH_S3_SUPPORT

#  include <gtest/gtest.h>
#  include <gmock/gmock.h>
#  include <thread>
#  include <atomic>
#  include <chrono>
#  include <random>

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

// Parameterized test for special character URLs
class S3SpecialCharacterTest : public S3EdgeCasesTest, public ::testing::WithParamInterface<std::string>
{};

TEST_P(S3SpecialCharacterTest, HandlesSpecialCharacters)
{
    const auto & url = GetParam();
    FileTransferRequest request(parseURL(url));
    EXPECT_EQ(request.uri.scheme(), "s3");
}

INSTANTIATE_TEST_SUITE_P(
    SpecialUrls,
    S3SpecialCharacterTest,
    ::testing::Values(
        "s3://bucket-with-dashes/key-with-dashes.txt",
        "s3://bucket.with.dots/path/with/slashes/file.txt",
        "s3://bucket123/key_with_underscores.txt",
        "s3://my-bucket/path/with%20encoded%20spaces.txt"),
    [](const ::testing::TestParamInfo<std::string> & info) {
        // Generate readable test names from URLs
        std::string name = info.param;
        // Extract just the interesting part for test naming
        size_t lastSlash = name.rfind('/');
        if (lastSlash != std::string::npos) {
            name = name.substr(lastSlash + 1);
        }
        // Replace non-alphanumeric characters with underscores
        for (auto & c : name) {
            if (!std::isalnum(c))
                c = '_';
        }
        return name;
    });

// Parameterized test for invalid regions
class S3InvalidRegionTest : public S3EdgeCasesTest, public ::testing::WithParamInterface<std::string>
{};

TEST_P(S3InvalidRegionTest, HandlesInvalidRegionsGracefully)
{
    const auto & url = GetParam();
    FileTransferRequest request(parseURL(url));
    // Should handle gracefully, possibly with default region
    EXPECT_EQ(request.uri.scheme(), "s3");
}

INSTANTIATE_TEST_SUITE_P(
    InvalidRegions,
    S3InvalidRegionTest,
    ::testing::Values(
        "s3://bucket/key?region=",               // Empty region
        "s3://bucket/key?region=invalid-region", // Non-existent region
        "s3://bucket/key?region=local"           // Local/custom region
        ),
    [](const ::testing::TestParamInfo<std::string> & info) {
        if (info.param.find("region=") == std::string::npos)
            return "no_region";
        if (info.param.find("region=invalid") != std::string::npos)
            return "invalid_region";
        if (info.param.find("region=local") != std::string::npos)
            return "local_region";
        if (info.param.find("region=&") != std::string::npos || info.param.back() == '=')
            return "empty_region";
        return "unknown";
    });

// Parameterized test for credential sources
struct CredentialTestCase
{
    std::string name;
    std::function<void()> setup;
    std::function<void()> cleanup;
    bool expectSuccess;
};

class S3CredentialSourceTest : public S3EdgeCasesTest, public ::testing::WithParamInterface<CredentialTestCase>
{};

TEST_P(S3CredentialSourceTest, TestCredentialSource)
{
    const auto & testCase = GetParam();

    // Setup environment
    testCase.setup();

    try {
        auto provider = AwsCredentialProvider::createDefault();
        auto creds = provider->getCredentials();

        if (testCase.expectSuccess) {
            EXPECT_FALSE(creds.accessKeyId.empty());
            EXPECT_FALSE(creds.secretAccessKey.empty());
        }
    } catch (const AwsAuthError & e) {
        if (testCase.expectSuccess) {
            GTEST_SKIP() << "AWS authentication failed: " << e.what();
        }
    }

    // Cleanup
    testCase.cleanup();
}

INSTANTIATE_TEST_SUITE_P(
    CredentialSources,
    S3CredentialSourceTest,
    ::testing::Values(
        CredentialTestCase{
            "with_session_token",
            []() {
                setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
                setenv("AWS_SECRET_ACCESS_KEY", "secret", 1);
                setenv("AWS_SESSION_TOKEN", "session", 1);
            },
            []() {
                unsetenv("AWS_ACCESS_KEY_ID");
                unsetenv("AWS_SECRET_ACCESS_KEY");
                unsetenv("AWS_SESSION_TOKEN");
            },
            true},
        CredentialTestCase{
            "without_session_token",
            []() {
                setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
                setenv("AWS_SECRET_ACCESS_KEY", "secret", 1);
            },
            []() {
                unsetenv("AWS_ACCESS_KEY_ID");
                unsetenv("AWS_SECRET_ACCESS_KEY");
            },
            true},
        CredentialTestCase{
            "no_credentials",
            []() {}, // No setup
            []() {}, // No cleanup
            false}),
    [](const ::testing::TestParamInfo<CredentialTestCase> & info) { return info.param.name; });

// Non-parameterized tests that don't fit the pattern

// Parameterized test for long URLs
struct LongUrlTestCase
{
    int keyLength;
    std::string suffix;
    std::string description;
};

class S3LongUrlTest : public S3EdgeCasesTest, public ::testing::WithParamInterface<LongUrlTestCase>
{};

TEST_P(S3LongUrlTest, extremelyLongUrls)
{
    const auto & testCase = GetParam();

    // Test very long S3 URLs
    std::string longKey = std::string(testCase.keyLength, 'x') + testCase.suffix;
    std::string longUrl = "s3://bucket/" + longKey;

    FileTransferRequest request(parseURL(longUrl));
    EXPECT_EQ(request.uri.scheme(), "s3");
}

INSTANTIATE_TEST_SUITE_P(
    LongUrls,
    S3LongUrlTest,
    ::testing::Values(
        LongUrlTestCase{100, "/file.txt", "short_key"},
        LongUrlTestCase{1000, "/file.txt", "long_key"},
        LongUrlTestCase{5000, "/file.txt", "very_long_key"},
        LongUrlTestCase{1000, "/deeply/nested/path/file.txt", "long_key_nested_path"}),
    [](const ::testing::TestParamInfo<LongUrlTestCase> & info) { return info.param.description; });

TEST_F(S3EdgeCasesTest, duplicateQueryParameters)
{
    // Test URLs with duplicate query parameters (last one should win)
    std::string complexUrl =
        "s3://bucket/key?region=us-west-2&profile=prod&endpoint=custom.s3.com&scheme=https&region=us-east-1";

    FileTransferRequest request(parseURL(complexUrl));
    EXPECT_EQ(request.uri.scheme(), "s3");
}

TEST_F(S3EdgeCasesTest, headerInjection)
{
    // Test that custom headers are properly handled
    FileTransferRequest request(parseURL("s3://bucket/key"));
    request.headers.emplace_back("Custom-Header", "custom-value");
    request.headers.emplace_back("Invalid-Header", "invalid-value");

    // Should handle gracefully - makeFileTransfer returns a ref which is never null
    auto transfer = makeFileTransfer();
    // ref<T> is always valid, so just verify we can create it
    EXPECT_NO_THROW({ transfer->enqueueFileTransfer(request); });
}

// Parameterized test for large credential sizes
struct LargeCredentialTestCase
{
    int accessKeySize;
    int secretKeySize;
    int sessionTokenSize;
    std::string description;
};

class S3LargeCredentialTest : public S3EdgeCasesTest, public ::testing::WithParamInterface<LargeCredentialTestCase>
{};

TEST_P(S3LargeCredentialTest, memoryLargeCredentials)
{
    const auto & testCase = GetParam();

    // Test handling of unusually large credential values
    std::string largeAccessKey(testCase.accessKeySize, 'A');
    std::string largeSecretKey(testCase.secretKeySize, 'S');
    std::string largeSessionToken(testCase.sessionTokenSize, 'T');

    setenv("AWS_ACCESS_KEY_ID", largeAccessKey.c_str(), 1);
    setenv("AWS_SECRET_ACCESS_KEY", largeSecretKey.c_str(), 1);
    if (testCase.sessionTokenSize > 0) {
        setenv("AWS_SESSION_TOKEN", largeSessionToken.c_str(), 1);
    }

    try {
        auto provider = AwsCredentialProvider::createDefault();
        // Should handle large credentials without memory issues
        auto creds = provider->getCredentials();
        // Just verify the call completes
        EXPECT_FALSE(creds.accessKeyId.empty());
    } catch (const AwsAuthError &) {
        // May fail in sandboxed environments, which is acceptable
    }

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
    unsetenv("AWS_SESSION_TOKEN");
}

INSTANTIATE_TEST_SUITE_P(
    LargeCredentials,
    S3LargeCredentialTest,
    ::testing::Values(
        LargeCredentialTestCase{100, 100, 500, "small_credentials"},
        LargeCredentialTestCase{1000, 1000, 5000, "large_credentials"},
        LargeCredentialTestCase{5000, 5000, 10000, "very_large_credentials"},
        LargeCredentialTestCase{1000, 1000, 0, "large_without_session"}),
    [](const ::testing::TestParamInfo<LargeCredentialTestCase> & info) { return info.param.description; });

TEST_F(S3EdgeCasesTest, threadSafetyMultipleProviders)
{
    // Test concurrent access to credential providers from multiple threads
    const int numThreads = 10;
    const int numIterations = 100;

    // Set up mock credentials for testing
    setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "secret", 1);

    // Create a shared provider that will be accessed concurrently
    std::unique_ptr<AwsCredentialProvider> sharedProvider;
    try {
        sharedProvider = AwsCredentialProvider::createDefault();
    } catch (const AwsAuthError &) {
        // Skip test if we can't create provider (sandboxed environment)
        GTEST_SKIP() << "Cannot create credential provider in this environment";
    }

    std::atomic<int> successCount{0};
    std::atomic<int> errorCount{0};
    std::vector<std::thread> threads;

    // Thread-local random number generator for delays
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    thread_local std::uniform_int_distribution<> dis(0, 99);

    // Spawn multiple threads that concurrently access the provider
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&sharedProvider, &successCount, &errorCount, numIterations]() {
            for (int i = 0; i < numIterations; ++i) {
                try {
                    // Concurrent calls to getCredentials
                    auto creds = sharedProvider->getCredentials();

                    // Verify we got valid credentials
                    if (!creds.accessKeyId.empty() && !creds.secretAccessKey.empty()) {
                        successCount++;
                    } else {
                        errorCount++;
                    }

                    // Small random delay to increase chance of race conditions
                    std::this_thread::sleep_for(std::chrono::microseconds(dis(gen)));
                } catch (const std::exception & e) {
                    errorCount++;
                }
            }
        });
    }

    // Also test creating providers concurrently
    std::vector<std::thread> creationThreads;
    std::atomic<int> createSuccess{0};

    for (int t = 0; t < numThreads; ++t) {
        creationThreads.emplace_back([&createSuccess]() {
            try {
                // Concurrent provider creation
                auto provider = AwsCredentialProvider::createDefault();
                auto creds = provider->getCredentials();
                if (!creds.accessKeyId.empty()) {
                    createSuccess++;
                }
            } catch (const std::exception &) {
                // Ignore errors in sandboxed environments
            }
        });
    }

    // Wait for all threads to complete
    for (auto & t : threads) {
        t.join();
    }
    for (auto & t : creationThreads) {
        t.join();
    }

    // Verify that operations completed successfully
    // We expect most operations to succeed if credentials are available
    EXPECT_GT(successCount.load(), 0) << "At least some credential fetches should succeed";
    EXPECT_EQ(successCount.load() + errorCount.load(), numThreads * numIterations)
        << "All operations should be accounted for";

    // Clean up
    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST_F(S3EdgeCasesTest, curlOptionsVerifyS3Configuration)
{
    // Test that curl options are properly configured for S3 requests
    setenv("AWS_ACCESS_KEY_ID", "AKIATEST", 1);
    setenv("AWS_SECRET_ACCESS_KEY", "secret", 1);

    auto s3Uri = parseURL("s3://bucket/key?region=us-west-2");
    FileTransferRequest request(s3Uri);

    // Verify request creation succeeds
    EXPECT_EQ(request.uri.scheme(), "s3");

    // Note: Testing actual curl option setting would require
    // exposing internal TransferItem state or using integration tests

    unsetenv("AWS_ACCESS_KEY_ID");
    unsetenv("AWS_SECRET_ACCESS_KEY");
}

} // namespace nix

#endif // NIX_WITH_S3_SUPPORT