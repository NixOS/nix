#include <gtest/gtest.h>

#include "nix/store/local-store.hh"

// Needed for template specialisations. This is not good! When we
// overhaul how store configs work, this should be fixed.
#include "nix/util/args.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

namespace nix {

TEST(LocalStore, constructConfig_rootQueryParam)
{
    LocalStoreConfig config{
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
    LocalStoreConfig config{"/foo/bar", {}};

    EXPECT_EQ(config.rootDir.get(), std::optional{"/foo/bar"});
}

TEST(LocalStore, constructConfig_to_string)
{
    LocalStoreConfig config{"", {}};
    EXPECT_EQ(config.getReference().to_string(), "local");
}

} // namespace nix
