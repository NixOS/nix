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

    EXPECT_EQ(config.rootDir.get(), std::optional<AbsolutePath>{std::string{root}});
}

TEST(LocalOverlayStore, constructConfig_rootPath)
{
#ifdef _WIN32
    constexpr std::string_view root = "C:\\foo\\bar";
#else
    constexpr std::string_view root = "/foo/bar";
#endif
    LocalOverlayStoreConfig config{std::string{root}, {}};

    EXPECT_EQ(config.rootDir.get(), std::optional<AbsolutePath>{std::string{root}});
}

TEST(LocalOverlayStore, upperLayer_notOverridden)
{
    LocalOverlayStoreConfig config{"", {}};
    EXPECT_FALSE(config.upperLayer.isOverridden());
}

TEST(LocalOverlayStore, upperLayer_overridden)
{
#ifdef _WIN32
    constexpr std::string_view upper = "C:\\some\\upper";
#else
    constexpr std::string_view upper = "/some/upper";
#endif
    LocalOverlayStoreConfig config{
        "",
        {
            {"upper-layer", std::string{upper}},
        },
    };
    EXPECT_TRUE(config.upperLayer.isOverridden());
    EXPECT_EQ(config.upperLayer.get(), std::optional<AbsolutePath>{std::string{upper}});
}

} // namespace nix
