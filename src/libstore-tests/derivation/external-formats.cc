#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/store/derivations.hh"
#include "derivation/test-support.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

using nlohmann::json;

TEST_F(DerivationTest, BadATerm_version)
{
    ASSERT_THROW(
        parseDerivation(*store, readFile(goldenMaster("bad-version.drv")), "whatever", mockXpSettings), FormatError);
}

TEST_F(DynDerivationTest, BadATerm_oldVersionDynDeps)
{
    ASSERT_THROW(
        parseDerivation(
            *store, readFile(goldenMaster("bad-old-version-dyn-deps.drv")), "dyn-dep-derivation", mockXpSettings),
        FormatError);
}

#define MAKE_OUTPUT_JSON_TEST_P(FIXTURE)                                       \
    TEST_P(FIXTURE, from_json)                                                 \
    {                                                                          \
        const auto & [name, expected] = GetParam();                            \
        readJsonTest(std::string{"output-"} + name, expected, mockXpSettings); \
    }                                                                          \
                                                                               \
    TEST_P(FIXTURE, to_json)                                                   \
    {                                                                          \
        const auto & [name, value] = GetParam();                               \
        writeJsonTest(std::string{"output-"} + name, value);                   \
    }

struct DerivationOutputJsonTest : DerivationTest,
                                  JsonCharacterizationTest<DerivationOutput>,
                                  ::testing::WithParamInterface<std::pair<std::string_view, DerivationOutput>>
{};

MAKE_OUTPUT_JSON_TEST_P(DerivationOutputJsonTest)

INSTANTIATE_TEST_SUITE_P(
    DerivationOutputJSON,
    DerivationOutputJsonTest,
    ::testing::Values(
        std::pair{
            "inputAddressed",
            DerivationOutput{DerivationOutput::InputAddressed{
                .path = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-drv-name-output-name"},
            }},
        },
        std::pair{
            "caFixedFlat",
            DerivationOutput{DerivationOutput::CAFixed{
                .ca =
                    {
                        .method = ContentAddressMethod::Raw::Flat,
                        .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
                    },
            }},
        },
        std::pair{
            "caFixedNAR",
            DerivationOutput{DerivationOutput::CAFixed{
                .ca =
                    {
                        .method = ContentAddressMethod::Raw::NixArchive,
                        .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
                    },
            }},
        },
        std::pair{
            "deferred",
            DerivationOutput{DerivationOutput::Deferred{}},
        }));

struct DynDerivationOutputJsonTest : DynDerivationTest,
                                     JsonCharacterizationTest<DerivationOutput>,
                                     ::testing::WithParamInterface<std::pair<std::string_view, DerivationOutput>>
{};

MAKE_OUTPUT_JSON_TEST_P(DynDerivationOutputJsonTest);

INSTANTIATE_TEST_SUITE_P(
    DynDerivationOutputJSON,
    DynDerivationOutputJsonTest,
    ::testing::Values(
        std::pair{
            "caFixedText",
            DerivationOutput{DerivationOutput::CAFixed{
                .ca =
                    {
                        .method = ContentAddressMethod::Raw::Text,
                        .hash = Hash::parseAnyPrefixed("sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8="),
                    },
            }},
        }));

struct CaDerivationOutputJsonTest : CaDerivationTest,
                                    JsonCharacterizationTest<DerivationOutput>,
                                    ::testing::WithParamInterface<std::pair<std::string_view, DerivationOutput>>
{};

MAKE_OUTPUT_JSON_TEST_P(CaDerivationOutputJsonTest);

INSTANTIATE_TEST_SUITE_P(
    CaDerivationOutputJSON,
    CaDerivationOutputJsonTest,
    ::testing::Values(
        std::pair{
            "caFloating",
            DerivationOutput{DerivationOutput::CAFloating{
                .method = ContentAddressMethod::Raw::NixArchive,
                .hashAlgo = HashAlgorithm::SHA256,
            }},
        }));

struct ImpureDerivationOutputJsonTest : ImpureDerivationTest,
                                        JsonCharacterizationTest<DerivationOutput>,
                                        ::testing::WithParamInterface<std::pair<std::string_view, DerivationOutput>>
{};

MAKE_OUTPUT_JSON_TEST_P(ImpureDerivationOutputJsonTest);

INSTANTIATE_TEST_SUITE_P(
    ImpureDerivationOutputJSON,
    ImpureDerivationOutputJsonTest,
    ::testing::Values(
        std::pair{
            "impure",
            DerivationOutput{DerivationOutput::Impure{
                .method = ContentAddressMethod::Raw::NixArchive,
                .hashAlgo = HashAlgorithm::SHA256,
            }},
        }));

#undef MAKE_OUTPUT_JSON_TEST_P

#define MAKE_TEST_P(FIXTURE)                                                                          \
    TEST_P(FIXTURE, from_json)                                                                        \
    {                                                                                                 \
        const auto & drv = GetParam();                                                                \
        readJsonTest(drv.name, drv, mockXpSettings);                                                  \
    }                                                                                                 \
                                                                                                      \
    TEST_P(FIXTURE, to_json)                                                                          \
    {                                                                                                 \
        const auto & drv = GetParam();                                                                \
        using namespace nlohmann;                                                                     \
        writeTest(                                                                                    \
            drv.name + ".json",                                                                       \
            [&]() -> json {                                                                           \
                json res;                                                                             \
                derivationToJson(res, drv, mockXpSettings);                                           \
                return res;                                                                           \
            },                                                                                        \
            [](const auto & file) { return json::parse(readFile(file)); },                            \
            [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); }); \
    }                                                                                                 \
                                                                                                      \
    TEST_P(FIXTURE, from_aterm)                                                                       \
    {                                                                                                 \
        const auto & drv = GetParam();                                                                \
        readTest(drv.name + ".drv", [&](auto encoded) {                                               \
            auto got = parseDerivation(*store, std::move(encoded), drv.name, mockXpSettings);         \
            using nlohmann::json;                                                                     \
            ASSERT_EQ(static_cast<json>(got), static_cast<json>(drv));                                \
            ASSERT_EQ(got, drv);                                                                      \
        });                                                                                           \
    }                                                                                                 \
                                                                                                      \
    TEST_P(FIXTURE, to_aterm)                                                                         \
    {                                                                                                 \
        const auto & drv = GetParam();                                                                \
        writeTest(drv.name + ".drv", [&]() -> std::string { return drv.unparse(*store, false); });    \
    }

struct DerivationJsonAtermTest : DerivationTest,
                                 JsonCharacterizationTest<Derivation>,
                                 ::testing::WithParamInterface<Derivation>
{};

MAKE_TEST_P(DerivationJsonAtermTest);

Derivation makeSimpleDrv()
{
    Derivation drv;
    drv.name = "simple-derivation";
    drv.inputSrcs = {
        StorePath("c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"),
    };
    drv.inputDrvs = {
        .map =
            {
                {
                    StorePath("c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv"),
                    {
                        .value =
                            {
                                "cat",
                                "dog",
                            },
                    },
                },
            },
    };
    drv.platform = "wasm-sel4";
    drv.builder = "foo";
    drv.args = {
        "bar",
        "baz",
    };
    drv.env = StringPairs{
        {
            "BIG_BAD",
            "WOLF",
        },
    };
    return drv;
}

INSTANTIATE_TEST_SUITE_P(DerivationJSONATerm, DerivationJsonAtermTest, ::testing::Values(makeSimpleDrv()));

struct DynDerivationJsonAtermTest : DynDerivationTest,
                                    JsonCharacterizationTest<Derivation>,
                                    ::testing::WithParamInterface<Derivation>
{};

MAKE_TEST_P(DynDerivationJsonAtermTest);

Derivation makeDynDepDerivation()
{
    Derivation drv;
    drv.name = "dyn-dep-derivation";
    drv.inputSrcs = {
        StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"},
    };
    drv.inputDrvs = {
        .map =
            {
                {
                    StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv"},
                    DerivedPathMap<StringSet>::ChildNode{
                        .value =
                            {
                                "cat",
                                "dog",
                            },
                        .childMap =
                            {
                                {
                                    "cat",
                                    DerivedPathMap<StringSet>::ChildNode{
                                        .value =
                                            {
                                                "kitten",
                                            },
                                    },
                                },
                                {
                                    "goose",
                                    DerivedPathMap<StringSet>::ChildNode{
                                        .value =
                                            {
                                                "gosling",
                                            },
                                    },
                                },
                            },
                    },
                },
            },
    };
    drv.platform = "wasm-sel4";
    drv.builder = "foo";
    drv.args = {
        "bar",
        "baz",
    };
    drv.env = StringPairs{
        {
            "BIG_BAD",
            "WOLF",
        },
    };
    return drv;
}

INSTANTIATE_TEST_SUITE_P(DynDerivationJSONATerm, DynDerivationJsonAtermTest, ::testing::Values(makeDynDepDerivation()));

struct DerivationMetaJsonAtermTest : DerivationMetaTest,
                                     JsonCharacterizationTest<Derivation>,
                                     ::testing::WithParamInterface<Derivation>
{};

MAKE_TEST_P(DerivationMetaJsonAtermTest);

Derivation makeMetaDerivation()
{
    Derivation drv;
    drv.name = "meta-derivation";
    drv.inputSrcs = {
        StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"},
    };
    drv.platform = "x86_64-linux";
    drv.builder = "/bin/sh";
    drv.args = {
        "-c",
        "echo hello > $out",
    };
    drv.env = StringPairs{
        {
            "out",
            "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-meta-derivation",
        },
    };
    drv.outputs = {
        {
            "out",
            DerivationOutput{DerivationOutput::InputAddressed{
                .path = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-meta-derivation"},
            }},
        },
    };

    // Add structured attributes with __meta
    nlohmann::json::object_t structuredAttrs;
    structuredAttrs["__meta"] = nlohmann::json::object_t{
        {"description", "A test derivation"},
        {"version", "1.0"},
        {"maintainer", "test@example.com"},
    };
    structuredAttrs["requiredSystemFeatures"] = nlohmann::json::array({"derivation-meta"});

    drv.structuredAttrs = StructuredAttrs{.structuredAttrs = structuredAttrs};

    return drv;
}

INSTANTIATE_TEST_SUITE_P(DerivationMetaJSONATerm, DerivationMetaJsonAtermTest, ::testing::Values(makeMetaDerivation()));

#undef MAKE_TEST_P

} // namespace nix
