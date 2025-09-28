#include <gtest/gtest.h>

#include "nix/store/local-store.hh"

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

    EXPECT_EQ(config.rootDir, std::optional{"/foo/bar"});
}

TEST(LocalStore, constructConfig_rootPath)
{
    LocalStoreConfig config{"local", "/foo/bar", {}};

    EXPECT_EQ(config.rootDir, std::optional{"/foo/bar"});
}

TEST(LocalStore, constructConfig_to_string)
{
    LocalStoreConfig config{"local", "", {}};
    EXPECT_EQ(config.getReference().to_string(), "local");
}

} // namespace nix
