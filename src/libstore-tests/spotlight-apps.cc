#include <gtest/gtest.h>

#include "../libstore/darwin/spotlight-apps-private.hh"
#include "nix/util/file-system.hh"

#include <optional>
#include <string>

namespace nix::darwin {

namespace fs = std::filesystem;

namespace {

void writeInfoPlist(const fs::path & path, const std::string & bundleName, const std::string & iconKey = "")
{
    std::string contents =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>\n"
        "  <key>CFBundleIdentifier</key><string>org.nix.test."
        + bundleName
        + "</string>\n"
          "  <key>CFBundleName</key><string>"
        + bundleName + "</string>\n";
    if (!iconKey.empty())
        contents += "  <key>CFBundleIconFile</key><string>" + iconKey + "</string>\n";
    contents += "</dict></plist>\n";
    writeFile(path, contents);
}

class SpotlightAppsTest : public ::testing::Test
{
protected:
    std::optional<AutoDelete> tmpDirGuard;
    fs::path tmpRoot;
    fs::path profile;
    fs::path appsDir;
    fs::path destRoot;

    void SetUp() override
    {
        tmpRoot = createTempDir("", "nix-spotlight-test");
        tmpDirGuard.emplace(tmpRoot, /*recursive=*/true);
        profile = tmpRoot / "profile";
        appsDir = profile / "Applications";
        destRoot = tmpRoot / "dest";
        createDirs(appsDir);
        createDirs(destRoot);
    }

    fs::path makeBundle(
        const std::string & name,
        bool withInfoPlist = true,
        const std::string & iconKey = "",
        const std::string & extraResource = "")
    {
        fs::path app = appsDir / (name + ".app");
        fs::path contents = app / "Contents";
        fs::path macos = contents / "MacOS";
        fs::path resources = contents / "Resources";
        createDirs(macos);
        createDirs(resources);

        writeFile(macos / name, "#!/bin/sh\n");

        if (withInfoPlist)
            writeInfoPlist(contents / "Info.plist", name, iconKey);

        if (!iconKey.empty()) {
            std::string iconName = iconKey;
            if (fs::path(iconName).extension().empty())
                iconName += ".icns";
            writeFile(resources / iconName, "icns-stub");
        }

        if (!extraResource.empty())
            writeFile(resources / extraResource, "extra");

        return app;
    }

    void sync()
    {
        detail::syncProfileAppBundlesAt(profile, destRoot, /*notifySystem=*/false);
    }
};

TEST_F(SpotlightAppsTest, materializesSingleBundleWithIcon)
{
    auto srcApp = makeBundle("Foo", true, "AppIcon");

    sync();

    fs::path destApp = destRoot / "Foo.app";
    EXPECT_TRUE(fs::is_directory(destApp));

    fs::path destInfoPlist = destApp / "Contents" / "Info.plist";
    EXPECT_TRUE(fs::is_regular_file(destInfoPlist));
    EXPECT_FALSE(fs::is_symlink(destInfoPlist));

    fs::path destIcon = destApp / "Contents" / "Resources" / "AppIcon.icns";
    EXPECT_TRUE(fs::is_regular_file(destIcon));
    EXPECT_FALSE(fs::is_symlink(destIcon));

    fs::path destMacOS = destApp / "Contents" / "MacOS";
    EXPECT_TRUE(fs::is_symlink(destMacOS));
    EXPECT_EQ(fs::read_symlink(destMacOS), srcApp / "Contents" / "MacOS");
}

TEST_F(SpotlightAppsTest, symlinksOtherResourceEntries)
{
    auto srcApp = makeBundle("Foo", true, "AppIcon", "extra-data.bin");

    sync();

    fs::path destExtra = destRoot / "Foo.app" / "Contents" / "Resources" / "extra-data.bin";
    EXPECT_TRUE(fs::is_symlink(destExtra));
    EXPECT_EQ(fs::read_symlink(destExtra), srcApp / "Contents" / "Resources" / "extra-data.bin");
}

TEST_F(SpotlightAppsTest, preservesBundleSymlinks)
{
    auto srcApp = makeBundle("Foo", true, "AppIcon", "extra-data.bin");
    replaceSymlink("extra-data.bin", appsDir / "Foo.app" / "Contents" / "Resources" / "extra-link");

    sync();

    fs::path destLink = destRoot / "Foo.app" / "Contents" / "Resources" / "extra-link";
    EXPECT_TRUE(fs::is_symlink(destLink));
    EXPECT_EQ(fs::read_symlink(destLink), srcApp / "Contents" / "Resources" / "extra-link");
}

TEST_F(SpotlightAppsTest, copiesPkgInfo)
{
    auto srcApp = makeBundle("Foo", true, "AppIcon");
    writeFile(srcApp / "Contents" / "PkgInfo", "APPL????");

    sync();

    fs::path destPkgInfo = destRoot / "Foo.app" / "Contents" / "PkgInfo";
    EXPECT_TRUE(fs::is_regular_file(destPkgInfo));
    EXPECT_FALSE(fs::is_symlink(destPkgInfo));
}

TEST_F(SpotlightAppsTest, skipsBundlesWithoutInfoPlist)
{
    makeBundle("Good", true, "AppIcon");
    makeBundle("Broken", /*withInfoPlist=*/false);

    sync();

    EXPECT_TRUE(fs::is_directory(destRoot / "Good.app"));
    EXPECT_FALSE(fs::exists(destRoot / "Broken.app"));
}

TEST_F(SpotlightAppsTest, removesDestWhenProfileHasNoApps)
{
    createDirs(destRoot / "Stale.app" / "Contents");
    writeFile(destRoot / "Stale.app" / "Contents" / "Info.plist", "stale");
    deletePath(appsDir);

    sync();

    EXPECT_FALSE(fs::exists(destRoot));
}

TEST_F(SpotlightAppsTest, wipeAndRebuildClearsStaleEntries)
{
    makeBundle("Foo", true, "AppIcon");
    sync();
    EXPECT_TRUE(fs::is_directory(destRoot / "Foo.app"));

    writeFile(destRoot / "garbage.txt", "trash");
    createDirs(destRoot / "Stale.app" / "Contents");
    writeFile(destRoot / "Stale.app" / "Contents" / "Info.plist", "stale");

    deletePath(appsDir / "Foo.app");
    makeBundle("Bar", true, "AppIcon");
    sync();

    EXPECT_TRUE(fs::is_directory(destRoot / "Bar.app"));
    EXPECT_FALSE(fs::exists(destRoot / "Foo.app"));
    EXPECT_FALSE(fs::exists(destRoot / "Stale.app"));
    EXPECT_FALSE(fs::exists(destRoot / "garbage.txt"));
}

TEST_F(SpotlightAppsTest, idempotentRepeatedSync)
{
    makeBundle("Foo", true, "AppIcon");

    sync();
    sync();
    sync();

    EXPECT_TRUE(fs::is_regular_file(destRoot / "Foo.app" / "Contents" / "Info.plist"));
    EXPECT_TRUE(fs::is_regular_file(destRoot / "Foo.app" / "Contents" / "Resources" / "AppIcon.icns"));
    EXPECT_TRUE(fs::is_symlink(destRoot / "Foo.app" / "Contents" / "MacOS"));
}

TEST_F(SpotlightAppsTest, handlesIconKeyWithExplicitExtension)
{
    // CFBundleIconFile may already include the extension.
    makeBundle("Foo", true, "AppIcon.icns");

    sync();

    EXPECT_TRUE(fs::is_regular_file(destRoot / "Foo.app" / "Contents" / "Resources" / "AppIcon.icns"));
}

TEST_F(SpotlightAppsTest, ignoresIconKeyOutsideResources)
{
    makeBundle("Foo", true, "../Leaked");

    sync();

    EXPECT_TRUE(fs::is_regular_file(destRoot / "Foo.app" / "Contents" / "Info.plist"));
    EXPECT_FALSE(fs::exists(destRoot / "Foo.app" / "Contents" / "Resources" / "Leaked.icns"));
}

TEST_F(SpotlightAppsTest, handlesBundleWithoutIcon)
{
    makeBundle("Foo", true, /*iconKey=*/"");

    sync();

    EXPECT_TRUE(fs::is_regular_file(destRoot / "Foo.app" / "Contents" / "Info.plist"));
    EXPECT_TRUE(fs::is_symlink(destRoot / "Foo.app" / "Contents" / "MacOS"));
}

TEST_F(SpotlightAppsTest, ignoresNonAppEntriesInApplicationsDir)
{
    makeBundle("Foo", true, "AppIcon");
    writeFile(appsDir / "README.txt", "ignored");
    createDirs(appsDir / "stuff");

    sync();

    EXPECT_TRUE(fs::is_directory(destRoot / "Foo.app"));
    EXPECT_FALSE(fs::exists(destRoot / "README.txt"));
    EXPECT_FALSE(fs::exists(destRoot / "stuff"));
}

TEST_F(SpotlightAppsTest, followsProfileApplicationsSymlink)
{
    fs::path realAppsDir = tmpRoot / "real-applications";
    createDirs(realAppsDir);

    deletePath(appsDir);
    replaceSymlink(realAppsDir, appsDir);

    fs::path oldAppsDir = appsDir;
    appsDir = realAppsDir;
    makeBundle("Foo", true, "AppIcon");
    appsDir = oldAppsDir;

    sync();

    EXPECT_TRUE(fs::is_directory(destRoot / "Foo.app"));
    EXPECT_TRUE(fs::is_regular_file(destRoot / "Foo.app" / "Contents" / "Info.plist"));
}

TEST_F(SpotlightAppsTest, missingProfileIsHarmless)
{
    createDirs(destRoot / "Old.app" / "Contents");
    writeFile(destRoot / "Old.app" / "Contents" / "Info.plist", "old");

    detail::syncProfileAppBundlesAt(tmpRoot / "no-such-profile", destRoot, /*notifySystem=*/false);

    EXPECT_FALSE(fs::exists(destRoot));
}

TEST_F(SpotlightAppsTest, profileScopedDestinationsDoNotClobberEachOther)
{
    makeBundle("Foo", true, "AppIcon");

    fs::path base = tmpRoot / "base";
    fs::path firstDest = detail::profileAppBundlesDirAt(profile, base);
    detail::syncProfileAppBundlesAt(profile, firstDest, /*notifySystem=*/false);

    fs::path otherProfile = tmpRoot / "other-profile";
    createDirs(otherProfile);
    fs::path secondDest = detail::profileAppBundlesDirAt(otherProfile, base);
    detail::syncProfileAppBundlesAt(otherProfile, secondDest, /*notifySystem=*/false);

    EXPECT_TRUE(fs::is_regular_file(firstDest / "Foo.app" / "Contents" / "Info.plist"));
    EXPECT_FALSE(fs::exists(secondDest));
}

} // anonymous namespace

} // namespace nix::darwin
