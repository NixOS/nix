#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/store/path-info.hh"
#include "nix/store/nar-info.hh"

#include "nix/util/tests/characterization.hh"
#include "nix/store/tests/libstore.hh"

namespace nix {

using nlohmann::json;

class NarInfoTest : public CharacterizationTest, public LibStoreTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "nar-info";

    std::filesystem::path goldenMaster(PathView testStem) const override
    {
        return unitTestData / (testStem + ".json");
    }
};

static NarInfo makeNarInfo(const Store & store, bool includeImpureInfo)
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
        info.sigs = {"asdf", "qwer"};

        info.url = "nar/1w1fff338fvdw53sqgamddn1b2xgds473pv6y13gizdbqjv4i5p3.nar.xz";
        info.compression = "xz";
        info.fileHash = Hash::parseSRI("sha256-FePFYIlMuycIXPZbWi7LGEiMmZSX9FMbaQenWBzm1Sc=");
        info.fileSize = 4029176;
    }
    return info;
}

#define JSON_TEST(STEM, PURE)                                                                         \
    TEST_F(NarInfoTest, NarInfo_##STEM##_from_json)                                                   \
    {                                                                                                 \
        readTest(#STEM, [&](const auto & encoded_) {                                                  \
            auto encoded = json::parse(encoded_);                                                     \
            auto expected = makeNarInfo(*store, PURE);                                                \
            auto got = UnkeyedNarInfo::fromJSON(&*store, encoded);                                    \
            ASSERT_EQ(got, expected);                                                                 \
        });                                                                                           \
    }                                                                                                 \
                                                                                                      \
    TEST_F(NarInfoTest, NarInfo_##STEM##_to_json)                                                     \
    {                                                                                                 \
        writeTest(                                                                                    \
            #STEM,                                                                                    \
            [&]() -> json { return makeNarInfo(*store, PURE).toJSON(&*store, PURE); },                \
            [](const auto & file) { return json::parse(readFile(file)); },                            \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); }); \
    }

JSON_TEST(pure, false)
JSON_TEST(impure, true)

} // namespace nix
