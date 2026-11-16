#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

#include "nix/store/derivations.hh"
#include "nix/store/derivation/aterm.hh"
#include "nix/store/derivation/full-inputs.hh"
#include "derivation/test-support.hh"
#include "nix/util/tests/json-characterization.hh"
#include "nix/util/tests/gmock-matchers.hh"

namespace nix {

namespace {

struct MalformedATermTestCase
{
    std::string aterm;
    std::string msg;
    std::string description;
};

static std::ostream & operator<<(std::ostream & os, const MalformedATermTestCase & testCase)
{
    return os << testCase.aterm;
}

} // namespace

class MalformedATermTest : public LibStoreTest, public ::testing::WithParamInterface<MalformedATermTestCase>
{};

INSTANTIATE_TEST_SUITE_P(
    MalformedATerm,
    MalformedATermTest,
    ::testing::Values(
        MalformedATermTestCase{"", "expected 'D' character", "truncated_1"},
        MalformedATermTestCase{"D", "does not start with", "truncated_2"},
        MalformedATermTestCase{"Derive(", "expected '[' character", "truncated_3"},
        MalformedATermTestCase{"Derive([", "expected '(' character", "truncated_4"},
        MalformedATermTestCase{"Derive([(\"out\",\"", "unterminated", "truncated_5"},
        MalformedATermTestCase{"Derive([(\"out\",\"\xff", "unterminated", "truncated_6"},
        MalformedATermTestCase{
            R"(DrvWithVersion("invalid-version",)",
            "unknown derivation ATerm format version 'invalid-version'",
            "bad_version",
        },
        MalformedATermTestCase{
            R"(Derive([("out\n")",
            "unexpected escape sequence in unquoted string",
            /* Escapes are not allowed in the output name parsing. */
            "escapes_rejected_in_output_name",
        },
        /* FIXME: Sigh... output names are not checked. As the test below demonstrates, embedded NULs and DELs
           are not rejected and are written straight to the output map. This will be rejected by makeStorePath
           when trying to create a StorePath, but it's thin ice. */
        MalformedATermTestCase{
            [] {
                using namespace std::string_literals;
                return "Derive([(\"blahblah\x7F\x00\",\"/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo\",\"\",\"\")]"s;
            }(),
            /**/
            "expected ',' character",
            "bad_character_in_output_name",
        },
        MalformedATermTestCase{
            R"(Derive([("out","/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo)",
            "unterminated string in derivation",
            "unterminated_string",
        },
        MalformedATermTestCase{
            R"(Derive([("out","/nix/store/\naaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo")",
            "unexpected escape sequence in unquoted string",
            "escapes_rejected_in_store_path_string",
        },
        MalformedATermTestCase{
            R"(Derive([("out","/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo","sha256\0"))",
            "unknown escape '\\0'",
            "invalid_escapes_in_hash_algo",
        },
        MalformedATermTestCase{
            R"(Derive([("bar","/gnu/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo","","")",
            "path '/gnu/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo' is not in the Nix store",
            "bad_store_path_different_store",
        },
        MalformedATermTestCase{
            R"(Derive([("bar","/nix/store/foo/../aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo","","")",
            "store path basename 'foo/../aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-foo' contains illegal base-32 character 'o'",
            "bad_store_path_non_canonical",
        }),
    [](const ::testing::TestParamInfo<MalformedATermTestCase> & info) { return info.param.description; });

TEST_P(MalformedATermTest, throwsFormatError)
{
    ExperimentalFeatureSettings mockXpSettings;
    auto [drv, msg, _] = GetParam();
    ASSERT_THAT(
        [&]() { derivation::parse(*store, std::string(drv), "foo", /*supportWindowsStoreDir=*/false, mockXpSettings); },
        /* The exception isn't always FormatError, but we also throw BadStorePath too. */
        ::testing::ThrowsMessage<Error>(::nix::testing::HasSubstrIgnoreANSIMatcher(msg)))
        << drv;
}

class RoundtripATermTest : public DerivationTest, public ::testing::WithParamInterface<std::string_view>
{};

TEST_F(RoundtripATermTest, everythingGoes)
{
    /* Basically, test and characterise (in a binary file) how we encode and decode all chars
       when stored in a derivation. Some notable behavior:
       - Everything that can be escaped must be escaped.
       - Some fields are not validated at parse time.
       Derivation serialisers doesn't escape certain fields, so the deserialiser can't parse it
       back. This applies to output names and the platform.
       TLDR; it's all broken, fix ASAP. */

    /* Just a test string with all byte values. */
    std::string all(256, '\0');
    for (int i = 255; i >= 0; --i)
        all[i] = static_cast<unsigned char>(i);

    /* FIXME: Hack because the serialiser doesn't check the unencoded value at
       all and pastes it verbatim, but the decoder correctly rejects them. */
    std::string escaped;
    for (char c : all) {
        if (c == '\\' || c == '\n' || c == '\t' || c == '\r' || c == '"')
            /* Funnily enough, NULs aren't rejected. */
            escaped += '\0';
        else
            escaped += c;
    }

    std::string outputName = "+_-._?=abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890";
    StorePath outputPath("j5v9jrrqr8yn981qi5x5dvcx075j29gm-foo-" + outputName);

    Derivation drv = {
        /* Store path length limitations. */
        .outputs = {{outputName, DerivationOutput::InputAddressed{outputPath}}},
        /* Hopefully store path parsing is sufficiently tested. */
        .inputs = {},
        /* Platform is freeform text. */
        .platform = escaped,
        /* As is the builder and the arguments. We should probably disallow embedded NULs
           at parse time, although derivation builders also check at exec-time. */
        .builder = all,
        /* Same goes for arguments and env variables. NULs also rejected at exec-time. */
        .args = {all, all},
        /* Yes, this does mean that an env variable name can contain `=`. We don't currently
           reject that at parse time or exec-time, so this truncates the env variable name. */
        .env = {{all, all}, {outputName, store->printStorePath(outputPath)}},
        /* Never actually finds up in the serialisation. */
        .name = "foo",
    };

    /* The input-addressed path won't change. */
    ASSERT_NO_THROW(checkInvariants(drv, *store));

    writeTest(
        "aterm-encoding.drv", [&]() { return derivation::unparse(drv, *store, /*supportWindowsStoreDir=*/false); });
    readTest("aterm-encoding.drv", [&](std::string aterm) {
        ASSERT_EQ(derivation::parse(*store, std::move(aterm), drv.name), drv);
    });
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

/**
 * Builds a dynamic-derivation ATerm whose single `inputDrvs` entry is
 * nested `depth` levels deep.
 */
static std::string nestedDynDepsATerm(size_t depth)
{
    std::string node;
    for (size_t i = 0; i < depth; ++i)
        node += "([],[(\"out\",";
    node += "[\"out\"]";
    for (size_t i = 0; i < depth; ++i)
        node += ")])";
    return "DrvWithVersion(\"xp-dyn-drv\",[],[(\"/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-dep.drv\"," + node
           + ")],[],\"x86_64-linux\",\"foo\",[],[])";
}

TEST_F(DynDerivationTest, ATerm_nestedDynDeps)
{
    ASSERT_NO_THROW(
        derivation::parse(
            *store, nestedDynDepsATerm(16), "nested", derivation::defaultSupportWindowsStoreDir, mockXpSettings));
}

/**
 * Nesting deep enough to overflow the stack of an unbounded recursive
 * parser must be rejected with a parse error instead.
 */
TEST_F(DynDerivationTest, BadATerm_deeplyNestedDynDeps)
{
    ASSERT_THROW(
        derivation::parse(
            *store, nestedDynDepsATerm(100000), "nested", derivation::defaultSupportWindowsStoreDir, mockXpSettings),
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
