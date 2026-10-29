#include <gtest/gtest.h>

#include "nix/util/file-system.hh"
#include "unix/file-system.hh"

#include <linux/capability.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace nix {

class RootDeletePath : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_EQ(geteuid(), 0) << "Run Root* tests as root";
    }
};

static void dropEffectiveCapability(int capability)
{
    __user_cap_header_struct header{.version = _LINUX_CAPABILITY_VERSION_3, .pid = 0};
    __user_cap_data_struct caps[_LINUX_CAPABILITY_U32S_3]{};
    if (::syscall(SYS_capget, &header, caps) == -1)
        throw SysError("getting capabilities");

    caps[CAP_TO_INDEX(capability)].effective &= ~CAP_TO_MASK(capability);
    if (::syscall(SYS_capset, &header, caps) == -1)
        throw SysError("dropping effective capability");
}

TEST_F(RootDeletePath, withoutDacOverride)
{
    /* Keep the capability permitted: only the effective set authorizes access.
       Restrict a child process so subsequent tests retain their capabilities. */
    ASSERT_EXIT(
        {
            dropEffectiveCapability(CAP_DAC_OVERRIDE);
            testDeletePathWithRestrictivePermissions();
            _exit(::testing::Test::HasFailure() ? 1 : 0);
        },
        ::testing::ExitedWithCode(0),
        "");
}

TEST_F(RootDeletePath, modeChangesDenied)
{
    auto tmpDir = createTempDir();
    AutoDelete cleanup(tmpDir);
    auto directory = tmpDir / "directory";
    createDirs(directory);
    writeFile(directory / "file", "contents");
    chmod(directory, 0000);
    ASSERT_EQ(::chown(directory.c_str(), 1, 1), 0);

    /* DAC_OVERRIDE allows deletion, but without FOWNER we cannot chmod a
       directory owned by another user. The parent can still clean up on failure. */
    ASSERT_EXIT(
        {
            dropEffectiveCapability(CAP_FOWNER);
            if (::chmod(directory.c_str(), 0700) != -1 || errno != EPERM)
                _exit(2);

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
