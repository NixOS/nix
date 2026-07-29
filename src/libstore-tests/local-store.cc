#include <gtest/gtest.h>

#include "nix/store/build-control-store.hh"
#include "nix/store/local-store.hh"
#include "nix/store/pathlocks.hh"
#include "nix/store/store-open.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"

#ifndef _WIN32
#  include <csignal>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

// Needed for template specialisations. This is not good! When we
// overhaul how store configs work, this should be fixed.
#include "nix/util/args.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

namespace nix {

TEST(LocalStore, storeDir_absolutePath)
{
    std::filesystem::path storeDir =
#ifdef _WIN32
        "C:\\";
#else
        "/";
#endif
    storeDir /= "nix";
    storeDir /= "store";
    LocalStoreConfig config{"", {{"store", storeDir.string()}}};
    EXPECT_EQ(config.storeDir, storeDir.string());
}

TEST(LocalStore, storeDir_relativePath_rejected)
{
    EXPECT_THROW(LocalStoreConfig("", {{"store", (std::filesystem::path{"nix"} / "store").string()}}), UsageError);
}

TEST(LocalStore, storeDir_empty_rejected)
{
    EXPECT_THROW(LocalStoreConfig("", {{"store", ""}}), UsageError);
}

TEST(LocalStore, constructConfig_rootQueryParam)
{
#ifdef _WIN32
    constexpr std::string_view root = "C:\\foo\\bar";
#else
    constexpr std::string_view root = "/foo/bar";
#endif
    LocalStoreConfig config{
        "",
        {
            {
                "root",
                std::string{root},
            },
        },
    };

    EXPECT_EQ(config.rootDir.get(), std::optional<AbsolutePath>{std::string{root}});
}

TEST(LocalStore, constructConfig_rootPath)
{
#ifdef _WIN32
    constexpr std::string_view root = "C:\\foo\\bar";
#else
    constexpr std::string_view root = "/foo/bar";
#endif
    LocalStoreConfig config{std::string{root}, {}};

    EXPECT_EQ(config.rootDir.get(), std::optional<AbsolutePath>{std::string{root}});
}

TEST(LocalStore, constructConfig_to_string)
{
    LocalStoreConfig config{"", {}};
    EXPECT_EQ(config.getReference().to_string(), "local");
}

#if defined(__linux__) || defined(__APPLE__)
struct KillBuildTest
{
    std::filesystem::path tempDir = createTempDir();
    AutoDelete deleteTempDir{tempDir, true};
    std::filesystem::path realStoreDir = tempDir / "nix/store";
    ref<Store> store;
    BuildControlStore & buildControlStore;
    StorePath path;
    std::filesystem::path realPath;

    explicit KillBuildTest(std::string_view name)
        : store([&] {
            createDirs(realStoreDir);
            return openStore(fmt("local?root=%s", tempDir.string()));
        }())
        , buildControlStore(dynamic_cast<BuildControlStore &>(*store))
        , path(StorePath::random(name))
        , realPath(realStoreDir / std::string{path.to_string()})
    {
    }
};

TEST(LocalStore, killActiveBuild)
{
    KillBuildTest test("active-build-test");

    Pipe ready;
    ready.create();
    Pid child = startProcess([&] {
        ready.readSide.close();
        PathLocks owner({test.realPath}, "", LockOwnerTracking::Yes);
        writeFull(ready.writeSide.get(), "1", false);
        while (true)
            pause();
    });
    auto childPid = static_cast<uint64_t>(static_cast<pid_t>(child));
    ready.writeSide.close();

    char ignored;
    readFull(ready.readSide.get(), &ignored, 1);

    EXPECT_EQ(test.buildControlStore.killBuild(test.path), childPid);
    auto status = child.wait();
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGTERM);
}

TEST(LocalStore, refusesUnsafeForcedTermination)
{
    KillBuildTest test("stubborn-build-test");

    Pipe ready;
    ready.create();
    Pid child = startProcess([&] {
        ready.readSide.close();
        signal(SIGTERM, SIG_IGN);
        PathLocks owner({test.realPath}, "", LockOwnerTracking::Yes);
        writeFull(ready.writeSide.get(), "1", false);
        while (true)
            pause();
    });
    auto childPid = static_cast<pid_t>(child);
    ready.writeSide.close();

    char ignored;
    readFull(ready.readSide.get(), &ignored, 1);

    EXPECT_THROW(test.buildControlStore.killBuild(test.path), Error);
    EXPECT_EQ(::kill(childPid, 0), 0);
    ASSERT_EQ(::kill(childPid, SIGKILL), 0);
    auto status = child.wait();
    EXPECT_TRUE(WIFSIGNALED(status));
}

TEST(LocalStore, doesNotTerminateAReplacementLockOwner)
{
    KillBuildTest test("replacement-build-test");

    Pipe firstReady;
    firstReady.create();
    Pid first = startProcess([&] {
        firstReady.readSide.close();
        PathLocks owner({test.realPath}, "", LockOwnerTracking::Yes);
        writeFull(firstReady.writeSide.get(), "1", false);
        while (true)
            pause();
    });
    firstReady.writeSide.close();

    char ignored;
    readFull(firstReady.readSide.get(), &ignored, 1);

    Pipe secondReady;
    secondReady.create();
    Pid second = startProcess([&] {
        secondReady.readSide.close();
        PathLocks owner({test.realPath}, "", LockOwnerTracking::Yes);
        writeFull(secondReady.writeSide.get(), "1", false);
        while (true)
            pause();
    });
    secondReady.writeSide.close();

    EXPECT_EQ(test.buildControlStore.killBuild(test.path), static_cast<uint64_t>(static_cast<pid_t>(first)));
    auto firstStatus = first.wait();
    EXPECT_TRUE(WIFSIGNALED(firstStatus));
    readFull(secondReady.readSide.get(), &ignored, 1);
    EXPECT_EQ(::kill(static_cast<pid_t>(second), 0), 0);

    ASSERT_EQ(::kill(static_cast<pid_t>(second), SIGTERM), 0);
    auto secondStatus = second.wait();
    EXPECT_TRUE(WIFSIGNALED(secondStatus));
}
#endif

} // namespace nix
