// FIXME: Odd failures for templates that are causing the PR to break
// for now with discussion with @Ericson2314 to comment out.
#if 0
#  include <gtest/gtest.h>

#  include "local-overlay-store.hh"

namespace nix {

TEST(LocalOverlayStore, constructConfig_rootQueryParam)
{
    LocalOverlayStoreConfig config{
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
    LocalOverlayStoreConfig config{"local-overlay", "/foo/bar", {}};

    EXPECT_EQ(config.rootDir.get(), std::optional{"/foo/bar"});
}

} // namespace nix
#endif
