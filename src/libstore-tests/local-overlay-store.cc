#include <gtest/gtest.h>

#include "nix/store/local-overlay-store.hh"

namespace nix {

TEST(LocalOverlayStore, constructConfig_rootQueryParam)
{
    LocalOverlayStoreConfig config{
        "",
        {
            {
                "root",
                "/foo/bar",
            },
        },
    };

    EXPECT_EQ(config.rootDir.get(), std::optional<AbsolutePath>{"/foo/bar"});
}

TEST(LocalOverlayStore, constructConfig_rootPath)
{
    LocalOverlayStoreConfig config{"/foo/bar", {}};

    EXPECT_EQ(config.rootDir.get(), std::optional<AbsolutePath>{"/foo/bar"});
}

} // namespace nix
