#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/store/derivations.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "derivation/test-support.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

using nlohmann::json;

TEST_F(DerivationTest, BadATerm_version)
{
    ASSERT_THROW(
        derivation::parse(*store, readFile(goldenMaster("bad-version.drv")), "whatever", mockXpSettings), FormatError);
}

TEST_F(DerivationTest, UnterminatedString)
{
    ASSERT_THROW(
        derivation::parse(
            *store, "Derive([(\"out\",\"/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo", "bar", mockXpSettings),
        FormatError);
}

TEST_F(DynDerivationTest, BadATerm_oldVersionDynDeps)
{
    ASSERT_THROW(
        derivation::parse(
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
                .ca{
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

#define MAKE_TEST_P(FIXTURE)                                                                    \
    TEST_P(FIXTURE, from_json)                                                                  \
    {                                                                                           \
        const auto & drv = GetParam();                                                          \
        readJsonTest(drv.name, drv, mockXpSettings);                                            \
    }                                                                                           \
                                                                                                \
    TEST_P(FIXTURE, to_json)                                                                    \
    {                                                                                           \
        const auto & drv = GetParam();                                                          \
        writeJsonTest(drv.name, drv, mockXpSettings);                                           \
    }                                                                                           \
                                                                                                \
    TEST_P(FIXTURE, from_aterm)                                                                 \
    {                                                                                           \
        const auto & drv = GetParam();                                                          \
        readTest(drv.name + ".drv", [&](auto encoded) {                                         \
            auto got = derivation::parse(*store, std::move(encoded), drv.name, mockXpSettings); \
            using nlohmann::json;                                                               \
            ASSERT_EQ(static_cast<json>(got), static_cast<json>(drv));                          \
            ASSERT_EQ(got, drv);                                                                \
        });                                                                                     \
    }                                                                                           \
                                                                                                \
    TEST_P(FIXTURE, to_aterm)                                                                   \
    {                                                                                           \
        const auto & drv = GetParam();                                                          \
        writeTest(drv.name + ".drv", [&]() -> std::string { return unparse(drv, *store); });    \
    }

struct DerivationJsonAtermTest : DerivationTest,
                                 JsonCharacterizationTest<Derivation>,
                                 ::testing::WithParamInterface<Derivation>
{};

MAKE_TEST_P(DerivationJsonAtermTest);

INSTANTIATE_TEST_SUITE_P(
    DerivationJSONATerm,
    DerivationJsonAtermTest,
    ::testing::Values(
        Derivation{
            .outputs = {},
            .inputs{
                SingleDerivedPath::Opaque{
                    .path = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"},
                },
                /* dep2.drv^cat */
                SingleDerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv"}),
                    .output = "cat",
                },
                /* dep2.drv^dog */
                SingleDerivedPath::Built{
                    .drvPath = makeConstantStorePathRef(StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv"}),
                    .output = "dog",
                },
            },
            .platform = "wasm-sel4",
            .builder = "foo",
            .args = {"bar", "baz"},
            .env{
                {"BIG_BAD", "WOLF"},
            },
            .name = "simple-derivation",
        }));

struct DynDerivationJsonAtermTest : DynDerivationTest,
                                    JsonCharacterizationTest<Derivation>,
                                    ::testing::WithParamInterface<Derivation>
{};

MAKE_TEST_P(DynDerivationJsonAtermTest);

Derivation makeDynDepDerivation()
{
    auto dep2 = makeConstantStorePathRef(StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep2.drv"});

    return Derivation{
        .outputs = {},
        .inputs{
            SingleDerivedPath::Opaque{
                .path = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"},
            },
            /* dep2.drv^cat */
            SingleDerivedPath::Built{
                .drvPath = dep2,
                .output = "cat",
            },
            /* dep2.drv^dog */
            SingleDerivedPath::Built{
                .drvPath = dep2,
                .output = "dog",
            },
            /* dep2.drv^cat^kitten */
            SingleDerivedPath::Built{
                .drvPath = make_ref<SingleDerivedPath>(SingleDerivedPath::Built{
                    .drvPath = dep2,
                    .output = "cat",
                }),
                .output = "kitten",
            },
            /* dep2.drv^goose^gosling */
            SingleDerivedPath::Built{
                .drvPath = make_ref<SingleDerivedPath>(SingleDerivedPath::Built{
                    .drvPath = dep2,
                    .output = "goose",
                }),
                .output = "gosling",
            },
        },
        .platform = "wasm-sel4",
        .builder = "foo",
        .args = {"bar", "baz"},
        .env{
            {"BIG_BAD", "WOLF"},
        },
        .name = "dyn-dep-derivation",
    };
}

INSTANTIATE_TEST_SUITE_P(DynDerivationJSONATerm, DynDerivationJsonAtermTest, ::testing::Values(makeDynDepDerivation()));

struct DerivationMetaJsonAtermTest : DerivationMetaTest,
                                     JsonCharacterizationTest<Derivation>,
                                     ::testing::WithParamInterface<Derivation>
{};

MAKE_TEST_P(DerivationMetaJsonAtermTest);

Derivation makeMetaDerivation()
{
    return Derivation{
        .outputs{
            {
                "out",
                DerivationOutput{DerivationOutput::InputAddressed{
                    .path = StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-meta-derivation"},
                }},
            },
        },
        .inputs{
            .srcs{
                StorePath{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep1"},
            },
            .drvs{},
        },
        .platform = "x86_64-linux",
        .builder = "/bin/sh",
        .args = {"-c", "echo hello > $out"},
        .env{
            {"out", "/nix/store/c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-meta-derivation"},
        },
        /* structuredAttrs is empty after `extractMeta`. */
        .structuredAttrs = StructuredAttrs{.structuredAttrs = {}},
        .meta =
            nlohmann::json::object_t{
                {"description", "A test derivation"},
                {"maintainer", "test@example.com"},
                {"version", "1.0"},
            },
        .name = "meta-derivation",
    };
}

INSTANTIATE_TEST_SUITE_P(DerivationMetaJSONATerm, DerivationMetaJsonAtermTest, ::testing::Values(makeMetaDerivation()));

#undef MAKE_TEST_P

} // namespace nix
