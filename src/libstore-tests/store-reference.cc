#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "file-system.hh"
#include "store-reference.hh"

#include "tests/characterization.hh"
#include "tests/libstore.hh"

namespace nix {

using nlohmann::json;

class StoreReferenceTest : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "store-reference";

    std::filesystem::path goldenMaster(PathView testStem) const override
    {
        return unitTestData / testStem;
    }
};

#define URI_TEST_READ(STEM, OBJ)                                \
    TEST_F(StoreReferenceTest, PathInfo_##STEM##_from_uri)      \
    {                                                           \
        readTest(#STEM ".txt", ([&](const auto & encoded) {     \
                     StoreReference expected = OBJ;             \
                     auto got = StoreReference::parse(encoded); \
                     ASSERT_EQ(got, expected);                  \
                 }));                                           \
    }

#define URI_TEST_WRITE(STEM, OBJ)                                                               \
    TEST_F(StoreReferenceTest, PathInfo_##STEM##_to_uri)                                        \
    {                                                                                           \
        writeTest(                                                                              \
            #STEM ".txt",                                                                       \
            [&]() -> StoreReference { return OBJ; },                                            \
            [](const auto & file) { return StoreReference::parse(readFile(file)); },            \
            [](const auto & file, const auto & got) { return writeFile(file, got.render()); }); \
    }

#define URI_TEST(STEM, OBJ)  \
    URI_TEST_READ(STEM, OBJ) \
    URI_TEST_WRITE(STEM, OBJ)

#define JSON_TEST_READ(STEM, OBJ)                             \
    TEST_F(StoreReferenceTest, PathInfo_##STEM##_from_json)   \
    {                                                         \
        readTest(#STEM ".json", ([&](const auto & encoded_) { \
                     auto encoded = json::parse(encoded_);    \
                     StoreReference expected = OBJ;           \
                     StoreReference got = encoded;            \
                     ASSERT_EQ(got, expected);                \
                 }));                                         \
    }

#define JSON_TEST_WRITE(STEM, OBJ)                                                                          \
    TEST_F(StoreReferenceTest, PathInfo_##STEM##_to_json)                                                   \
    {                                                                                                       \
        writeTest(                                                                                          \
            #STEM ".json",                                                                                  \
            [&]() -> StoreReference { return OBJ; },                                                        \
            [](const auto & file) -> StoreReference { return json::parse(readFile(file)); },                \
            [](const auto & file, const auto & got) { return writeFile(file, json(got).dump(2) + "\n"); }); \
    }

#define JSON_TEST(STEM, OBJ)  \
    JSON_TEST_READ(STEM, OBJ) \
    JSON_TEST_WRITE(STEM, OBJ)

#define BOTH_FORMATS_TEST(STEM, OBJ) \
    URI_TEST(STEM, OBJ)              \
    JSON_TEST(STEM, OBJ)

BOTH_FORMATS_TEST(
    auto,
    (StoreReference{
        .variant = StoreReference::Auto{},
        .params = {},
    }))

BOTH_FORMATS_TEST(
    auto_param,
    (StoreReference{
        .variant = StoreReference::Auto{},
        .params =
            {
                {"root", "/foo/bar/baz"},
            },
    }))

static StoreReference localExample_1{
    .variant =
        StoreReference::Specified{
            .scheme = "local",
        },
    .params =
        {
            {"root", "/foo/bar/baz"},
        },
};

static StoreReference localExample_2{
    .variant =
        StoreReference::Specified{
            .scheme = "local",
            .authority = "/foo/bar/baz",
        },
    .params =
        {
            {"trusted", true},
        },
};

BOTH_FORMATS_TEST(local_1, localExample_1)

BOTH_FORMATS_TEST(local_2, localExample_2)

URI_TEST_READ(local_shorthand_1, localExample_1)

URI_TEST_READ(local_shorthand_2, localExample_2)

static StoreReference unixExample{
    .variant =
        StoreReference::Specified{
            .scheme = "unix",
        },
    .params =
        {
            {"max-connections", 7},
            {"trusted", true},
        },
};

BOTH_FORMATS_TEST(unix, unixExample)

URI_TEST_READ(unix_shorthand, unixExample)

BOTH_FORMATS_TEST(
    ssh,
    (StoreReference{
        .variant =
            StoreReference::Specified{
                .scheme = "ssh",
                .authority = "localhost",
            },
        .params = {},
    }))

}
