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
        derivation::parse(
            *store,
            readFile(goldenMaster("bad-version.drv")),
            "whatever",
            derivation::defaultSupportWindowsStoreDir,
            mockXpSettings),
        FormatError);
}

TEST_F(DerivationTest, UnterminatedString)
{
    ASSERT_THROW(
        derivation::parse(
            *store,
            "Derive([(\"out\",\"/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo",
            "bar",
            derivation::defaultSupportWindowsStoreDir,
            mockXpSettings),
        FormatError);
}

/**
 * A fixed-output derivation states its output path, but that path is a
 * function of the content address, so a stated path that disagrees is
 * rejected rather than silently kept.
 *
 * This is also the coverage for the check itself, which `parseOutput`
 * compiles out under `FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION`.
 */
TEST_F(DerivationTest, CAFixedPathMismatch)
{
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    GTEST_SKIP() << "Path check is compiled out of fuzzer-instrumented builds";
#endif
    ASSERT_THROW(
        derivation::parse(
            *store,
            readFile(goldenMaster("bad-ca-fixed-path.drv")),
            "fixed",
            derivation::defaultSupportWindowsStoreDir,
            mockXpSettings),
        FormatError);
}

TEST_F(DynDerivationTest, BadATerm_oldVersionDynDeps)
{
    ASSERT_THROW(
        derivation::parse(
            *store,
            readFile(goldenMaster("bad-old-version-dyn-deps.drv")),
            "dyn-dep-derivation",
            derivation::defaultSupportWindowsStoreDir,
            mockXpSettings),
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

/**
 * A derivation, and how the ATerm format should be told to encode the
 * store paths in it.
 *
 * The defaults are a Unix store directory, which holds no character the
 * format escapes. A Windows one is `C:\ProgramData\nix\store`, whose `\`
 * must be escaped or it is read back as an escape sequence. See
 * `derivation::defaultSupportWindowsStoreDir`.
 */
struct DerivationTestCase
{
    Derivation drv;
    std::string storeDir = "/nix/store";
    bool supportWindowsStoreDir = false;
};

#define MAKE_TEST_P(FIXTURE)                                                                                \
    TEST_P(FIXTURE, from_json)                                                                              \
    {                                                                                                       \
        const auto & drv = GetParam().drv;                                                                  \
        readJsonTest(drv.name, drv, mockXpSettings);                                                        \
    }                                                                                                       \
                                                                                                            \
    TEST_P(FIXTURE, to_json)                                                                                \
    {                                                                                                       \
        const auto & drv = GetParam().drv;                                                                  \
        writeJsonTest(drv.name, drv);                                                                       \
    }                                                                                                       \
                                                                                                            \
    TEST_P(FIXTURE, from_aterm)                                                                             \
    {                                                                                                       \
        const auto & drv = GetParam().drv;                                                                  \
        StoreDirConfig storeCfg{GetParam().storeDir};                                                       \
        readTest(drv.name + ".drv", [&](auto encoded) {                                                     \
            auto got = derivation::parse(                                                                   \
                storeCfg, std::move(encoded), drv.name, GetParam().supportWindowsStoreDir, mockXpSettings); \
            using nlohmann::json;                                                                           \
            ASSERT_EQ(static_cast<json>(got), static_cast<json>(drv));                                      \
            ASSERT_EQ(got, drv);                                                                            \
        });                                                                                                 \
    }                                                                                                       \
                                                                                                            \
    TEST_P(FIXTURE, to_aterm)                                                                               \
    {                                                                                                       \
        const auto & drv = GetParam().drv;                                                                  \
        StoreDirConfig storeCfg{GetParam().storeDir};                                                       \
        writeTest(drv.name + ".drv", [&]() -> std::string {                                                 \
            return derivation::unparse(drv, storeCfg, GetParam().supportWindowsStoreDir);                   \
        });                                                                                                 \
    }

/**
 * A derivation for a Windows store directory, whose `\` the ATerm
 * format must escape.
 *
 * Note that `printStorePath` joins with `/` regardless, so a store path
 * in such a directory uses both separators.
 */
DerivationTestCase makeWindowsStoreDirCase()
{
    std::string storeDir = R"(C:\ProgramData\nix\store)";
    StorePath out{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo"};

    /* The same store path again, in a field that has always been
       escaped. It is what the outputs field disagreed with. Cannot be
       below, because the `outputs` field comes first, so this is the
       use that has to happen before `out` is moved. */
    std::string outEnv = StoreDirConfig{storeDir}.printStorePath(out);

    return {
        .drv{
            .outputs{
                {"out",
                 DerivationOutput::InputAddressed{
                     .path = std::move(out),
                 }},
            },
            .inputs{
                SingleDerivedPath::Opaque{
                    .path = StorePath{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb-bar"},
                },
            },
            .platform = "x86_64-windows",
            .builder = "builder.exe",
            .args = {"arg"},
            .env{
                {"out", std::move(outEnv)},
            },
            .name = "windows-store-dir",
        },
        .storeDir = std::move(storeDir),
        .supportWindowsStoreDir = true,
    };
}

struct DerivationJsonAtermTest : DerivationTest,
                                 JsonCharacterizationTest<Derivation>,
                                 ::testing::WithParamInterface<DerivationTestCase>
{};

MAKE_TEST_P(DerivationJsonAtermTest);

INSTANTIATE_TEST_SUITE_P(
    DerivationJSONATerm,
    DerivationJsonAtermTest,
    ::testing::Values(
        DerivationTestCase{
            .drv =
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
                }},
        makeWindowsStoreDirCase()));

struct DynDerivationJsonAtermTest : DynDerivationTest,
                                    JsonCharacterizationTest<Derivation>,
                                    ::testing::WithParamInterface<DerivationTestCase>
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

INSTANTIATE_TEST_SUITE_P(
    DynDerivationJSONATerm,
    DynDerivationJsonAtermTest,
    ::testing::Values(DerivationTestCase{.drv = makeDynDepDerivation()}));

#undef MAKE_TEST_P

/**
 * The properties of the flag that the characterization tests above do
 * not state, all of them about the two sides having to agree.
 */
struct WindowsStoreDirTest : DerivationTest
{
    DerivationTestCase testCase = makeWindowsStoreDirCase();
    StoreDirConfig store{testCase.storeDir};
};

/**
 * Without the support the store paths are written verbatim, so the `\n`
 * of `\nix` is read back as a newline and the derivation no longer says
 * what it was written to say. This is the state Windows was in.
 */
TEST_F(WindowsStoreDirTest, unsupportedIsUnreadable)
{
    auto aterm = derivation::unparse(testCase.drv, store, false);

    ASSERT_THROW(derivation::parse(store, std::move(aterm), testCase.drv.name, false, mockXpSettings), FormatError);
}

/**
 * An escape in a store path is rejected when the store directory does
 * not need one, since it would be a second encoding of the same value.
 */
TEST_F(WindowsStoreDirTest, escapeRejectedWhenUnsupported)
{
    auto aterm = derivation::unparse(testCase.drv, store, true);

    ASSERT_THROW(derivation::parse(store, std::move(aterm), testCase.drv.name, false, mockXpSettings), FormatError);
}

/**
 * The flag must not change a single byte for a Unix store directory, or
 * it would change every input address.
 */
TEST_F(WindowsStoreDirTest, unixUnaffected)
{
    const std::string unixStoreDir = "/nix/store";
    StoreDirConfig unixStore{unixStoreDir};
    auto drv = makeDynDepDerivation();

    ASSERT_EQ(derivation::unparse(drv, unixStore, true), derivation::unparse(drv, unixStore, false));
}

} // namespace nix
