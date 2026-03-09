#include <gtest/gtest.h>

#include "nix/store/local-overlay-store.hh"

namespace nix {

TEST(LocalOverlayStore, constructConfig_rootQueryParam)
{
#ifdef _WIN32
    constexpr std::string_view root = "C:\\foo\\bar";
#else
    constexpr std::string_view root = "/foo/bar";
#endif
    LocalOverlayStoreConfig config{
        "",
        {
            {
                "root",
                std::string{root},
            },
        },
    };

    EXPECT_EQ(config.rootDir.get(), std::optional{std::string{root}});
}

TEST(LocalOverlayStore, constructConfig_rootPath)
{
#ifdef _WIN32
    constexpr std::string_view root = "C:\\foo\\bar";
#else
    constexpr std::string_view root = "/foo/bar";
#endif
    LocalOverlayStoreConfig config{std::string{root}, {}};

    EXPECT_EQ(config.rootDir.get(), std::optional{std::string{root}});
}

TEST(LocalOverlayStore, upperLayer_notOverridden)
{
    LocalOverlayStoreConfig config{"", {}};
    EXPECT_FALSE(config.upperLayer.isOverridden());
}

TEST(LocalOverlayStore, upperLayer_overridden)
{
    LocalOverlayStoreConfig config{
        "",
        {
            {"upper-layer", "/some/upper"},
        },
    };
    EXPECT_TRUE(config.upperLayer.isOverridden());
    EXPECT_EQ(config.upperLayer.get(), std::filesystem::path{"/some/upper"});
}

} // namespace nix
