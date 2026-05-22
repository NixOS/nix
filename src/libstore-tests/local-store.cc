#include <gtest/gtest.h>

#include "nix/store/local-store.hh"
#include "nix/store/store-api.hh"

// Needed for template specialisations. This is not good! When we
// overhaul how store configs work, this should be fixed.
#include "nix/util/args.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

namespace nix {

TEST(ContentStats, bucket_boundaries)
{
    using B = Store::ContentStats;
    EXPECT_EQ(B::bucket(0), 0);  // 0 is the special case (`bit_width(0) == 0`)
    EXPECT_EQ(B::bucket(1), 0);  // `bit_width(1) - 1 = 0`, so 0 and 1 share the bucket
    EXPECT_EQ(B::bucket(2), 1);
    EXPECT_EQ(B::bucket(3), 1);
    EXPECT_EQ(B::bucket(1023), 9);
    EXPECT_EQ(B::bucket(1024), 10);
    EXPECT_EQ(B::bucket(1u << 20), 20);
    /* The top bucket saturates: `bit_width(UINT64_MAX) == 64`, so
       `64 - 1 = 63`, covering every value in [2^63, UINT64_MAX]. */
    EXPECT_EQ(B::bucket(uint64_t{1} << 63), 63);
    EXPECT_EQ(B::bucket(std::numeric_limits<uint64_t>::max()), 63);
}

TEST(LocalStore, storeDir_absolutePath)
{
    std::filesystem::path storeDir =
#ifdef _WIN32
        "C:\\";
#else
        "/";
#endif
    storeDir /= "nix";
    storeDir /= "store";
    LocalStoreConfig config{"", {{"store", storeDir.string()}}};
    EXPECT_EQ(config.storeDir, storeDir.string());
}

TEST(LocalStore, storeDir_relativePath_rejected)
{
    EXPECT_THROW(LocalStoreConfig("", {{"store", (std::filesystem::path{"nix"} / "store").string()}}), UsageError);
}

TEST(LocalStore, storeDir_empty_rejected)
{
    EXPECT_THROW(LocalStoreConfig("", {{"store", ""}}), UsageError);
}

TEST(LocalStore, constructConfig_rootQueryParam)
{
#ifdef _WIN32
    constexpr std::string_view root = "C:\\foo\\bar";
#else
    constexpr std::string_view root = "/foo/bar";
#endif
    LocalStoreConfig config{
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

TEST(LocalStore, constructConfig_rootPath)
{
#ifdef _WIN32
    constexpr std::string_view root = "C:\\foo\\bar";
#else
    constexpr std::string_view root = "/foo/bar";
#endif
    LocalStoreConfig config{std::string{root}, {}};

    EXPECT_EQ(config.rootDir.get(), std::optional<AbsolutePath>{std::string{root}});
}

TEST(LocalStore, constructConfig_to_string)
{
    LocalStoreConfig config{"", {}};
    EXPECT_EQ(config.getReference().to_string(), "local");
}

} // namespace nix
