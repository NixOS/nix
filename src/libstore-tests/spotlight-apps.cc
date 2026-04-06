#include <fstream>

#include <gtest/gtest.h>

#include "nix/store/spotlight-apps.hh"
#include "nix/util/file-system.hh"

// Tests for the macOS Spotlight `.app` materializer. These exercise the
// file-ops half via the `notifySystem = false` test seam, so they never
// call LSRegisterURL or spawn mdimport - safe to run in CI without
// touching the host's Launch Services database or the real Spotlight
// indexer.

namespace nix::darwin {

namespace fs = std::filesystem;

namespace {

/**
 * Write a minimal `Info.plist` for a synthetic `.app` bundle.
 */
void writeInfoPlist(const fs::path & path, const std::string & bundleName, const std::string & iconKey = "")
{
    std::ofstream plist(path);
    plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
             "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
             "<plist version=\"1.0\"><dict>\n"
             "  <key>CFBundleIdentifier</key><string>org.nix.test."
          << bundleName
          << "</string>\n"
             "  <key>CFBundleName</key><string>"
          << bundleName << "</string>\n";
    if (!iconKey.empty())
        plist << "  <key>CFBundleIconFile</key><string>" << iconKey << "</string>\n";
    plist << "</dict></plist>\n";
}

class SpotlightAppsTest : public ::testing::Test
{
protected:
    std::optional<AutoDelete> tmpDirGuard;
    fs::path tmpRoot;
    fs::path profile;  // fake profile root (acts as the resolved generation)
    fs::path appsDir;  // profile/Applications
    fs::path destRoot; // where we ask the materializer to write

    void SetUp() override
    {
        tmpRoot = createTempDir("", "nix-spotlight-test");
        tmpDirGuard.emplace(tmpRoot, /*recursive=*/true);
        profile = tmpRoot / "profile";
        appsDir = profile / "Applications";
        destRoot = tmpRoot / "dest";
        fs::create_directories(appsDir);
        fs::create_directories(destRoot);
    }

    /**
     * Build a synthetic `.app` under the fake profile's `Applications/`
     * directory. Always creates `Contents/MacOS/<name>` (a no-op script);
     * optionally writes an `Info.plist` and an icon file referenced from
     * it. `extraResource`, if set, becomes a sibling of the icon under
     * `Contents/Resources/` to exercise the symlink-back loop.
     */
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
        fs::create_directories(macos);
        fs::create_directories(resources);

        std::ofstream(macos / name) << "#!/bin/sh\n";

        if (withInfoPlist)
            writeInfoPlist(contents / "Info.plist", name, iconKey);

        if (!iconKey.empty()) {
            // The materializer doesn't validate the file format, so any
            // non-empty bytes work as a stand-in icon.
            std::string iconName = iconKey;
            if (fs::path(iconName).extension().empty())
                iconName += ".icns";
            std::ofstream(resources / iconName) << "icns-stub";
        }

        if (!extraResource.empty())
            std::ofstream(resources / extraResource) << "extra";

        return app;
    }

    void sync()
    {
        detail::syncProfileAppBundlesAt(profile, destRoot, /*notifySystem=*/false);
    }
};

TEST_F(SpotlightAppsTest, materializesSingleBundleWithIcon)
{
    makeBundle("Foo", true, "AppIcon");

    sync();

    fs::path destApp = destRoot / "Foo.app";
    EXPECT_TRUE(fs::is_directory(destApp));

    // Info.plist and the icon are real files; everything else is a symlink.
    fs::path destInfoPlist = destApp / "Contents" / "Info.plist";
    EXPECT_TRUE(fs::is_regular_file(destInfoPlist));
    EXPECT_FALSE(fs::is_symlink(destInfoPlist));

    fs::path destIcon = destApp / "Contents" / "Resources" / "AppIcon.icns";
    EXPECT_TRUE(fs::is_regular_file(destIcon));
    EXPECT_FALSE(fs::is_symlink(destIcon));

    // Symlinks point at the canonical source path so the materialized
    // bundle survives intermediate generation deletion.
    fs::path destMacOS = destApp / "Contents" / "MacOS";
    EXPECT_TRUE(fs::is_symlink(destMacOS));
    fs::path expectedTarget = fs::canonical(appsDir / "Foo.app") / "Contents" / "MacOS";
    EXPECT_EQ(fs::read_symlink(destMacOS), expectedTarget);
}

TEST_F(SpotlightAppsTest, symlinksOtherResourceEntries)
{
    makeBundle("Foo", true, "AppIcon", "extra-data.bin");

    sync();

    fs::path destExtra = destRoot / "Foo.app" / "Contents" / "Resources" / "extra-data.bin";
    EXPECT_TRUE(fs::is_symlink(destExtra));
    fs::path expectedExtraTarget = fs::canonical(appsDir / "Foo.app") / "Contents" / "Resources" / "extra-data.bin";
    EXPECT_EQ(fs::read_symlink(destExtra), expectedExtraTarget);
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
    // Pre-seed the destination with a stale bundle and prove it gets
    // cleaned up when the profile has no Applications/ directory.
    fs::create_directories(destRoot / "Stale.app" / "Contents");
    std::ofstream(destRoot / "Stale.app" / "Contents" / "Info.plist") << "stale";
    fs::remove_all(appsDir);

    sync();

    EXPECT_FALSE(fs::exists(destRoot));
}

TEST_F(SpotlightAppsTest, wipeAndRebuildClearsStaleEntries)
{
    makeBundle("Foo", true, "AppIcon");
    sync();
    EXPECT_TRUE(fs::is_directory(destRoot / "Foo.app"));

    // Inject garbage that should be cleaned by the next sync.
    std::ofstream(destRoot / "garbage.txt") << "trash";
    fs::create_directories(destRoot / "Stale.app" / "Contents");
    std::ofstream(destRoot / "Stale.app" / "Contents" / "Info.plist") << "stale";

    // Replace Foo with Bar in the profile and re-sync.
    fs::remove_all(appsDir / "Foo.app");
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

TEST_F(SpotlightAppsTest, handlesBundleWithoutIcon)
{
    // Spotlight will show a blank icon, but the bundle should still
    // materialize.
    makeBundle("Foo", true, /*iconKey=*/"");

    sync();

    EXPECT_TRUE(fs::is_regular_file(destRoot / "Foo.app" / "Contents" / "Info.plist"));
    EXPECT_TRUE(fs::is_symlink(destRoot / "Foo.app" / "Contents" / "MacOS"));
}

TEST_F(SpotlightAppsTest, ignoresNonAppEntriesInApplicationsDir)
{
    makeBundle("Foo", true, "AppIcon");
    std::ofstream(appsDir / "README.txt") << "ignored";
    fs::create_directories(appsDir / "stuff");

    sync();

    EXPECT_TRUE(fs::is_directory(destRoot / "Foo.app"));
    EXPECT_FALSE(fs::exists(destRoot / "README.txt"));
    EXPECT_FALSE(fs::exists(destRoot / "stuff"));
}

TEST_F(SpotlightAppsTest, missingProfileIsHarmless)
{
    fs::create_directories(destRoot / "Old.app" / "Contents");
    std::ofstream(destRoot / "Old.app" / "Contents" / "Info.plist") << "old";

    detail::syncProfileAppBundlesAt(tmpRoot / "no-such-profile", destRoot, /*notifySystem=*/false);

    // Treated as "no apps" - destination dir is removed.
    EXPECT_FALSE(fs::exists(destRoot));
}

} // anonymous namespace

} // namespace nix::darwin
