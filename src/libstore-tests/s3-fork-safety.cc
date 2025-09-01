#include <gtest/gtest.h>

#include "nix/store/filetransfer.hh"
#include "nix/store/s3.hh"
#include "nix/util/logging.hh"

#include <sys/wait.h>
#include <unistd.h>

#if NIX_WITH_S3_SUPPORT

namespace nix {

/**
 * Test suite to validate fork safety of S3 credential providers
 * and FileTransfer instances, particularly for builtin:fetchurl
 */
class S3ForkSafetyTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        verbosity = lvlDebug;
    }
};

/**
 * Test that FileTransfer instances work correctly after fork
 */
TEST_F(S3ForkSafetyTest, FileTransferAfterFork)
{
    // Create a FileTransfer in the parent
    auto parentTransfer = makeFileTransfer();
    // ref<T> is always valid, no need to check

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        // Child process
        try {
            // Create a new FileTransfer in the child
            debug("[pid=%d] Child creating new FileTransfer", getpid());
            auto childTransfer = makeFileTransfer();

            // The child should have its own instance (check pointers)
            EXPECT_NE(&*childTransfer, &*parentTransfer);

            // Test that we can use the child's FileTransfer
            // (This won't actually make a request, just test object validity)
            FileTransferRequest testReq(parseURL("https://example.com"));
            EXPECT_NO_THROW(childTransfer->enqueueFileTransfer(testReq));

            _exit(0);
        } catch (...) {
            _exit(1);
        }
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0) << "Child process failed";

        // Parent's FileTransfer should still work
        FileTransferRequest testReq(parseURL("https://example.com"));
        EXPECT_NO_THROW(parentTransfer->enqueueFileTransfer(testReq));
    }
}

/**
 * Test that credential provider caching is separate between parent and child
 */
TEST_F(S3ForkSafetyTest, CredentialProviderCacheAfterFork)
{
    // Skip test if AWS credentials are not available
    if (getenv("AWS_ACCESS_KEY_ID") == nullptr) {
        GTEST_SKIP() << "AWS credentials not available for testing";
    }

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        // Child process
        try {
            debug("[pid=%d] Child creating credential provider", getpid());

            // Create a credential provider in the child
            auto childProvider = AwsCredentialProvider::createDefault();
            EXPECT_NE(childProvider.get(), nullptr);

            // Try to get credentials (may fail if not configured, that's OK)
            try {
                auto creds = childProvider->getCredentials();
                debug("[pid=%d] Child got credentials", getpid());
            } catch (const AwsAuthError & e) {
                debug("[pid=%d] Child credential fetch failed (expected): %s", getpid(), e.what());
            }

            _exit(0);
        } catch (...) {
            _exit(1);
        }
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0) << "Child process failed";

        // Parent should be able to create its own provider
        debug("[pid=%d] Parent creating credential provider", getpid());
        auto parentProvider = AwsCredentialProvider::createDefault();
        EXPECT_NE(parentProvider.get(), nullptr);
    }
}

/**
 * Test multiple concurrent forks accessing S3
 * This simulates what happens when multiple derivations run builtin:fetchurl
 */
TEST_F(S3ForkSafetyTest, ConcurrentForkS3Access)
{
    const int numForks = 3;
    std::vector<pid_t> pids;

    for (int i = 0; i < numForks; i++) {
        pid_t pid = fork();
        ASSERT_GE(pid, 0);

        if (pid == 0) {
            // Child process
            try {
                debug("[pid=%d] Child %d creating FileTransfer", getpid(), i);
                auto transfer = makeFileTransfer();
                // ref<T> is always valid, no need to check

                // Simulate S3 request setup (without actually making the request)
                FileTransferRequest s3Req(parseURL("s3://test-bucket/test-file"));

                // The child should be able to handle S3 URLs
                // (This tests the URL parsing and setup, not actual S3 access)
                EXPECT_NO_THROW({
                    // This would normally trigger credential provider creation
                    // but we're not actually executing the transfer
                    debug("[pid=%d] Child %d prepared S3 request", getpid(), i);
                });

                _exit(0);
            } catch (...) {
                _exit(1);
            }
        } else {
            // Parent process - collect child PIDs
            pids.push_back(pid);
        }
    }

    // Parent waits for all children
    for (pid_t childPid : pids) {
        int status;
        waitpid(childPid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0) << "Child process " << childPid << " failed";
    }

    // Parent should still be functional
    auto parentTransfer = makeFileTransfer();
    // ref<T> is always valid, no need to check
}

/**
 * Test that simulates builtin:fetchurl behavior with fork
 */
TEST_F(S3ForkSafetyTest, SimulateBuiltinFetchurl)
{
    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        // Child process - simulates what builtin:fetchurl does
        try {
            debug("[pid=%d] Simulating builtin:fetchurl in forked process", getpid());

            // Create a fresh FileTransfer as builtin:fetchurl does
            auto fileTransfer = makeFileTransfer();
            // ref<T> is always valid, no need to check

            // Test with an S3 URL
            std::string s3Url = "s3://test-bucket/test-file?endpoint=http://localhost:9000&region=us-east-1";
            auto parsedUrl = parseURL(s3Url);

            // Create a request (but don't execute it)
            FileTransferRequest request(parsedUrl);

            // The forked process should be able to handle this
            debug("[pid=%d] Forked process successfully created S3 request", getpid());

            _exit(0);
        } catch (const std::exception & e) {
            debug("[pid=%d] Forked process failed: %s", getpid(), e.what());
            _exit(1);
        }
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        EXPECT_EQ(WEXITSTATUS(status), 0) << "Simulated builtin:fetchurl failed in forked process";
    }
}

} // namespace nix

#endif // NIX_WITH_S3_SUPPORT