#include <gtest/gtest.h>

#include "nix/util/file-system.hh"
#include "unix/file-system.hh"

#include <unistd.h>

extern "C" int
sandbox_init_with_parameters(const char * profile, uint64_t flags, const char * const parameters[], char ** errorbuf);

namespace nix {

class RootDeletePath : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_EQ(geteuid(), 0) << "Run Root* tests as root";
    }
};

TEST_F(RootDeletePath, restrictiveDirectoryPermissions)
{
    testDeletePathWithRestrictivePermissions();
}

TEST_F(RootDeletePath, modeChangesDenied)
{
    auto tmpDir = createTempDir();
    AutoDelete cleanup(tmpDir);
    auto directory = tmpDir / "directory";
    createDirs(directory);
    writeFile(directory / "file", "contents");
    chmod(directory, 0555);

    /* Restrict only the child so the parent can clean up after a failed test.
       This models App Management allowing deletion but denying mode changes. */
    ASSERT_EXIT(
        {
            char * error = nullptr;
            if (sandbox_init_with_parameters("(version 1)(allow default)(deny file-write-mode)", 0, nullptr, &error)) {
                dprintf(STDERR_FILENO, "sandbox initialization failed: %s\n", error);
                _exit(2);
            }

            try {
                deletePath(directory);
            } catch (const std::exception & e) {
                dprintf(STDERR_FILENO, "%s\n", e.what());
                _exit(3);
            }
            _exit(0);
        },
        ::testing::ExitedWithCode(0),
        "");

    EXPECT_FALSE(pathExists(directory));
}

} // namespace nix
