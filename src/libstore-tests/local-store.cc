#include <gtest/gtest.h>

#include "nix/store/globals.hh"
#include "nix/store/local-store.hh"
#include "nix/store/tests/test-main.hh"

namespace nix {

TEST(LocalStore, constructConfig_rootQueryParam)
{
    auto settings = getTestSettings();
    LocalStoreConfig config{
        settings,
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
    auto settings = getTestSettings();
    LocalStoreConfig config{settings, "local", "/foo/bar", {}};

    EXPECT_EQ(config.rootDir.get(), std::optional{"/foo/bar"});
}

TEST(LocalStore, constructConfig_to_string)
{
    auto settings = getTestSettings();
    LocalStoreConfig config{settings, "local", "", {}};
    EXPECT_EQ(config.getReference().to_string(), "local");
}

} // namespace nix
