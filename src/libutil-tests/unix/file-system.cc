#include <gtest/gtest.h>

#include "nix/util/file-system.hh"
#include "unix/file-system.hh"

namespace nix {

void testDeletePathWithRestrictivePermissions()
{
    auto tmpDir = createTempDir();
    AutoDelete cleanup(tmpDir);

    for (mode_t mode : {0000, 0100, 0400, 0500, 0555}) {
        auto directory = tmpDir / "directory";
        auto child = directory / "child";
        createDirs(child);
        writeFile(child / "file", "contents");
        chmod(child, mode);
        chmod(directory, mode);

        ASSERT_NO_THROW(deletePath(directory));
        EXPECT_FALSE(pathExists(directory));
    }
}

TEST(deletePath, restrictiveDirectoryPermissions)
{
    testDeletePathWithRestrictivePermissions();
}

} // namespace nix
