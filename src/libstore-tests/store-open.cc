#include <gtest/gtest.h>

#include "nix/store/store-open.hh"
#include "nix/store/store-reference.hh"
#include "nix/store/local-store.hh"
#include "nix/store/uds-remote-store.hh"
#include "nix/store/globals.hh"
#include "nix/util/file-system.hh"
#include "nix/util/finally.hh"

namespace nix {

TEST(StoreOpen, resolveStoreConfig_auto_default)
{
    // Save original settings
    //
    // TODO: resolveStoreConfig should not depend on global settings;
    // the test should not have to override them.
    auto originalStateDir = settings.nixStateDir;
    Finally restoreStateDir([&]() { settings.nixStateDir = originalStateDir; });

    // Set up a temporary writable state directory
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    auto stateDir = tmpDir / "var/nix";
    createDirs(stateDir);
    settings.nixStateDir = stateDir;

    StoreReference ref{
        .variant = StoreReference::Auto{},
        .params = {},
    };

    auto config = resolveStoreConfig(std::move(ref));

    // With a writable state directory and no daemon socket, "auto" should resolve to LocalStore
    auto * localConfig = dynamic_cast<LocalStore::Config *>(config.get());
    ASSERT_NE(localConfig, nullptr);
    EXPECT_EQ(localConfig->getStateDir(), stateDir);
}

TEST(StoreOpen, resolveStoreConfig_auto_withParams)
{
    // Create a temporary directory with a writable state directory
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    auto stateDir = tmpDir / "var/nix";
    createDirs(stateDir);

    StoreReference ref{
        .variant = StoreReference::Auto{},
        .params = {{"state", stateDir}},
    };

    auto config = resolveStoreConfig(std::move(ref));

    // With a writable state directory and no daemon socket, "auto" should resolve to LocalStore
    auto * localConfig = dynamic_cast<LocalStore::Config *>(config.get());
    ASSERT_NE(localConfig, nullptr);
    EXPECT_EQ(localConfig->getStateDir(), stateDir);
}

} // namespace nix
