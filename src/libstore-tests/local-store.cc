#include <gtest/gtest.h>

#include "local-store.hh"

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
