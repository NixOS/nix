#include <gtest/gtest.h>
#include <cstdlib>
#include "globals.hh"
#include "logging.hh"

using namespace nix;

int main (int argc, char **argv) {
    if (argc > 1 && std::string_view(argv[1]) == "__build-remote") {
        printError("test-build-remote: not supported in libexpr unit tests");
        return 1;
    }

    // Disable build hook. We won't be testing remote builds in these unit tests. If we do, fix the above build hook.
    settings.buildHook = {};

    #if __linux__ // should match the conditional around sandboxBuildDir declaration.

    // When building and testing nix within the host's Nix sandbox, our store dir will be located in the host's sandboxBuildDir, e.g.:
    // Host
    //   storeDir = /nix/store
    //   sandboxBuildDir = /build
    // This process
    //   storeDir = /build/foo/bar/store
    //   sandboxBuildDir = /build
    // However, we have a rule that the store dir must not be inside the storeDir, so we need to pick a different sandboxBuildDir.
    settings.sandboxBuildDir = "/test-build-dir-instead-of-usual-build-dir";
    #endif

    #if __APPLE__
    // Avoid this error, when already running in a sandbox:
    // sandbox-exec: sandbox_apply: Operation not permitted
    settings.sandboxMode = smDisabled;
    setEnv("_NIX_TEST_NO_SANDBOX", "1");
    #endif

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
