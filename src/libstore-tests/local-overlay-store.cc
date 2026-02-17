#include <gtest/gtest.h>

#include "nix/store/globals.hh"
#include "nix/store/local-overlay-store.hh"
#include "nix/store/tests/test-main.hh"

namespace nix {

TEST(LocalOverlayStore, constructConfig_rootQueryParam)
{
    auto settings = getTestSettings();
    LocalOverlayStoreConfig config{
        settings,
        "local-overlay",
        "",
        {
            {
                "root",
                "/foo/bar",
            },
        },
    };

    EXPECT_EQ(config.rootDir.get(), std::optional{"/foo/bar"});
}

TEST(LocalOverlayStore, constructConfig_rootPath)
{
    auto settings = getTestSettings();
    LocalOverlayStoreConfig config{settings, "local-overlay", "/foo/bar", {}};

    EXPECT_EQ(config.rootDir.get(), std::optional{"/foo/bar"});
}

} // namespace nix
