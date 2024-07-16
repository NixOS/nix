#include <gtest/gtest.h>

#include "local-store.hh"

// Needed for template specialisations. This is not good! When we
// overhaul how store configs work, this should be fixed.
#include "args.hh"
#include "config-impl.hh"
#include "abstract-setting-to-json.hh"

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
