#include <gtest/gtest.h>

#include "nix/store/local-overlay-store.hh"

namespace nix {

TEST(LocalOverlayStore, constructConfig_rootQueryParam)
{
    constexpr std::string_view rootPath =
#ifdef _WIN32
        "C:\\foo\\bar"
#else
        "/foo/bar"
#endif
        ;
    LocalOverlayStoreConfig config{
        "",
        {
            {
                "root",
                std::string{rootPath},
            },
        },
    };

    EXPECT_EQ(config.rootDir.get(), std::optional{std::string{rootPath}});
}

TEST(LocalOverlayStore, constructConfig_rootPath)
{
    constexpr std::string_view rootPath =
#ifdef _WIN32
        "C:\\foo\\bar"
#else
        "/foo/bar"
#endif
        ;
    LocalOverlayStoreConfig config{std::string{rootPath}, {}};

    EXPECT_EQ(config.rootDir.get(), std::optional{std::string{rootPath}});
}

} // namespace nix
