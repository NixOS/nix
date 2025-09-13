#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/store/path-info.hh"

#include "nix/util/tests/characterization.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

using nlohmann::json;

class PathInfoTest : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "path-info";

    std::filesystem::path goldenMaster(PathView testStem) const override
    {
        return unitTestData / (testStem + ".json");
    }
};

static UnkeyedValidPathInfo makeEmpty()
{
    return {
        Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc="),
    };
}

static ValidPathInfo makeFullKeyed(const Store & store, bool includeImpureInfo)
{
    auto info = ValidPathInfo::makeFromCA(
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
        info.sigs = {"asdf", "qwer"};
    }
    return info;
}

static UnkeyedValidPathInfo makeFull(const Store & store, bool includeImpureInfo)
{
    return makeFullKeyed(store, includeImpureInfo);
}

#define JSON_TEST(STEM, OBJ, PURE)                                                                    \
    TEST_F(PathInfoTest, PathInfo_##STEM##_from_json)                                                 \
    {                                                                                                 \
        readTest(#STEM, [&](const auto & encoded_) {                                                  \
            auto encoded = json::parse(encoded_);                                                     \
            UnkeyedValidPathInfo got = UnkeyedValidPathInfo::fromJSON(&*store, encoded);              \
            auto expected = OBJ;                                                                      \
            ASSERT_EQ(got, expected);                                                                 \
        });                                                                                           \
    }                                                                                                 \
                                                                                                      \
    TEST_F(PathInfoTest, PathInfo_##STEM##_to_json)                                                   \
    {                                                                                                 \
        writeTest(                                                                                    \
            #STEM,                                                                                    \
            [&]() -> json { return OBJ.toJSON(&*store, PURE); },                                      \
            [](const auto & file) { return json::parse(readFile(file)); },                            \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); }); \
    }

JSON_TEST(empty_pure, makeEmpty(), false)
JSON_TEST(empty_impure, makeEmpty(), true)

JSON_TEST(pure, makeFull(*store, false), false)
JSON_TEST(impure, makeFull(*store, true), true)

TEST_F(PathInfoTest, PathInfo_full_shortRefs)
{
    ValidPathInfo it = makeFullKeyed(*store, true);
    // it.references = unkeyed.references;
    auto refs = it.shortRefs();
    ASSERT_EQ(refs.size(), 2u);
    ASSERT_EQ(*refs.begin(), "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar");
    ASSERT_EQ(*++refs.begin(), "n5wkd9frr45pa74if5gpz9j7mifg27fh-foo");
}

} // namespace nix
