#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/store/path-info.hh"
#include "nix/store/nar-info.hh"

#include "nix/util/tests/characterization.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

using nlohmann::json;

class NarInfoTestV1 : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "nar-info" / "json-1";

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / (testStem + ".json");
    }
};

class NarInfoTestV2 : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "nar-info" / "json-2";

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / (testStem + ".json");
    }
};

class NarInfoTestV3 : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "nar-info" / "json-3";

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / (testStem + ".json");
    }
};

static NarInfo makeNarInfo(const Store & store, bool includeImpureInfo, bool useAbsoluteUrl = false)
{
    auto info = NarInfo::makeFromCA(
        store,
        "foo",
        FixedOutputInfo{
            .method = FileIngestionMethod::NixArchive,
            .hash = hashString(HashAlgorithm::SHA256, "(...)"),

            .references =
                {
                    .others =
                        {
                            StorePath{
                                "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar",
                            },
                        },
                    .self = true,
                },
        },
        Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="));
    info.narSize = 34878;
    if (includeImpureInfo) {
        info.deriver = StorePath{
            "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar.drv",
        };
        info.registrationTime = 23423;
        info.ultimate = true;
        info.sigs = {
            Signature{.keyName = "asdf", .sig = std::string(64, '\0')},
            Signature{.keyName = "qwer", .sig = std::string(64, '\0')},
        };

        if (useAbsoluteUrl) {
            info.url = parseURL(
                "https://cache.example.com/nar/1w1fff338fvdw53sqgamddn1b2xgds473pv6y13gizdbqjv4i5p3.nar.xz?auth=secret");
        } else {
            info.url = ParsedRelativeUrl::parse(
                "nar/1w1fff338fvdw53sqgamddn1b2xgds473pv6y13gizdbqjv4i5p3.nar.xz?sha256=1w1fff338fvdw53sqgamddn1b2xgds473pv6y13gizdbqjv4i5p3");
        }
        info.compression = "xz";
        info.fileHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc=");
        info.fileSize = 4029176;
    }
    return info;
}

#define JSON_READ_TEST_V1(STEM, PURE)                              \
    TEST_F(NarInfoTestV1, NarInfo_##STEM##_from_json)              \
    {                                                              \
        readTest(#STEM, [&](const auto & encoded_) {               \
            auto encoded = json::parse(encoded_);                  \
            auto expected = makeNarInfo(*store, PURE);             \
            auto got = UnkeyedNarInfo::fromJSON(&*store, encoded); \
            ASSERT_EQ(got, expected);                              \
        });                                                        \
    }

#define JSON_WRITE_TEST_V1(STEM, PURE)                                                                         \
    TEST_F(NarInfoTestV1, NarInfo_##STEM##_to_json)                                                            \
    {                                                                                                          \
        writeTest(                                                                                             \
            #STEM,                                                                                             \
            [&]() -> json { return makeNarInfo(*store, PURE).toJSON(&*store, PURE, PathInfoJsonFormat::V1); }, \
            [](const auto & file) { return json::parse(readFile(file)); },                                     \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });          \
    }

#define JSON_TEST_V1(STEM, PURE)  \
    JSON_READ_TEST_V1(STEM, PURE) \
    JSON_WRITE_TEST_V1(STEM, PURE)

#define JSON_READ_TEST_V2(STEM, PURE)                              \
    TEST_F(NarInfoTestV2, NarInfo_##STEM##_from_json)              \
    {                                                              \
        readTest(#STEM, [&](const auto & encoded_) {               \
            auto encoded = json::parse(encoded_);                  \
            auto expected = makeNarInfo(*store, PURE);             \
            auto got = UnkeyedNarInfo::fromJSON(nullptr, encoded); \
            ASSERT_EQ(got, expected);                              \
        });                                                        \
    }

#define JSON_WRITE_TEST_V2(STEM, PURE)                                                                         \
    TEST_F(NarInfoTestV2, NarInfo_##STEM##_to_json)                                                            \
    {                                                                                                          \
        writeTest(                                                                                             \
            #STEM,                                                                                             \
            [&]() -> json { return makeNarInfo(*store, PURE).toJSON(nullptr, PURE, PathInfoJsonFormat::V2); }, \
            [](const auto & file) { return json::parse(readFile(file)); },                                     \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });          \
    }

#define JSON_TEST_V2(STEM, PURE)  \
    JSON_READ_TEST_V2(STEM, PURE) \
    JSON_WRITE_TEST_V2(STEM, PURE)

#define JSON_READ_TEST_V3(STEM, PURE)                              \
    TEST_F(NarInfoTestV3, NarInfo_##STEM##_from_json)              \
    {                                                              \
        readTest(#STEM, [&](const auto & encoded_) {               \
            auto encoded = json::parse(encoded_);                  \
            auto expected = makeNarInfo(*store, PURE);             \
            auto got = UnkeyedNarInfo::fromJSON(nullptr, encoded); \
            ASSERT_EQ(got, expected);                              \
        });                                                        \
    }

#define JSON_WRITE_TEST_V3(STEM, PURE)                                                                         \
    TEST_F(NarInfoTestV3, NarInfo_##STEM##_to_json)                                                            \
    {                                                                                                          \
        writeTest(                                                                                             \
            #STEM,                                                                                             \
            [&]() -> json { return makeNarInfo(*store, PURE).toJSON(nullptr, PURE, PathInfoJsonFormat::V3); }, \
            [](const auto & file) { return json::parse(readFile(file)); },                                     \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });          \
    }

#define JSON_TEST_V3(STEM, PURE)  \
    JSON_READ_TEST_V3(STEM, PURE) \
    JSON_WRITE_TEST_V3(STEM, PURE)

JSON_TEST_V1(pure, false)
JSON_TEST_V1(impure, true)

// Test that JSON without explicit version field parses as V1
JSON_READ_TEST_V1(pure_noversion, false)

JSON_TEST_V2(pure, false)
JSON_TEST_V2(impure, true)

JSON_TEST_V3(pure, false)
JSON_TEST_V3(impure, true)

#undef JSON_TEST_V1
#undef JSON_READ_TEST_V1
#undef JSON_WRITE_TEST_V1

#undef JSON_TEST_V2
#undef JSON_READ_TEST_V2
#undef JSON_WRITE_TEST_V2

#undef JSON_TEST_V3
#undef JSON_READ_TEST_V3
#undef JSON_WRITE_TEST_V3

// Tests for absolute URLs in the url field
#define JSON_READ_TEST_V2_ABSURL(STEM)                             \
    TEST_F(NarInfoTestV2, NarInfo_##STEM##_from_json)              \
    {                                                              \
        readTest(#STEM, [&](const auto & encoded_) {               \
            auto encoded = json::parse(encoded_);                  \
            auto expected = makeNarInfo(*store, true, true);       \
            auto got = UnkeyedNarInfo::fromJSON(nullptr, encoded); \
            ASSERT_EQ(got, expected);                              \
        });                                                        \
    }

#define JSON_WRITE_TEST_V2_ABSURL(STEM)                                                                              \
    TEST_F(NarInfoTestV2, NarInfo_##STEM##_to_json)                                                                  \
    {                                                                                                                \
        writeTest(                                                                                                   \
            #STEM,                                                                                                   \
            [&]() -> json { return makeNarInfo(*store, true, true).toJSON(nullptr, true, PathInfoJsonFormat::V2); }, \
            [](const auto & file) { return json::parse(readFile(file)); },                                           \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });                \
    }

#define JSON_TEST_V2_ABSURL(STEM)  \
    JSON_READ_TEST_V2_ABSURL(STEM) \
    JSON_WRITE_TEST_V2_ABSURL(STEM)

JSON_TEST_V2_ABSURL(impure_absolute_url)

// Text format (.narinfo) tests

class NarInfoTextTest : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "nar-info" / "text";

    std::filesystem::path goldenMaster(PathView testStem) const override
    {
        return unitTestData / (testStem + ".narinfo");
    }
};

#define TEXT_READ_TEST(STEM, USE_ABSURL)                           \
    TEST_F(NarInfoTextTest, NarInfo_##STEM##_from_text)            \
    {                                                              \
        readTest(#STEM, [&](const auto & encoded) {                \
            auto expected = makeNarInfo(*store, true, USE_ABSURL); \
            /* Text format doesn't have these fields */            \
            expected.registrationTime = 0;                         \
            expected.ultimate = false;                             \
            NarInfo got{*store, encoded, "test"};                  \
            ASSERT_EQ(got, expected);                              \
        });                                                        \
    }

#define TEXT_WRITE_TEST(STEM, USE_ABSURL)                                                             \
    TEST_F(NarInfoTextTest, NarInfo_##STEM##_to_text)                                                 \
    {                                                                                                 \
        writeTest(                                                                                    \
            #STEM,                                                                                    \
            [&]() -> std::string { return makeNarInfo(*store, true, USE_ABSURL).to_string(*store); }, \
            [](const auto & file) { return readFile(file); },                                         \
            [](const auto & file, const auto & got) { return writeFile(file, got); });                \
    }

#define TEXT_TEST(STEM, USE_ABSURL)  \
    TEXT_READ_TEST(STEM, USE_ABSURL) \
    TEXT_WRITE_TEST(STEM, USE_ABSURL)

TEXT_TEST(relative_url, false)
TEXT_TEST(absolute_url, true)

} // namespace nix
