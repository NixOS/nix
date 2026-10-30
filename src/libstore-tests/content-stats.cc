#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/store/content-stats.hh"
#include "nix/util/tests/characterization.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

TEST(ContentStats, bucket_boundaries)
{
    EXPECT_EQ(ContentStats::bucket(0), 0); // 0 is the special case (`bit_width(0) == 0`)
    EXPECT_EQ(ContentStats::bucket(1), 0); // `bit_width(1) - 1 = 0`, so 0 and 1 share the bucket
    EXPECT_EQ(ContentStats::bucket(2), 1);
    EXPECT_EQ(ContentStats::bucket(3), 1);
    EXPECT_EQ(ContentStats::bucket(1023), 9);
    EXPECT_EQ(ContentStats::bucket(1024), 10);
    EXPECT_EQ(ContentStats::bucket(1u << 20), 20);
    /* The top bucket saturates: `bit_width(UINT64_MAX) == 64`, so
       `64 - 1 = 63`, covering every value in [2^63, UINT64_MAX]. */
    EXPECT_EQ(ContentStats::bucket(uint64_t{1} << 63), 63);
    EXPECT_EQ(ContentStats::bucket(std::numeric_limits<uint64_t>::max()), 63);
}

TEST(ContentStats, bucketRange)
{
    EXPECT_EQ(ContentStats::bucketRange(0), (std::pair<uint64_t, uint64_t>{0, 2}));
    EXPECT_EQ(ContentStats::bucketRange(10), (std::pair<uint64_t, uint64_t>{1024, 2048}));
    EXPECT_EQ(
        ContentStats::bucketRange(63),
        (std::pair<uint64_t, uint64_t>{uint64_t{1} << 63, std::numeric_limits<uint64_t>::max()}));
}

/* Forward compatibility: a newer producer may add fields (which must
   be ignored) or omit fields (which default to zero / absent). */
TEST(ContentStats, from_json_lenient)
{
    auto json = nlohmann::json::parse(R"({
        "pathCount": 3,
        "someFutureField": {"x": 1},
        "dedup": {"linksFileCount": 2, "someFutureCounter": 7},
        "fullWalk": null
    })");
    auto stats = json.get<ContentStats>();
    EXPECT_EQ(
        stats,
        (ContentStats{
            .pathCount = 3,
            .dedup = ContentStats::Dedup{.linksFileCount = 2},
        }));
}

class ContentStatsTest : public virtual CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "content-stats";

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

struct ContentStatsJsonTest : ContentStatsTest,
                              JsonCharacterizationTest<ContentStats>,
                              ::testing::WithParamInterface<std::pair<std::string_view, ContentStats>>
{};

TEST_P(ContentStatsJsonTest, from_json)
{
    auto & [name, expected] = GetParam();
    readJsonTest(name, expected);
}

TEST_P(ContentStatsJsonTest, to_json)
{
    auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    ContentStatsJSON,
    ContentStatsJsonTest,
    ::testing::Values(
        std::pair{
            "empty",
            ContentStats{},
        },
        std::pair{
            "local",
            ContentStats{
                .pathCount = 42,
                .totalNarSize = 1024 * 1024,
                .dedup =
                    ContentStats::Dedup{
                        .linksFileCount = 30,
                        .uniqueBytes = 65536,
                        .uniqueDiskBytes = 73728,
                        .dedupBytes = 8192,
                        .dedupDiskBytes = 12288,
                        .dedupedFileCount = 7,
                        .inodesSaved = 14,
                    },
                .predictedDedup =
                    ContentStats::PredictedDedup{
                        .filesLinkable = 17,
                        .bytesLinkable = 4096,
                    },
                .fullWalk =
                    ContentStats::FullWalk{
                        .totalDiskBytes = 1u << 30,
                        .fileInodes = 100,
                        .dirInodes = 25,
                        .symlinkInodes = 8,
                    },
            },
        },
        std::pair{
            "histograms",
            ContentStats{
                .pathCount = 42,
                .totalNarSize = 1024 * 1024,
                .narSizeHistogram = ContentStats::Histogram{{0, 1}, {8, 10}, {12, 20}, {63, 11}},
                .dedup =
                    ContentStats::Dedup{
                        .linksFileCount = 30,
                        .uniqueBytes = 65536,
                        .uniqueDiskBytes = 73728,
                        .sizeHistogram = ContentStats::Histogram{{0, 3}, {10, 15}, {16, 12}},
                    },
            },
        }));

} // namespace nix
