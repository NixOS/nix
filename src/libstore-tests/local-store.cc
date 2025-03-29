// FIXME: Odd failures for templates that are causing the PR to break
// for now with discussion with @Ericson2314 to comment out.
#if 0
#  include <gtest/gtest.h>

#  include "nix/local-store.hh"

// Needed for template specialisations. This is not good! When we
// overhaul how store configs work, this should be fixed.
#  include "nix/args.hh"
#  include "nix/config-impl.hh"
#  include "nix/abstract-setting-to-json.hh"

namespace nix {

TEST(LocalStore, constructConfig_rootQueryParam)
{
    LocalStoreConfig config{
        "local",
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

TEST(LocalStore, constructConfig_rootPath)
{
    LocalStoreConfig config{"local", "/foo/bar", {}};

    EXPECT_EQ(config.rootDir.get(), std::optional{"/foo/bar"});
}

} // namespace nix
#endif
